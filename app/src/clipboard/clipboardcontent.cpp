#include "clipboardcontent.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QTextDocument>
#include <QUrl>

namespace {
class TextOnlyDocument final : public QTextDocument {
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
bool fail(QString *error, const QString &message) { if (error) *error=message; return false; }
bool validImage(const QByteArray &bytes)
{
    QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer,"PNG");
    const auto size=reader.size();
    return size.isValid() && qint64(size.width())*size.height()<=ClipboardContent::MaximumPixels;
}
}

bool ClipboardContent::read(const QMimeData *mime, AppDatabase::ClipboardItem *item, QString *error)
{
    if (!mime || !item) return false;
    QJsonObject data;
    data.insert("version",1);
    QString searchable;
    QString kind;
    qsizetype total=0;
    if (mime->hasText()) {
        searchable=mime->text();
        total+=searchable.toUtf8().size();
        if (total>MaximumBytes) return fail(error,QStringLiteral("本次内容超过 16 MiB，未记录。"));
        if (!searchable.isEmpty()) { data.insert("text",searchable); kind="text"; }
    }
    if (mime->hasHtml()) {
        const auto html=mime->html(); total+=html.toUtf8().size();
        if (total>MaximumBytes) return fail(error,QStringLiteral("本次内容超过 16 MiB，未记录。"));
        if (!html.isEmpty()) {
            data.insert("html",html); kind="html";
            if (searchable.isEmpty()) {
                // Never render pasted HTML or load its resources in the UI.
                TextOnlyDocument document; document.setHtml(html); searchable=document.toPlainText();
                data.insert("text",searchable);
            }
        }
    }
    if (mime->hasImage() || mime->hasFormat("image/png")) {
        QByteArray png=mime->data("image/png");
        if (png.size()>MaximumBytes) return fail(error,QStringLiteral("图片超过 16 MiB，未记录。"));
        if (png.isEmpty()) {
            const auto variant=mime->imageData();
            QImage image=qvariant_cast<QImage>(variant);
            if (image.isNull() && variant.canConvert<QPixmap>()) image=qvariant_cast<QPixmap>(variant).toImage();
            if (image.isNull()) return fail(error,QStringLiteral("无法读取本次图片。"));
            if (qint64(image.width())*image.height()>MaximumPixels) return fail(error,QStringLiteral("图片超过 3200 万像素，未记录。"));
            QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer,"PNG")) return fail(error,QStringLiteral("无法保存本次图片。"));
        }
        total+=png.size();
        if (total>MaximumBytes || !validImage(png)) return fail(error,QStringLiteral("图片过大或格式无效，未记录。"));
        data.insert("png",QString::fromLatin1(png.toBase64())); kind="image";
    }
    if (mime->hasUrls()) {
        QJsonArray urls; QStringList paths;
        for (const auto &url : mime->urls()) {
            const auto encoded=url.toString(QUrl::FullyEncoded); total+=encoded.toUtf8().size();
            if (total>MaximumBytes) return fail(error,QStringLiteral("文件路径超过容量限制，未记录。"));
            urls.append(encoded); paths.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        if (!urls.isEmpty()) { data.insert("urls",urls); kind="files"; if (searchable.isEmpty()) searchable=paths.join('\n'); }
    }
    if (kind.isEmpty()) return false;
    const QByteArray payload=QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (payload.size()>24*1024*1024) return fail(error,QStringLiteral("本次内容超过保存上限，未记录。"));
    item->payload=payload; item->fingerprint=QCryptographicHash::hash(payload,QCryptographicHash::Sha256);
    item->text=searchable; item->kind=kind;
    return true;
}

std::unique_ptr<QMimeData> ClipboardContent::restore(const QByteArray &payload, QString *error)
{
    if (payload.isEmpty() || payload.size()>24*1024*1024) { fail(error,QStringLiteral("条目数据无效。")); return {}; }
    const auto document=QJsonDocument::fromJson(payload);
    const auto data=document.object();
    if (data.value("version").toInt()!=1) { fail(error,QStringLiteral("无法读取此条目的格式版本。")); return {}; }
    auto mime=std::make_unique<QMimeData>();
    if (data.contains("text")) mime->setText(data.value("text").toString());
    if (data.contains("html")) mime->setHtml(data.value("html").toString());
    if (data.contains("png")) {
        const auto png=QByteArray::fromBase64(data.value("png").toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        if (png.size()>MaximumBytes || !validImage(png)) { fail(error,QStringLiteral("保存的图片无效。")); return {}; }
        const auto image=QImage::fromData(png,"PNG");
        if (image.isNull()) { fail(error,QStringLiteral("保存的图片无法解码。")); return {}; }
        mime->setImageData(image); mime->setData("image/png",png);
    }
    if (data.contains("urls")) {
        QList<QUrl> urls;
        for (const auto &value : data.value("urls").toArray()) {
            const auto url=QUrl(value.toString());
            if (!url.isValid() || url.isEmpty()) { fail(error,QStringLiteral("保存的文件路径无效。")); return {}; }
            urls.append(url);
        }
        if (!urls.isEmpty()) mime->setUrls(urls);
    }
    if (mime->formats().isEmpty()) { fail(error,QStringLiteral("条目没有可复制的内容。")); return {}; }
    return mime;
}

bool ClipboardContent::plainText(const QString &text, AppDatabase::ClipboardItem *item, QString *error)
{
    if (text.trimmed().isEmpty()) return fail(error,QStringLiteral("请填写内容。"));
    QMimeData mime; mime.setText(text); return read(&mime,item,error);
}

QImage ClipboardContent::previewImage(const QByteArray &payload, const QSize &size)
{
    const auto data=QJsonDocument::fromJson(payload).object();
    auto png=QByteArray::fromBase64(data.value("png").toString().toLatin1());
    if (png.isEmpty() || png.size()>MaximumBytes || !validImage(png)) return {};
    QBuffer buffer(&png); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer,"PNG"); reader.setScaledSize(reader.size().scaled(size,Qt::KeepAspectRatio));
    return reader.read();
}

QString ClipboardContent::kindName(const QString &kind)
{
    if (kind=="image") return QStringLiteral("图片");
    if (kind=="html") return QStringLiteral("富文本");
    if (kind=="files") return QStringLiteral("文件路径");
    return QStringLiteral("文本");
}
