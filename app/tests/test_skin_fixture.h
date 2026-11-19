#pragma once

#include "ui/skinlibrary.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QTimer>
#include <cstring>

namespace TestSkinFixture {

inline const QString Id = QStringLiteral("test-night");
inline const QString Name = QStringLiteral("夜蓝 × 琥珀");
inline const QString Accent = QStringLiteral("#d8ad52");

inline SkinTheme theme()
{
    return {Id, Name, QStringLiteral("dark"), {{QStringLiteral("accent"), Accent}}, {}};
}

inline QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

inline QJsonObject catalogEntry(const QString &id = Id, const QString &name = Name)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}};
}

inline QByteArray catalog(const QJsonArray &entries = QJsonArray{catalogEntry()})
{
    return json({{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("themes"), entries}});
}

inline QJsonObject manifest(bool hasArtwork = false, const QString &id = Id, const QString &name = Name)
{
    QJsonObject object{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("base"), QStringLiteral("dark")},
        {QStringLiteral("colors"), QJsonObject{{QStringLiteral("accent"), Accent}}}
    };
    if (hasArtwork) object.insert(QStringLiteral("sidebarImage"), QStringLiteral("sidebar.png"));
    return object;
}

inline QByteArray sidebarImage(const QSize &size = QSize(168, 192))
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#586a96"));
    painter.drawEllipse(QRect(14, 26, 140, 140));
    painter.setPen(QPen(QColor(Accent), 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPoint(34, 136), QPoint(134, 56));
    painter.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return bytes;
}

inline bool writeFile(const QString &filename, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(filename).absolutePath())) return false;
    QFile file(filename);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

inline bool writeInstalled(const QString &root)
{
    return writeFile(QDir(root).filePath(Id + QStringLiteral("/theme.json")), json(manifest()));
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
    Reply(const QNetworkRequest &request, const Response &response, int delay, QObject *parent)
        : QNetworkReply(parent), response_(response)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response_.status);
        if (response_.contentLength >= 0)
            setHeader(QNetworkRequest::ContentLengthHeader, response_.contentLength);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(delay, this, [this] {
            if (isFinished()) return;
            emit metaDataChanged();
            if (isFinished()) return;
            available_ = true;
            emit readyRead();
            if (!isFinished() && !response_.pause) complete(response_.error);
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
        if (error != NoError) setError(error, QStringLiteral("模拟网络连接失败"));
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
    int delay = 0;
    QHash<QString, Response> responses;
    QList<QUrl> requests;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        requests.append(request.url());
        return new Reply(request, responses.value(request.url().path(),
            {{}, QNetworkReply::ContentNotFoundError, 404}), delay, this);
    }
};

} // namespace TestSkinFixture
