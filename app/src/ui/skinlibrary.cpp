#include "skinlibrary.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <utility>

namespace {
constexpr qint64 CatalogLimit = 256 * 1024;
constexpr qint64 ManifestLimit = 32 * 1024;
constexpr qint64 ImageLimit = 4 * 1024 * 1024;
constexpr quint32 ImageAxisLimit = 4096;
constexpr quint64 ImagePixelLimit = 4 * 1024 * 1024;
constexpr int CatalogEntryLimit = 128;
constexpr int RequestTimeoutMs = 15000;
const auto ManifestFile = QStringLiteral("theme.json");
const auto SidebarFile = QStringLiteral("sidebar.png");

bool validId(const QString &id)
{
    static const QRegularExpression syntax(QStringLiteral("\\A[a-z0-9]+(?:-[a-z0-9]+)*\\z"));
    static const QRegularExpression reserved(QStringLiteral("\\A(?:mist|paper|precision|con|prn|aux|nul|com[0-9]|lpt[0-9])\\z"));
    return id.size() <= 40 && syntax.match(id).hasMatch() && !reserved.match(id).hasMatch();
}

bool validName(const QString &name)
{
    if (name.isEmpty() || name.size() > 64 || name.trimmed() != name
        || name.contains(u'<') || name.contains(u'>')) return false;
    for (const auto codePoint : name.toUcs4()) {
        const auto category = QChar::category(codePoint);
        if (category == QChar::Other_Control || category == QChar::Other_Format
            || category == QChar::Separator_Line || category == QChar::Separator_Paragraph)
            return false;
    }
    return true;
}

QJsonObject versionedObject(const QByteArray &bytes)
{
    const auto object = QJsonDocument::fromJson(bytes).object();
    return object.value(QStringLiteral("schemaVersion")).toInt() == 1 ? object : QJsonObject{};
}

std::optional<QList<SkinCatalogEntry>> parseCatalog(const QByteArray &bytes)
{
    const auto themes = versionedObject(bytes).value(QStringLiteral("themes"));
    if (!themes.isArray()) return std::nullopt;
    const auto entries = themes.toArray();
    if (entries.size() > CatalogEntryLimit) return std::nullopt;

    QSet<QString> ids;
    QList<SkinCatalogEntry> catalog;
    for (const auto &entry : entries) {
        const auto object = entry.toObject();
        const auto id = object.value(QStringLiteral("id")).toString();
        const auto name = object.value(QStringLiteral("name")).toString();
        if (!validId(id) || !validName(name) || ids.contains(id)) return std::nullopt;
        ids.insert(id);
        catalog.append({id, name});
    }
    return catalog;
}

std::optional<SkinTheme> parseManifest(const QByteArray &bytes, const QString &expectedId)
{
    const auto object = versionedObject(bytes);
    SkinTheme theme{
        object.value(QStringLiteral("id")).toString(),
        object.value(QStringLiteral("name")).toString(),
        object.value(QStringLiteral("base")).toString(),
        {},
        {}
    };
    if (theme.id != expectedId || !validName(theme.name)
        || (theme.base != QStringLiteral("light") && theme.base != QStringLiteral("dark")))
        return std::nullopt;

    static const QRegularExpression colorKey(QStringLiteral("\\A[a-z][A-Za-z0-9]{0,39}\\z"));
    static const QRegularExpression colorValue(QStringLiteral("\\A#[0-9A-Fa-f]{6}\\z"));
    const auto colors = object.value(QStringLiteral("colors"));
    if (!colors.isUndefined() && !colors.isObject()) return std::nullopt;
    const auto palette = colors.toObject();
    for (auto it = palette.begin(); it != palette.end(); ++it) {
        const auto value = it.value().toString();
        if (!colorKey.match(it.key()).hasMatch() || !colorValue.match(value).hasMatch()) return std::nullopt;
        theme.colors.insert(it.key(), value);
    }

    const auto image = object.value(QStringLiteral("sidebarImage"));
    if (!image.isUndefined()) {
        if (image.toString() != SidebarFile) return std::nullopt;
        theme.sidebarImagePath = SidebarFile;
    }
    return theme;
}

bool hasLink(const QFileInfo &info)
{
    return info.isSymLink() || info.isJunction();
}

bool localDirectory(const QFileInfo &info)
{
    return info.isDir() && !hasLink(info);
}

QFileInfo themeDirectory(const QDir &root, const QString &id)
{
    const auto entries = root.entryInfoList({id}, QDir::Dirs | QDir::NoSymLinks | QDir::CaseSensitive);
    if (entries.size() == 1 && entries.first().fileName() == id && localDirectory(entries.first()))
        return entries.first();
    return {};
}

bool openPackageFile(QFile &file, qint64 limit)
{
    const QFileInfo info(file);
    return info.isFile() && !hasLink(info) && file.open(QIODevice::ReadOnly)
        && file.size() > 0 && file.size() <= limit;
}

bool validPng(QIODevice &file, bool decode)
{
    static const auto pngHeader = QByteArray::fromHex("89504e470d0a1a0a0000000d49484452");
    static const auto pngEnd = QByteArray::fromHex("0000000049454e44ae426082");
    const auto header = file.peek(33);
    if (file.size() < 57 || header.size() != 33 || !header.startsWith(pngHeader)) return false;
    const quint32 width = qFromBigEndian<quint32>(header.constData() + 16);
    const quint32 height = qFromBigEndian<quint32>(header.constData() + 20);
    if (!width || !height || width > ImageAxisLimit || height > ImageAxisLimit
        || quint64(width) * height > ImagePixelLimit) return false;
    if (!file.seek(file.size() - pngEnd.size()) || file.read(pngEnd.size()) != pngEnd) return false;
    // Startup checks the local file envelope. Downloads also validate the complete decode.
    if (!decode) return true;
    if (!file.seek(0)) return false;
    QImageReader reader(&file, "png");
    reader.setAutoDetectImageFormat(false);
    return !reader.read().isNull();
}

std::optional<SkinTheme> readInstalled(const QFileInfo &directory)
{
    if (!validId(directory.fileName()) || !localDirectory(directory)) return std::nullopt;
    const QDir path(directory.absoluteFilePath());
    QFile manifest(path.filePath(ManifestFile));
    if (!openPackageFile(manifest, ManifestLimit)) return std::nullopt;
    const auto bytes = manifest.read(ManifestLimit + 1);
    if (bytes.size() > ManifestLimit || manifest.error() != QFile::NoError) return std::nullopt;
    auto theme = parseManifest(bytes, directory.fileName());
    if (theme && !theme->sidebarImagePath.isEmpty()) {
        QFile image(path.filePath(SidebarFile));
        if (!openPackageFile(image, ImageLimit) || !validPng(image, false)) return std::nullopt;
        theme->sidebarImagePath = image.fileName();
    }
    return theme;
}

bool hasOnlyPackageFiles(const QFileInfo &directory)
{
    if (!localDirectory(directory)) return false;
    const auto entries = QDir(directory.absoluteFilePath()).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const auto &entry : entries) {
        if (!entry.isFile() || hasLink(entry)
            || (entry.fileName() != ManifestFile && entry.fileName() != SidebarFile)) return false;
    }
    return true;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QUrl assetUrl(const QUrl &catalogUrl, const QString &id, const QString &filename)
{
    return catalogUrl.resolved(QUrl(QStringLiteral("themes/%1/%2").arg(id, filename)));
}

bool allowedSource(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment()) return false;
    if (url.scheme() == QStringLiteral("https")) return true;
    return url.scheme() == QStringLiteral("http")
        && (url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
            || QHostAddress(url.host()).isLoopback());
}
}

