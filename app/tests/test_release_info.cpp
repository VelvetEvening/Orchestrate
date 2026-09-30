#include "update/releaseinfo.h"
#include "update/updatewidget.h"

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ReleaseInfoTest : public QObject {
    Q_OBJECT
private:
    QJsonObject release() const
    {
        const QString url = QStringLiteral("https://github.com/VelvetEvening/Orchestrate/releases/download/v1.1.0/");
        const QString name = QStringLiteral("Orchestrate-v1.1.0-windows-x64.zip");
        return {{QStringLiteral("tag_name"), QStringLiteral("v1.1.0")},
                {QStringLiteral("draft"), false}, {QStringLiteral("prerelease"), false},
                {QStringLiteral("assets"), QJsonArray{QJsonObject{
                    {QStringLiteral("name"), name}, {QStringLiteral("browser_download_url"), url + name},
                    {QStringLiteral("size"), 12345}, {QStringLiteral("digest"), QStringLiteral("sha256:") + QString(64, QLatin1Char('a'))}}}}};
    }

private slots:
    void officialRelease()
    {
        ReleaseInfo info;
        QString error;
        QVERIFY2(ReleaseInfo::parse(release(), &info, &error), qPrintable(error));
        QCOMPARE(info.tag, QStringLiteral("v1.1.0"));
        QCOMPARE(info.digest, QByteArray(64, 'a'));
        QVERIFY(QVersionNumber::compare(info.version, QVersionNumber(1, 0, 0)) > 0);
        QVERIFY(QVersionNumber::compare(QVersionNumber(1, 10, 0), info.version) > 0);
    }

    void rejectInvalidRelease()
    {
        ReleaseInfo info;
        QString error;
        for (const auto &key : {QStringLiteral("draft"), QStringLiteral("prerelease")}) {
            auto object = release(); object[key] = true;
            QVERIFY(!ReleaseInfo::parse(object, &info, &error));
        }
        for (const auto &tag : {QStringLiteral("v1.1.0-beta"), QStringLiteral("v1.1"), QStringLiteral("../v1.1.0")}) {
            auto object = release(); object[QStringLiteral("tag_name")] = tag;
            QVERIFY(!ReleaseInfo::parse(object, &info, &error));
        }
        auto object = release();
        auto assets = object.value(QStringLiteral("assets")).toArray();
        assets.append(assets.first()); object[QStringLiteral("assets")] = assets;
        QVERIFY(!ReleaseInfo::parse(object, &info, &error));
        auto asset = assets.first().toObject();
        for (const auto &url : {QStringLiteral("http://github.com/VelvetEvening/Orchestrate/releases/download/v1.1.0/a.zip"),
                               QStringLiteral("https://example.com/a.zip"),
                               QStringLiteral("https://github.com/other/repo/releases/download/v1.1.0/a.zip")}) {
            asset[QStringLiteral("browser_download_url")] = url;
            object[QStringLiteral("assets")] = QJsonArray{asset};
            QVERIFY(!ReleaseInfo::parse(object, &info, &error));
        }
    }

    void checksumFallback()
    {
        auto object = release();
        auto assets = object.value(QStringLiteral("assets")).toArray();
        auto asset = assets.first().toObject(); asset.remove(QStringLiteral("digest"));
        object[QStringLiteral("assets")] = QJsonArray{asset};
        ReleaseInfo info;
        QString error;
        QVERIFY(!ReleaseInfo::parse(object, &info, &error));
        QJsonObject checksum {{QStringLiteral("name"), asset.value(QStringLiteral("name")).toString() + QStringLiteral(".sha256")},
                              {QStringLiteral("browser_download_url"), asset.value(QStringLiteral("browser_download_url")).toString() + QStringLiteral(".sha256")}};
        object[QStringLiteral("assets")] = QJsonArray{asset, checksum};
        QVERIFY2(ReleaseInfo::parse(object, &info, &error), qPrintable(error));
        QByteArray digest;
        const QByteArray content = QByteArray(64, 'B') + "  " + info.assetName.toUtf8() + "\n";
        QVERIFY(ReleaseInfo::parseChecksum(content, info.assetName, &digest, &error));
        QCOMPARE(digest, QByteArray(64, 'b'));
        QVERIFY(!ReleaseInfo::parseChecksum(content, QStringLiteral("wrong.zip"), &digest, &error));
        QVERIFY(!ReleaseInfo::parseChecksum(content + "other\n", info.assetName, &digest, &error));
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral("1.1.0"));
    if (app.arguments().contains(QStringLiteral("--live-check"))) {
        UpdateWidget widget;
        widget.resize(650, 400);
        widget.show();
        auto *button = widget.findChild<QPushButton *>(QStringLiteral("checkUpdatesButton"));
        auto *status = widget.findChild<QLabel *>(QStringLiteral("updateStatus"));
        button->click();
        QElapsedTimer timer; timer.start();
        while (!button->isEnabled() && timer.elapsed() < 70000) QTest::qWait(100);
        QTextStream(stdout) << status->text() << Qt::endl;
        return status->text().contains(QStringLiteral("当前已是最新版本")) ? 0 : 1;
    }
    ReleaseInfoTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_release_info.moc"
