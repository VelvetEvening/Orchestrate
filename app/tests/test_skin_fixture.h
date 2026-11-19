#pragma once

#include "ui/skinlibrary.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace TestSkinFixture {

inline const QString Id = QStringLiteral("test-night");
inline const QString Name = QStringLiteral("夜蓝 × 琥珀");
inline const QString Accent = QStringLiteral("#d8ad52");

inline SkinTheme theme()
{
    return {Id, Name, QStringLiteral("dark"), {{QStringLiteral("accent"), Accent}}, {}};
}

inline QByteArray manifest(bool hasArtwork = false)
{
    QJsonObject object{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("id"), Id},
        {QStringLiteral("name"), Name},
        {QStringLiteral("base"), QStringLiteral("dark")},
        {QStringLiteral("colors"), QJsonObject{{QStringLiteral("accent"), Accent}}}
    };
    if (hasArtwork) object.insert(QStringLiteral("sidebarImage"), QStringLiteral("sidebar.png"));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

inline QByteArray sidebarImage()
{
    QImage image(168, 192, QImage::Format_ARGB32_Premultiplied);
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

inline bool writeInstalled(const QString &root)
{
    const QDir dir(QDir(root).filePath(Id));
    if (!dir.mkpath(QStringLiteral("."))) return false;
    QFile file(dir.filePath(QStringLiteral("theme.json")));
    const QByteArray bytes = manifest();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace TestSkinFixture