QUrl SkinLibrary::defaultCatalogUrl()
{
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/VelvetEvening/Orchestrate-skins/main/catalog.json"));
}

SkinLibrary::SkinLibrary(const QString &installRoot, QObject *parent, const QUrl &catalogUrl,
                         QNetworkAccessManager *network)
    : QObject(parent),
      installRoot_(installRoot.isEmpty() ? QString() : QDir(installRoot).absolutePath()),
      catalogUrl_(catalogUrl),
      network_(network)
{
}

SkinLibrary::~SkinLibrary()
{
    closeReply(true);
}

QList<SkinTheme> SkinLibrary::installedThemes() const
{
    QList<SkinTheme> themes;
    if (!localDirectory(QFileInfo(installRoot_))) return themes;
    const QDir root(installRoot_);
    for (const auto &directory : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        if (auto theme = readInstalled(directory)) themes.append(std::move(*theme));
    }
    return themes;
}

std::optional<SkinTheme> SkinLibrary::installedTheme(const QString &id) const
{
    if (!validId(id) || !localDirectory(QFileInfo(installRoot_))) return std::nullopt;
    return readInstalled(themeDirectory(QDir(installRoot_), id));
}

QList<SkinCatalogEntry> SkinLibrary::catalog() const
{
    return catalog_;
}

