#include "releaseinfo.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool validAssetUrl(const QUrl &url)
{
    return url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("github.com")
        && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443)
        && url.path().startsWith(QStringLiteral("/VelvetEvening/Orchestrate/releases/download/"));
}
}

bool ReleaseInfo::parse(const QJsonObject &object, ReleaseInfo *result, QString *error)
{
    ReleaseInfo info;
    if (object.value(QStringLiteral("draft")).toBool() || object.value(QStringLiteral("prerelease")).toBool())
        return fail(error, QStringLiteral("该 Release 不是正式发布版本。"));
    info.tag = object.value(QStringLiteral("tag_name")).toString();
    if (!QRegularExpression(QStringLiteral("^v[0-9]+\\.[0-9]+\\.[0-9]+$")).match(info.tag).hasMatch())
        return fail(error, QStringLiteral("Release 版本号格式无效。"));
    info.version = QVersionNumber::fromString(info.tag.mid(1));
    info.assetName = QStringLiteral("Orchestrate-%1-windows-x64.zip").arg(info.tag);
    info.notes = object.value(QStringLiteral("body")).toString();
    info.pageUrl = QUrl(QStringLiteral("https://github.com/VelvetEvening/Orchestrate/releases/tag/") + info.tag);
    int archives = 0;
    int checksums = 0;
    for (const auto &value : object.value(QStringLiteral("assets")).toArray()) {
        const auto asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (name == info.assetName) {
            ++archives;
            info.archiveUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
            info.archiveBytes = asset.value(QStringLiteral("size")).toInteger();
            const QString digest = asset.value(QStringLiteral("digest")).toString();
            if (QRegularExpression(QStringLiteral("^sha256:[0-9a-fA-F]{64}$")).match(digest).hasMatch())
                info.digest = digest.mid(7).toLatin1().toLower();
        } else if (name == info.assetName + QStringLiteral(".sha256")) {
            ++checksums;
            info.checksumUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        }
    }
    if (archives != 1 || !validAssetUrl(info.archiveUrl) || info.archiveBytes <= 0
        || info.archiveBytes > 512LL * 1024 * 1024)
        return fail(error, QStringLiteral("Release 缺少有效的 Windows x64 ZIP。"));
    if (info.digest.isEmpty() && (checksums != 1 || !validAssetUrl(info.checksumUrl)))
        return fail(error, QStringLiteral("Release 缺少 SHA256 校验信息。"));
    *result = info;
    return true;
}

bool ReleaseInfo::parseChecksum(const QByteArray &text, const QString &assetName,
                                QByteArray *digest, QString *error)
{
    const auto match = QRegularExpression(QStringLiteral("^([0-9a-fA-F]{64})  ([^\\r\\n]+)\\r?\\n?$")).match(QString::fromUtf8(text));
    if (!match.hasMatch() || match.captured(2) != assetName)
        return fail(error, QStringLiteral("SHA256 文件格式或 ZIP 文件名不匹配。"));
    *digest = match.captured(1).toLatin1().toLower();
    return true;
}
