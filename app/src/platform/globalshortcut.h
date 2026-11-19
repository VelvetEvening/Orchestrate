#pragma once
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <functional>

// GUI-thread ownership; no hooks, polling or input replay.
class GlobalShortcut final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    GlobalShortcut(bool enabled, const QString &savedBinding, QObject *parent = nullptr);
    ~GlobalShortcut() override;
    bool apply(bool enabled, QKeyCombination binding, const std::function<bool(QString *)> &save = {});
    bool enabled() const { return enabled_; }
    QKeyCombination binding() const { return binding_; }
    bool isRegistered() const { return id_ != 0; }
    static QString text(QKeyCombination binding);
    QString status() const { return status_; }
    bool hasError() const { return hasError_; }
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
signals:
    void activated();
    void statusChanged(const QString &status);
private:
    friend class GlobalShortcutTest; // Deliver WM_HOTKEY in tests without synthesizing keyboard input.
    void release();
    bool reportFailure(QString message);
    bool enabled_;
    QKeyCombination binding_;
    unsigned short id_ = 0;
    QString status_;
    bool hasError_ = false;
};