bool SkinLibrary::isBusy() const
{
    return request_ != Request::None;
}

void SkinLibrary::refreshCatalog()
{
    if (isBusy() || removing_) return;
    request_ = Request::Catalog;
    fetch(catalogUrl_, CatalogLimit);
}

void SkinLibrary::installTheme(const QString &id)
{
    if (isBusy() || removing_) return;
    const auto entry = std::find_if(catalog_.cbegin(), catalog_.cend(), [&](const auto &item) {
        return item.id == id;
    });
    if (entry == catalog_.cend()) {
        emit installFinished(id, QStringLiteral("皮肤不在已加载的目录中。"));
        return;
    }
    if (installedTheme(id)) {
        emit installFinished(id, {});
        return;
    }

    installId_ = id;
    installName_ = entry->name;
    request_ = Request::Manifest;
    fetch(assetUrl(catalogUrl_, id, ManifestFile), ManifestLimit);
}

void SkinLibrary::cancel()
{
    if (isBusy()) failRequest(QStringLiteral("已取消皮肤库请求。"));
}

bool SkinLibrary::removeTheme(const QString &id, QString *error,
                              const std::function<bool(QString *)> &saveSelection)
{
    if (error) error->clear();
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (request_ == Request::Manifest || request_ == Request::SidebarImage || removing_)
        return fail(QStringLiteral("请等当前皮肤操作完成后重试。"));
    if (!validId(id) || !localDirectory(QFileInfo(installRoot_)))
        return fail(QStringLiteral("未找到要删除的已安装皮肤。"));

    QDir root(installRoot_);
    const auto directory = themeDirectory(root, id);
    if (!hasOnlyPackageFiles(directory) || !readInstalled(directory))
        return fail(QStringLiteral("无法删除皮肤目录，请检查本地文件。"));

    {
        QScopedValueRollback<bool> removing(removing_, true);
        QTemporaryDir stage(root.filePath(QStringLiteral(".remove-XXXXXX")));
        if (!stage.isValid()) return fail(QStringLiteral("无法暂存要移除的皮肤。"));
        const auto previous = stage.filePath(id);
        if (!root.rename(directory.absoluteFilePath(), previous))
            return fail(QStringLiteral("无法移除皮肤，请检查文件是否正被使用。"));

        QString saveError;
        if (saveSelection && !saveSelection(&saveError)) {
            if (!root.rename(previous, directory.absoluteFilePath())) {
                stage.setAutoRemove(false);
                return fail(QStringLiteral("无法恢复皮肤目录，文件保存在 %1。").arg(previous));
            }
            return fail(saveError.isEmpty() ? QStringLiteral("无法保存选中的皮肤。") : saveError);
        }
    }
    emit installedChanged();
    return true;
}

void SkinLibrary::fetch(const QUrl &url, qint64 limit)
{
    if (!allowedSource(url)) {
        failRequest(QStringLiteral("皮肤库地址无效。"));
        return;
    }
    if (!network_) network_ = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Orchestrate/") + QCoreApplication::applicationVersion());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, false);
    auto *reply = network_->get(request);
    reply_ = reply;
    responseLimit_ = limit;
    reply->setReadBufferSize(64 * 1024);
    const auto checkSize = [this, reply] {
        if (reply_ == reply && exceedsLimit(reply)) failRequest(QStringLiteral("皮肤库文件过大。"));
    };
    connect(reply, &QNetworkReply::metaDataChanged, this, checkSize);
    connect(reply, &QNetworkReply::readyRead, this, checkSize);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { replyFinished(reply); });
    connect(reply, &QObject::destroyed, this, [this] {
        failRequest(QStringLiteral("皮肤库连接已关闭。"));
    });
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this, reply] {
        if (reply_ == reply) failRequest(QStringLiteral("连接皮肤库超时，请重试。"));
    });
    timeout->start(RequestTimeoutMs);
    if (reply->isFinished()) replyFinished(reply);
}

bool SkinLibrary::exceedsLimit(QNetworkReply *reply)
{
    if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > responseLimit_) return true;
    const auto available = qMin(reply->bytesAvailable(), responseLimit_ - response_.size() + 1);
    if (available > 0) response_.append(reply->read(available));
    return response_.size() > responseLimit_;
}

