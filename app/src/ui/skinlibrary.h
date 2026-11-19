#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

struct SkinTheme {
    QString id;
    QString name;
    QString base;
    QMap<QString, QString> colors;
    QString sidebarImagePath;
};

struct SkinCatalogEntry {
    QString id;
    QString name;
};

class SkinLibrary final : public QObject
{
    Q_OBJECT
public:
    static QUrl defaultCatalogUrl();
    explicit SkinLibrary(const QString &installRoot, QObject *parent = nullptr,
                         const QUrl &catalogUrl = defaultCatalogUrl(),
                         QNetworkAccessManager *network = nullptr);
    ~SkinLibrary() override;

    QList<SkinTheme> installedThemes() const;
    std::optional<SkinTheme> installedTheme(const QString &id) const;
    QList<SkinCatalogEntry> catalog() const;
    bool isBusy() const;

    void refreshCatalog();
    void installTheme(const QString &id);
    void cancel();
    // Persist any necessary selection change while removal can still be rolled back.
    bool removeTheme(const QString &id, QString *error = nullptr,
                     const std::function<bool(QString *)> &saveSelection = {});

signals:
    void catalogFinished(const QString &error);
    void installFinished(const QString &id, const QString &error);
    void installedChanged();

private:
    enum class Request { None, Catalog, Manifest, SidebarImage };

    void fetch(const QUrl &url, qint64 limit);
    bool exceedsLimit(QNetworkReply *reply);
    void replyFinished(QNetworkReply *reply);
    void closeReply(bool abort);
    void failRequest(const QString &error);
    void completeRequest(const QByteArray &bytes, QString error);
    void completeInstall(const QString &error);
    QString publish(const QByteArray &sidebarImage);

    QString installRoot_;
    QUrl catalogUrl_;
    QList<SkinCatalogEntry> catalog_;
    QPointer<QNetworkAccessManager> network_;
    QPointer<QNetworkReply> reply_;
    Request request_ = Request::None;
    bool removing_ = false;
    qint64 responseLimit_ = 0;
    QByteArray response_;
    QString installId_;
    QString installName_;
    QByteArray manifest_;
};
