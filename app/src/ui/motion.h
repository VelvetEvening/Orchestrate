#pragma once

#include <QAbstractNativeEventFilter>
#include <QEasingCurve>
#include <QObject>

class QCoreApplication;

class UiMotion final : public QObject, private QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    enum class Preference { FollowSystem, Off };

    static UiMotion &instance();
    static bool enabled();
    static int duration(int milliseconds);
    static QEasingCurve outBack(qreal overshoot);

    Preference preference() const { return preference_; }
    void setPreference(Preference preference);

signals:
    void enabledChanged(bool enabled);

private:
    explicit UiMotion(QCoreApplication &app);
    bool animationsAllowed() const;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
    void refreshSystemPreference();

    Preference preference_ = Preference::FollowSystem;
    bool systemAnimations_;
};