void SkinLibrary::replyFinished(QNetworkReply *reply)
{
    if (reply_ != reply) return;
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 || reply->error() != QNetworkReply::NoError) {
        failRequest(status != 0 && status != 200
            ? QStringLiteral("皮肤库请求失败（HTTP %1）。").arg(status)
            : QStringLiteral("皮肤库请求失败：%1").arg(reply->errorString()));
        return;
    }
    if (exceedsLimit(reply)) {
        failRequest(QStringLiteral("皮肤库文件过大。"));
        return;
    }
    closeReply(false);
    completeRequest(std::exchange(response_, {}), {});
}

void SkinLibrary::closeReply(bool abort)
{
    auto *reply = reply_.data();
    if (!reply) return;
    reply_.clear();
    disconnect(reply, nullptr, this, nullptr);
    if (abort && !reply->isFinished()) reply->abort();
    reply->deleteLater();
}

void SkinLibrary::failRequest(const QString &error)
{
    closeReply(true);
    response_.clear();
    completeRequest({}, error);
}

void SkinLibrary::completeRequest(const QByteArray &bytes, QString error)
{
    if (request_ == Request::Catalog) {
        if (error.isEmpty()) {
            if (auto next = parseCatalog(bytes)) catalog_ = std::move(*next);
            else error = QStringLiteral("皮肤目录格式无效。");
        }
        request_ = Request::None;
        emit catalogFinished(error);
        return;
    }
    if (request_ == Request::None) return;
    if (!error.isEmpty()) {
        completeInstall(error);
        return;
    }
    if (request_ == Request::Manifest) {
        const auto theme = parseManifest(bytes, installId_);
        if (!theme || theme->name != installName_) {
            completeInstall(QStringLiteral("皮肤配置无效或与目录不符。"));
            return;
        }
        manifest_ = bytes;
        if (theme->sidebarImagePath.isEmpty()) {
            completeInstall(publish({}));
        } else {
            request_ = Request::SidebarImage;
            fetch(assetUrl(catalogUrl_, installId_, SidebarFile), ImageLimit);
        }
        return;
    }

    QBuffer image;
    image.setData(bytes);
    image.open(QIODevice::ReadOnly);
    if (!validPng(image, true)) {
        completeInstall(QStringLiteral("皮肤侧栏图片无效。"));
        return;
    }
    completeInstall(publish(bytes));
}

void SkinLibrary::completeInstall(const QString &error)
{
    const auto id = std::exchange(installId_, {});
    installName_.clear();
    manifest_.clear();
    request_ = Request::None;
    const QPointer<SkinLibrary> self(this);
    if (error.isEmpty()) emit installedChanged();
    if (self) emit installFinished(id, error);
}

QString SkinLibrary::publish(const QByteArray &sidebarImage)
{
    if (installRoot_.isEmpty() || hasLink(QFileInfo(installRoot_)) || !QDir().mkpath(installRoot_)
        || !localDirectory(QFileInfo(installRoot_))) return QStringLiteral("无法创建本地皮肤目录。");

    QDir root(installRoot_);
    const auto target = root.filePath(installId_);
    const auto existing = themeDirectory(root, installId_);
    if (readInstalled(existing)) return {};
    const QFileInfo targetInfo(target);
    const bool needsReplacement = targetInfo.exists() || hasLink(targetInfo);
    if (needsReplacement && !hasOnlyPackageFiles(existing))
        return QStringLiteral("本地皮肤目录存在冲突。");

    QTemporaryDir stage(root.filePath(QStringLiteral(".install-XXXXXX")));
    if (!stage.isValid()) return QStringLiteral("无法暂存要安装的皮肤。");
    const auto package = stage.filePath(QStringLiteral("package"));
    const QDir files(package);
    if (!QDir().mkpath(package) || !writeFile(files.filePath(ManifestFile), manifest_)
        || (!sidebarImage.isEmpty() && !writeFile(files.filePath(SidebarFile), sidebarImage)))
        return QStringLiteral("无法写入本地皮肤文件。");

    // Publish a complete directory. A valid previous installation is never replaced.
    const auto previous = stage.filePath(QStringLiteral("previous"));
    if (needsReplacement && !root.rename(target, previous))
        return QStringLiteral("无法移除损坏的皮肤目录。");
    if (root.rename(package, target)) return {};
    if (needsReplacement && !root.rename(previous, target)) {
        stage.setAutoRemove(false);
        return QStringLiteral("无法恢复皮肤目录，文件保存在 %1。").arg(previous);
    }
    return QStringLiteral("无法安装皮肤文件。");
}
