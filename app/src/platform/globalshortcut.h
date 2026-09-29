#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>

// GUI-thread ownership; no hooks, polling or input replay.
class GlobalShortcut final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalShortcut(QObject *parent = nullptr);
    ~GlobalShortcut() override;
    void configure(bool enabled);
    bool isRegistered() const { return registered_; }
    QString status() const { return status_; }
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
signals:
    void activated();
    void statusChanged(const QString &status);
private:
    void unregister();
    bool registered_ = false;
    unsigned short id_ = 0;
    QString status_;
};
