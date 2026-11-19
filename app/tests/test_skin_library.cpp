#include "ui/skinlibrary.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <cstring>

namespace {
const QUrl CatalogUrl(QStringLiteral("https://skins.example.test/library/catalog.json"));
const auto CatalogPath = QStringLiteral("/library/catalog.json");
const auto ManifestPath = QStringLiteral("/library/themes/mamba/theme.json");
const auto SidebarPath = QStringLiteral("/library/themes/mamba/sidebar.png");
const auto ThemeName = QStringLiteral("曼巴紫 × 冠军金");

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject catalogEntry(const QString &id, const QString &name = ThemeName)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}};
}

QByteArray catalog(const QJsonArray &themes = QJsonArray{catalogEntry(QStringLiteral("mamba"))})
{
    return json({{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("themes"), themes}});
}

QJsonObject manifest(bool sidebar = false, const QString &id = QStringLiteral("mamba"))
{
    auto object = catalogEntry(id);
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("base"), QStringLiteral("dark"));
    object.insert(QStringLiteral("colors"), QJsonObject{
        {QStringLiteral("accent"), QStringLiteral("#d8ad52")},
        {QStringLiteral("futureInk"), QStringLiteral("#ffffff")}
    });
    if (sidebar) object.insert(QStringLiteral("sidebarImage"), QStringLiteral("sidebar.png"));
    return object;
}

QByteArray png(QSize size = QSize(32, 64))
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::darkMagenta);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    return image.save(&buffer, "png") ? buffer.data() : QByteArray{};
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

struct Response {
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    int status = 200;
    bool pause = false;
    qint64 contentLength = -1;
};

class Reply final : public QNetworkReply
{
public:
    Reply(const QNetworkRequest &request, const Response &response, QObject *parent)
        : QNetworkReply(parent), response_(response)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response_.status);
        if (response_.contentLength >= 0)
            setHeader(QNetworkRequest::ContentLengthHeader, response_.contentLength);
        setOpenMode(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            if (isFinished()) return;
            emit metaDataChanged();
            if (isFinished()) return;
            available_ = true;
            emit readyRead();
            if (isFinished() || response_.pause) return;
            complete(response_.error);
        });
    }

    void abort() override
    {
        if (!isFinished()) complete(OperationCanceledError);
    }

    qint64 bytesAvailable() const override
    {
        return (available_ ? response_.body.size() - offset_ : 0) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *target, qint64 maximum) override
    {
        const auto count = available_ ? qMin<qint64>(maximum, response_.body.size() - offset_) : 0;
        if (!count) return isFinished() ? -1 : 0;
        std::memcpy(target, response_.body.constData() + offset_, count);
        offset_ += count;
        return count;
    }

private:
    void complete(NetworkError error)
    {
        if (error != NoError) setError(error, QStringLiteral("模拟连接中断"));
        setFinished(true);
        emit finished();
    }

    Response response_;
    bool available_ = false;
    qint64 offset_ = 0;
};

class Network final : public QNetworkAccessManager
{
public:
    QHash<QString, Response> responses;
    QList<QUrl> requests;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        Q_ASSERT(operation == GetOperation);
        Q_ASSERT(!outgoingData);
        requests.append(request.url());
        return new Reply(request, responses.value(request.url().path(), {{}, QNetworkReply::ContentNotFoundError, 404}), this);
    }
};
}

