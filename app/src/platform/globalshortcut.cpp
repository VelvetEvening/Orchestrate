#include "globalshortcut.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QUuid>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
bool isAllowed(QKeyCombination binding)
{
    const auto modifier = binding.keyboardModifiers();
    return (modifier == Qt::AltModifier || modifier == Qt::ControlModifier)
        && binding.key() >= Qt::Key_A && binding.key() <= Qt::Key_Z;
}

QKeyCombination readBinding(const QString &saved)
{
    const auto sequence = QKeySequence::fromString(saved, QKeySequence::PortableText);
    if (sequence.count() == 1 && isAllowed(sequence[0])) return sequence[0];
    return QKeyCombination(Qt::AltModifier, Qt::Key_X);
}

#ifdef Q_OS_WIN
UINT nativeModifier(QKeyCombination binding)
{
    return binding.keyboardModifiers() == Qt::ControlModifier ? MOD_CONTROL : MOD_ALT;
}
#endif

unsigned short registerBinding(QKeyCombination binding, QString *error)
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("Orchestrate.Hotkey.") + QUuid::createUuid().toString();
    const ATOM id = GlobalAddAtomW(reinterpret_cast<LPCWSTR>(name.utf16()));
    if (id && RegisterHotKey(nullptr, id, nativeModifier(binding) | MOD_NOREPEAT, UINT(binding.key())))
        return id;
    const DWORD code = GetLastError();
    if (id) GlobalDeleteAtom(id);
    const auto text = GlobalShortcut::text(binding);
    *error = code == ERROR_HOTKEY_ALREADY_REGISTERED
        ? QStringLiteral("%1 已被占用，请更换组合或稍后重试。").arg(text)
        : QStringLiteral("%1 注册失败（Windows 错误 %2）。").arg(text).arg(code);
#else
    Q_UNUSED(binding);
    *error = QStringLiteral("当前平台不支持此 Windows 全局快捷键。");
#endif
    return 0;
}

void releaseBinding(unsigned short id)
{
#ifdef Q_OS_WIN
    if (id) {
        UnregisterHotKey(nullptr, id);
        GlobalDeleteAtom(id);
    }
#else
    Q_UNUSED(id);
#endif
}
}

GlobalShortcut::GlobalShortcut(bool enabled, const QString &savedBinding, QObject *parent)
    : QObject(parent), enabled_(enabled), binding_(readBinding(savedBinding))
{
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &GlobalShortcut::release);
    apply(enabled_, binding_);
}

GlobalShortcut::~GlobalShortcut()
{
    release();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

QString GlobalShortcut::text(QKeyCombination binding)
{
    return QKeySequence(binding).toString(QKeySequence::PortableText);
}

void GlobalShortcut::release()
{
    releaseBinding(id_);
    id_ = 0;
}

bool GlobalShortcut::reportFailure(QString message)
{
    if (isRegistered())
        message += QStringLiteral(" 当前仍使用 %1。").arg(text(binding_));
    else if (enabled_)
        message += QStringLiteral(" 当前可通过托盘打开 %1。").arg(QGuiApplication::applicationDisplayName());
    else
        message += QStringLiteral(" 全局快捷键已关闭。");
    status_ = message;
    emit statusChanged(status_);
    return false;
}

bool GlobalShortcut::apply(bool enabled, QKeyCombination binding, const std::function<bool(QString *)> &save)
{
    if (!isAllowed(binding)) return reportFailure(QStringLiteral("请选择 Ctrl 或 Alt，再选择一个 A–Z 字母。"));
    const auto platform = QGuiApplication::platformName();
    const bool offscreen = platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal");
    unsigned short candidate = 0;
    QString error;
    // Keep the old registration until both the replacement and its preference are ready.
    if (enabled && !offscreen && (!isRegistered() || binding != binding_)) {
        candidate = registerBinding(binding, &error);
        if (!candidate) return reportFailure(error);
    }
    if (save && !save(&error)) {
        releaseBinding(candidate);
        return reportFailure(QStringLiteral("快捷键设置保存失败。%1").arg(error));
    }
    if (!enabled || candidate) {
        release();
        id_ = candidate;
    }
    enabled_ = enabled;
    binding_ = binding;
    status_.clear();
    emit statusChanged(status_);
    return true;
}

bool GlobalShortcut::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (isRegistered() && message && (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG")) {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == id_
            && LOWORD(msg->lParam) == nativeModifier(binding_) && HIWORD(msg->lParam) == UINT(binding_.key())) {
            if (result) *result = 0;
            emit activated();
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
