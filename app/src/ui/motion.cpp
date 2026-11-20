#include "motion.h"

#include <QGuiApplication>
#include <QPointer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
bool systemAnimationsEnabled()
{
    const QString platform = QGuiApplication::platformName();
#ifdef Q_OS_WIN
    if (platform == QStringLiteral("windows")) {
        BOOL enabled = FALSE;
        return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && enabled;
    }
#endif
    return !platform.isEmpty() && platform != QStringLiteral("offscreen")
        && platform != QStringLiteral("minimal");
}
}

UiMotion::UiMotion(QCoreApplication &app)
    : QObject(&app), systemAnimations_(systemAnimationsEnabled())
{
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        app.installNativeEventFilter(this);
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) refreshSystemPreference();
        });
    }
#endif
}

UiMotion &UiMotion::instance()
{
    static QPointer<UiMotion> motion;
    if (!motion) {
        auto *app = QCoreApplication::instance();
        Q_ASSERT(app);
        motion = new UiMotion(*app);
    }
    return *motion;
}

bool UiMotion::enabled()
{
    return QCoreApplication::instance() && instance().animationsAllowed();
}

int UiMotion::duration(int milliseconds)
{
    return enabled() ? milliseconds : 0;
}

QEasingCurve UiMotion::outBack(qreal overshoot)
{
    QEasingCurve curve(QEasingCurve::OutBack);
    curve.setOvershoot(overshoot);
    return curve;
}

bool UiMotion::animationsAllowed() const
{
    return systemAnimations_ && preference_ == Preference::FollowSystem;
}

void UiMotion::setPreference(Preference preference)
{
    const bool wasEnabled = animationsAllowed();
    preference_ = preference;
    if (animationsAllowed() != wasEnabled) emit enabledChanged(!wasEnabled);
}

void UiMotion::refreshSystemPreference()
{
    const bool wasEnabled = animationsAllowed();
    systemAnimations_ = systemAnimationsEnabled();
    if (animationsAllowed() != wasEnabled) emit enabledChanged(!wasEnabled);
}

bool UiMotion::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (message && (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG")
        && static_cast<const MSG *>(message)->message == WM_SETTINGCHANGE) {
        refreshSystemPreference();
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    Q_UNUSED(result);
    return false;
}