class SkinLibraryTest : public QObject
{
    Q_OBJECT
private slots:
    void downloadsOnlyOnDemandAndReopensOffline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = dir.filePath(QStringLiteral("skins"));
        const auto image = png();
        QVERIFY(!image.isEmpty());

        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {json(manifest(true))}}, {SidebarPath, {image}}};
        SkinLibrary library(root, nullptr, CatalogUrl, &network);
        QSignalSpy catalogFinished(&library, &SkinLibrary::catalogFinished);
        QSignalSpy installFinished(&library, &SkinLibrary::installFinished);
        QSignalSpy changed(&library, &SkinLibrary::installedChanged);
        QVERIFY(library.installedThemes().isEmpty());
        QVERIFY(library.catalog().isEmpty());
        QVERIFY(network.requests.isEmpty());
        QVERIFY(!QFileInfo::exists(root));

        library.refreshCatalog();
        library.refreshCatalog();
        QTRY_COMPARE(catalogFinished.count(), 1);
        QVERIFY(catalogFinished.last().first().toString().isEmpty());
        QCOMPARE(network.requests, QList<QUrl>{CatalogUrl});
        QCOMPARE(library.catalog().size(), 1);
        QCOMPARE(library.catalog().first().name, ThemeName);

        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(installFinished.count(), 1);
        QVERIFY(installFinished.last().at(1).toString().isEmpty());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(network.requests, (QList<QUrl>{CatalogUrl, CatalogUrl.resolved(QUrl(ManifestPath)),
                                                  CatalogUrl.resolved(QUrl(SidebarPath))}));
        QVERIFY(!library.isBusy());

        Network offline;
        SkinLibrary reopened(root, nullptr, CatalogUrl, &offline);
        QCOMPARE(reopened.installedThemes().size(), 1);
        const auto installed = reopened.installedTheme(QStringLiteral("mamba"));
        QVERIFY(installed);
        QCOMPARE(installed->name, ThemeName);
        QCOMPARE(installed->base, QStringLiteral("dark"));
        QCOMPARE(installed->colors.value(QStringLiteral("accent")), QStringLiteral("#d8ad52"));
        QCOMPARE(installed->colors.value(QStringLiteral("futureInk")), QStringLiteral("#ffffff"));
        QCOMPARE(installed->sidebarImagePath, QDir(root).filePath(QStringLiteral("mamba/sidebar.png")));
        QFile file(installed->sidebarImagePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), image);
        QVERIFY(offline.requests.isEmpty());
    }

    void interruptionAndCancellationLeaveNothingPublishedAndCanRetry()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = QDir(dir.path());
        const auto existing = root.filePath(QStringLiteral("keep/theme.json"));
        QVERIFY(writeFile(existing, json(manifest(false, QStringLiteral("keep")))));
        const auto image = png();
        QVERIFY(!image.isEmpty());

        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {json(manifest(true))}},
                             {SidebarPath, {image.left(48), QNetworkReply::RemoteHostClosedError}}};
        SkinLibrary library(root.path(), nullptr, CatalogUrl, &network);
        QSignalSpy catalogFinished(&library, &SkinLibrary::catalogFinished);
        QSignalSpy installFinished(&library, &SkinLibrary::installFinished);
        QSignalSpy changed(&library, &SkinLibrary::installedChanged);
        library.refreshCatalog();
        QTRY_COMPARE(catalogFinished.count(), 1);

        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(installFinished.count(), 1);
        QVERIFY(!installFinished.last().at(1).toString().isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("mamba"))));
        QCOMPARE(changed.count(), 0);

        network.responses[SidebarPath] = {image.left(48), QNetworkReply::NoError, 200, true};
        library.installTheme(QStringLiteral("mamba"));
        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(network.requests.size(), 5);
        QVERIFY(library.isBusy());
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("mamba"))));
        library.cancel();
        QCOMPARE(installFinished.count(), 2);
        QVERIFY(!installFinished.last().at(1).toString().isEmpty());
        QCOMPARE(changed.count(), 0);

        const auto corrupt = root.filePath(QStringLiteral("mamba/theme.json"));
        QVERIFY(writeFile(corrupt, "{"));
        QVERIFY(!library.installedTheme(QStringLiteral("mamba")));
        network.responses[SidebarPath] = {image};
        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(installFinished.count(), 3);
        QVERIFY(installFinished.last().at(1).toString().isEmpty());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(library.installedThemes().size(), 2);
        QVERIFY(library.installedTheme(QStringLiteral("keep")));
        QCOMPARE(root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 (QStringList{QStringLiteral("keep"), QStringLiteral("mamba")}));
    }

    void rejectedCatalogsPreserveLastSuccessAndInstalledData()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("keep/theme.json")), json(manifest(false, QStringLiteral("keep")))));

        Network network;
        network.responses[CatalogPath] = {catalog()};
        SkinLibrary library(dir.path(), nullptr, CatalogUrl, &network);
        QSignalSpy done(&library, &SkinLibrary::catalogFinished);
        library.refreshCatalog();
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(done.last().first().toString().isEmpty());

        const QList<Response> failures{
            {"{"},
            {catalog({catalogEntry(QStringLiteral("../keep"))})},
            {catalog({catalogEntry(QStringLiteral("mist"))})},
            {catalog({catalogEntry(QStringLiteral("paper"))})},
            {catalog({catalogEntry(QStringLiteral("precision"))})},
            {catalog({catalogEntry(QStringLiteral("com1"))})},
            {catalog({catalogEntry(QStringLiteral("other"), QStringLiteral("<b>标题</b>"))})},
            {catalog({catalogEntry(QStringLiteral("other"), QStringLiteral("标题\n第二行"))})},
            {catalog({catalogEntry(QStringLiteral("mamba")), catalogEntry(QStringLiteral("mamba"))})},
            {catalog() + QByteArray(256 * 1024, ' ')},
            {{}, QNetworkReply::NoError, 302},
            {{}, QNetworkReply::ConnectionRefusedError, 0}
        };
        int count = 1;
        for (const auto &response : failures) {
            network.responses[CatalogPath] = response;
            library.refreshCatalog();
            ++count;
            QTRY_COMPARE(done.count(), count);
            QVERIFY(!done.last().first().toString().isEmpty());
            QCOMPARE(library.catalog().size(), 1);
            QCOMPARE(library.catalog().first().id, QStringLiteral("mamba"));
            QVERIFY(library.installedTheme(QStringLiteral("keep")));
        }
        QCOMPARE(network.requests.size(), count);
        QVERIFY(!library.isBusy());
    }

    void failedPublishPreservesUnrelatedFilesAndCanRetryWithoutAnImage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto unrelatedPath = dir.filePath(QStringLiteral("mamba/other.txt"));
        QVERIFY(writeFile(unrelatedPath, "keep"));
        const auto bytes = json(manifest());

        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {bytes}}};
        SkinLibrary library(dir.path(), nullptr, CatalogUrl, &network);
        QSignalSpy ready(&library, &SkinLibrary::catalogFinished);
        QSignalSpy done(&library, &SkinLibrary::installFinished);
        library.refreshCatalog();
        QTRY_COMPARE(ready.count(), 1);

        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(!done.last().at(1).toString().isEmpty());
        QVERIFY(!library.installedTheme(QStringLiteral("mamba")));
        QFile unrelated(unrelatedPath);
        QVERIFY(unrelated.open(QIODevice::ReadOnly));
        QCOMPARE(unrelated.readAll(), QByteArray("keep"));
        unrelated.close();
        QVERIFY(unrelated.remove());

        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(done.count(), 2);
        QVERIFY(done.last().at(1).toString().isEmpty());
        QCOMPARE(network.requests.size(), 3);
        const auto installed = library.installedTheme(QStringLiteral("mamba"));
        QVERIFY(installed);
        QVERIFY(installed->sidebarImagePath.isEmpty());
    }

    void rejectsMalformedManifest_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        auto object = manifest();
        object.insert(QStringLiteral("id"), QStringLiteral("other"));
        QTest::newRow("wrong-id") << json(object);
        object = manifest();
        object.insert(QStringLiteral("name"), QStringLiteral("不同的名字"));
        QTest::newRow("wrong-name") << json(object);
        object = manifest();
        object.insert(QStringLiteral("base"), QStringLiteral("unknown"));
        QTest::newRow("wrong-base") << json(object);
        object = manifest();
        object.insert(QStringLiteral("schemaVersion"), 2);
        QTest::newRow("wrong-version") << json(object);
        object = manifest();
        object.insert(QStringLiteral("colors"), QJsonObject{{QStringLiteral("accent"), QStringLiteral("#aabbcc; url(foo)")}});
        QTest::newRow("invalid-color") << json(object);
        object = manifest();
        object.insert(QStringLiteral("sidebarImage"), QStringLiteral("../sidebar.png"));
        QTest::newRow("image-path") << json(object);
        QTest::newRow("large-manifest") << (json(manifest()) + QByteArray(32 * 1024, ' '));
    }

    void rejectsMalformedManifest()
    {
        QFETCH(QByteArray, bytes);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {bytes}}};
        SkinLibrary library(dir.filePath(QStringLiteral("skins")), nullptr, CatalogUrl, &network);
        QSignalSpy ready(&library, &SkinLibrary::catalogFinished);
        QSignalSpy done(&library, &SkinLibrary::installFinished);
        library.refreshCatalog();
        QTRY_COMPARE(ready.count(), 1);
        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(!done.last().at(1).toString().isEmpty());
        QCOMPARE(network.requests.size(), 2);
        QVERIFY(library.installedThemes().isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("skins"))));
    }

    void rejectsInvalidImage_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<qint64>("declaredLength");
        const auto image = png();
        QTest::newRow("wrong-format") << QByteArray("<svg/>") << qint64(-1);
        QTest::newRow("truncated") << image.chopped(4) << qint64(-1);
        QTest::newRow("oversize-axis") << png(QSize(4097, 1)) << qint64(-1);
        QTest::newRow("oversize-pixels") << png(QSize(2049, 2048)) << qint64(-1);
        QTest::newRow("oversize-header") << image << qint64(4 * 1024 * 1024 + 1);
        QTest::newRow("oversize-body") << (image + QByteArray(4 * 1024 * 1024, ' ')) << qint64(-1);
    }

    void rejectsInvalidImage()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(qint64, declaredLength);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {json(manifest(true))}},
                             {SidebarPath, {bytes, QNetworkReply::NoError, 200, false, declaredLength}}};
        SkinLibrary library(dir.path(), nullptr, CatalogUrl, &network);
        QSignalSpy ready(&library, &SkinLibrary::catalogFinished);
        QSignalSpy done(&library, &SkinLibrary::installFinished);
        library.refreshCatalog();
        QTRY_COMPARE(ready.count(), 1);
        library.installTheme(QStringLiteral("mamba"));
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(!done.last().at(1).toString().isEmpty());
        QVERIFY(library.installedThemes().isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("mamba"))));
    }

    void removalCanRestoreSelectionAndWorksDuringRefresh()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = dir.filePath(QStringLiteral("skins"));
        const auto path = dir.filePath(QStringLiteral("skins/mamba/theme.json"));
        const auto outside = dir.filePath(QStringLiteral("outside/theme.json"));
        QVERIFY(writeFile(path, json(manifest())));
        QVERIFY(writeFile(outside, json(manifest(false, QStringLiteral("outside")))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("skins/other/theme.json")), json(manifest())));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("skins/mist/theme.json")), json(manifest(false, QStringLiteral("mist")))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("skins/lost-image/theme.json")), json(manifest(true, QStringLiteral("lost-image")))));

        Network network;
        network.responses[CatalogPath] = {{}, QNetworkReply::NoError, 200, true};
        SkinLibrary library(root, nullptr, CatalogUrl, &network);
        QSignalSpy changed(&library, &SkinLibrary::installedChanged);
        QCOMPARE(library.installedThemes().size(), 1);
        QString error;
        QVERIFY(!library.removeTheme(QStringLiteral("../outside"), &error));
        QVERIFY(QFileInfo::exists(outside));

        bool hiddenWhileSaving = false;
        QVERIFY(!library.removeTheme(QStringLiteral("mamba"), &error, [&](QString *saveError) {
            hiddenWhileSaving = !QFileInfo::exists(path);
            *saveError = QStringLiteral("模拟数据库写入失败");
            return false;
        }));
        QVERIFY(hiddenWhileSaving);
        QCOMPARE(error, QStringLiteral("模拟数据库写入失败"));
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(library.installedTheme(QStringLiteral("mamba")));
        QCOMPARE(changed.count(), 0);

        library.refreshCatalog();
        QVERIFY(library.isBusy());
        bool saved = false;
        QVERIFY(library.removeTheme(QStringLiteral("mamba"), &error, [&](QString *) {
            saved = !QFileInfo::exists(path);
            return true;
        }));
        QVERIFY(saved);
        QVERIFY(error.isEmpty());
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(network.requests.size(), 1);
        library.cancel();
        QVERIFY(library.installedThemes().isEmpty());
    }
};

QTEST_GUILESS_MAIN(SkinLibraryTest)
#include "test_skin_library.moc"
