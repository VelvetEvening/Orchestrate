#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVersionNumber>

struct ReleaseInfo {
    QString tag;
    QString notes;
    QString assetName;
    QUrl pageUrl;
    QUrl archiveUrl;
    QUrl checksumUrl;
    QByteArray digest;
    qint64 archiveBytes = 0;
    QVersionNumber version;

    static bool parse(const QJsonObject &object, ReleaseInfo *result, QString *error);
    static bool parseChecksum(const QByteArray &text, const QString &assetName,
                              QByteArray *digest, QString *error);
};
