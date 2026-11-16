#pragma once

#include "data/appdatabase.h"
#include <QImage>
#include <QMimeData>
#include <memory>

namespace ClipboardContent {
constexpr qsizetype MaximumBytes = 16 * 1024 * 1024;
constexpr qint64 MaximumPixels = 32000000;
inline const QString OwnerFormat = QStringLiteral("application/x-orchestrate-clipboard-fingerprint");
// The payload contains only supported portable formats, not Windows handles or opaque OLE data.
bool read(const QMimeData *mime, AppDatabase::ClipboardItem *item, QString *error = nullptr);
std::unique_ptr<QMimeData> restore(const QByteArray &payload, QString *error = nullptr);
bool plainText(const QString &text, AppDatabase::ClipboardItem *item, QString *error = nullptr);
QImage previewImage(const QByteArray &payload, const QSize &size);
QString kindName(const QString &kind);
}
