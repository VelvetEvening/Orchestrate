#include "test_skin_fixture.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
using namespace TestSkinFixture;

const QUrl CatalogUrl(QStringLiteral("https://skins.example.test/library/catalog.json"));
const auto CatalogPath = CatalogUrl.path();
const auto ManifestPath = QStringLiteral("/library/themes/") + Id + QStringLiteral("/theme.json");
const auto SidebarPath = QStringLiteral("/library/themes/") + Id + QStringLiteral("/sidebar.png");

bool fetchCatalog(SkinLibrary &library)
{
    QSignalSpy finished(&library, &SkinLibrary::catalogFinished);
    library.refreshCatalog();
    return (finished.count() || finished.wait()) && finished.first().first().toString().isEmpty();
}
}

class SkinLibraryTest : public QObject
{
    Q_OBJECT
private slots:
    void interruptedAndCancelledDownloadsAreAtomicAndCanRetryOffline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir root(dir.path());
        QVERIFY(writeFile(root.filePath(QStringLiteral("keep/theme.json")), json(manifest(false, QStringLiteral("keep")))));
        const auto image = sidebarImage();
        QVERIFY(!image.isEmpty());
        auto config = manifest(true);
        auto colors = config.value(QStringLiteral("colors")).toObject();
        colors.insert(QStringLiteral("futureInk"), QStringLiteral("#ffffff"));
        config.insert(QStringLiteral("colors"), colors);

        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {json(config)}},
                             {SidebarPath, {image.left(48), QNetworkReply::RemoteHostClosedError}}};
        SkinLibrary library(root.path(), nullptr, CatalogUrl, &network);
        QSignalSpy finished(&library, &SkinLibrary::installFinished);
        QSignalSpy changed(&library, &SkinLibrary::installedChanged);
        QVERIFY(!library.installedTheme(Id));
        QVERIFY(network.requests.isEmpty());
        QVERIFY(fetchCatalog(library));
        QCOMPARE(network.requests, QList<QUrl>{CatalogUrl});

        library.installTheme(Id);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.last().at(1).toString().isEmpty());
        QCOMPARE(network.requests, (QList<QUrl>{CatalogUrl, CatalogUrl.resolved(QUrl(ManifestPath)),
                                                 CatalogUrl.resolved(QUrl(SidebarPath))}));
        QVERIFY(!root.exists(Id));
        QCOMPARE(changed.count(), 0);

        network.responses[SidebarPath] = {image.left(48), QNetworkReply::NoError, 200, true};
        library.installTheme(Id);
        library.installTheme(Id);
        QTRY_COMPARE(network.requests.size(), 5);
        QVERIFY(library.isBusy() && !root.exists(Id));
        library.cancel();
        QCOMPARE(finished.count(), 2);
        QVERIFY(!finished.last().at(1).toString().isEmpty());
        QCOMPARE(changed.count(), 0);

        QVERIFY(writeFile(root.filePath(Id + QStringLiteral("/theme.json")), "{"));
        network.responses[SidebarPath] = {image};
        library.installTheme(Id);
        QTRY_COMPARE(finished.count(), 3);
        QVERIFY(finished.last().at(1).toString().isEmpty());
        QCOMPARE(changed.count(), 1);
        QVERIFY(library.installedTheme(QStringLiteral("keep")));
        QCOMPARE(root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 (QStringList{QStringLiteral("keep"), Id}));

        Network offline;
        SkinLibrary reopened(root.path(), nullptr, CatalogUrl, &offline);
        const auto installed = reopened.installedTheme(Id);
        QVERIFY(installed);
        QCOMPARE(installed->name, Name);
        QCOMPARE(installed->base, QStringLiteral("dark"));
        QCOMPARE(installed->colors.value(QStringLiteral("futureInk")), QStringLiteral("#ffffff"));
        QFile file(installed->sidebarImagePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), image);
        QVERIFY(offline.requests.isEmpty());
    }

    void rejectedCatalogsPreserveLastSuccessAndInstalledData()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid() && writeInstalled(dir.path()));
        Network network;
        network.responses[CatalogPath] = {catalog()};
        SkinLibrary library(dir.path(), nullptr, CatalogUrl, &network);
        QVERIFY(fetchCatalog(library));

        const QList<Response> failures{
            {"{"},
            {catalog({catalogEntry(QStringLiteral("../keep"))})},
            {catalog({catalogEntry(QStringLiteral("precision"))})},
            {catalog({catalogEntry(QStringLiteral("com1"))})},
            {catalog({catalogEntry(Id, QStringLiteral("<b>标题</b>"))})},
            {catalog({catalogEntry(Id, QStringLiteral("标题\n第二行"))})},
            {catalog({catalogEntry(), catalogEntry()})},
            {catalog() + QByteArray(256 * 1024, ' ')},
            {{}, QNetworkReply::NoError, 302},
            {{}, QNetworkReply::ConnectionRefusedError, 0}
        };
        QSignalSpy finished(&library, &SkinLibrary::catalogFinished);
        for (const auto &response : failures) {
            network.responses[CatalogPath] = response;
            library.refreshCatalog();
            QVERIFY(finished.wait());
            QVERIFY(!finished.takeFirst().first().toString().isEmpty());
        }
        QCOMPARE(library.catalog().size(), 1);
        QCOMPARE(library.catalog().first().id, Id);
        QVERIFY(library.installedTheme(Id));
    }

    void failedPublishKeepsUnrelatedFilesAndCanRetryWithoutAnImage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto unrelatedPath = dir.filePath(Id + QStringLiteral("/other.txt"));
        QVERIFY(writeFile(unrelatedPath, "keep"));
        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {json(manifest())}}};
        SkinLibrary library(dir.path(), nullptr, CatalogUrl, &network);
        QVERIFY(fetchCatalog(library));
        QSignalSpy finished(&library, &SkinLibrary::installFinished);

        library.installTheme(Id);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.last().at(1).toString().isEmpty());
        QVERIFY(!library.installedTheme(Id));
        QFile unrelated(unrelatedPath);
        QVERIFY(unrelated.open(QIODevice::ReadOnly));
        QCOMPARE(unrelated.readAll(), QByteArray("keep"));
        unrelated.close();
        QVERIFY(unrelated.remove());

        library.installTheme(Id);
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(finished.last().at(1).toString().isEmpty());
        QCOMPARE(network.requests.size(), 3);
        const auto installed = library.installedTheme(Id);
        QVERIFY(installed && installed->sidebarImagePath.isEmpty());
    }

    void rejectsBrokenPackages_data()
    {
        QTest::addColumn<QByteArray>("configuration");
        QTest::addColumn<QByteArray>("artwork");
        QTest::addColumn<qint64>("declaredLength");
        auto badField = [](const char *label, const QString &key, const QJsonValue &value) {
            auto config = manifest();
            config.insert(key, value);
            QTest::newRow(label) << json(config) << QByteArray() << qint64(-1);
        };
        badField("wrong-id", "id", QStringLiteral("other"));
        badField("wrong-name", "name", QStringLiteral("其他名字"));
        badField("wrong-base", "base", QStringLiteral("unknown"));
        badField("wrong-version", "schemaVersion", 2);
        badField("invalid-color", "colors", QJsonObject{{"accent", "#aabbcc; url(foo)"}});
        badField("image-path", "sidebarImage", QStringLiteral("../sidebar.png"));
        QTest::newRow("large-manifest") << (json(manifest()) + QByteArray(32 * 1024, ' '))
                                       << QByteArray() << qint64(-1);

        const auto config = json(manifest(true));
        const auto validImage = sidebarImage();
        QTest::newRow("wrong-format") << config << QByteArray("<svg/>") << qint64(-1);
        QTest::newRow("truncated-png") << config << validImage.chopped(4) << qint64(-1);
        QTest::newRow("oversize-axis") << config << sidebarImage(QSize(4097, 1)) << qint64(-1);
        QTest::newRow("oversize-pixels") << config << sidebarImage(QSize(2049, 2048)) << qint64(-1);
        QTest::newRow("oversize-header") << config << validImage << qint64(4 * 1024 * 1024 + 1);
        QTest::newRow("oversize-body") << config << (validImage + QByteArray(4 * 1024 * 1024, ' ')) << qint64(-1);
    }

    void rejectsBrokenPackages()
    {
        QFETCH(QByteArray, configuration);
        QFETCH(QByteArray, artwork);
        QFETCH(qint64, declaredLength);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = dir.filePath(QStringLiteral("skins"));
        Network network;
        network.responses = {{CatalogPath, {catalog()}}, {ManifestPath, {configuration}},
                             {SidebarPath, {artwork, QNetworkReply::NoError, 200, false, declaredLength}}};
        SkinLibrary library(root, nullptr, CatalogUrl, &network);
        QVERIFY(fetchCatalog(library));
        QSignalSpy finished(&library, &SkinLibrary::installFinished);
        library.installTheme(Id);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.last().at(1).toString().isEmpty());
        QCOMPARE(network.requests.size(), artwork.isEmpty() ? 2 : 3);
        QVERIFY(library.installedThemes().isEmpty() && !QFileInfo::exists(root));
    }

    void ignoresInvalidLocalDirectoriesAndRefusesTraversal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = dir.filePath(QStringLiteral("skins"));
        const auto outside = dir.filePath(QStringLiteral("outside/theme.json"));
        QVERIFY(writeInstalled(root));
        QVERIFY(writeFile(outside, json(manifest(false, QStringLiteral("outside")))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("skins/wrong-id/theme.json")), json(manifest())));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("skins/mist/theme.json")), json(manifest(false, QStringLiteral("mist")))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("skins/lost-image/theme.json")), json(manifest(true, QStringLiteral("lost-image")))));
        SkinLibrary library(root);
        QCOMPARE(library.installedThemes().size(), 1);
        QVERIFY(library.installedTheme(Id));
        QVERIFY(!library.removeTheme(QStringLiteral("../outside")));
        QVERIFY(QFileInfo::exists(outside));
    }
};

QTEST_GUILESS_MAIN(SkinLibraryTest)
#include "test_skin_library.moc"
