#include "clipboardpaste.h"
#include <QApplication>
#include <QTimer>
#include <QWidget>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>

namespace {
struct ExternalWindow {
    HWND handle = nullptr;
    DWORD process = 0;
    DWORD thread = 0;
};

ExternalWindow previous;

void remember(HWND window)
{
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window,&process);
    if (!thread || process==GetCurrentProcessId()) return;
    // Taskbar and desktop activation can intervene when opening the tray menu.
    wchar_t name[64]{};
    GetClassNameW(window,name,64);
    const QString type = QString::fromWCharArray(name);
    if (window==GetShellWindow() || type=="Shell_TrayWnd" || type=="Shell_SecondaryTrayWnd"
        || type=="NotifyIconOverflowWindow" || type=="TopLevelWindowForOverflowXamlIsland" || type=="WorkerW") return;
    previous = {window,process,thread};
}

void CALLBACK foregroundChanged(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD)
{
    remember(window);
}

struct ForegroundTracker {
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,
        foregroundChanged,0,0,WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    ForegroundTracker() { if (hook) remember(GetForegroundWindow()); }
    ~ForegroundTracker() { if (hook) UnhookWinEvent(hook); }
};

bool available(const ExternalWindow &target)
{
    DWORD process = 0;
    return target.handle && GetWindowThreadProcessId(target.handle,&process)==target.thread
        && process==target.process && IsWindowVisible(target.handle) && !IsIconic(target.handle);
}

bool paste(const ExternalWindow &target, DWORD sequence)
{
    if (!available(target) || GetForegroundWindow()!=target.handle
        || sequence==0 || GetClipboardSequenceNumber()!=sequence) return false;
    for (const int key : {VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN})
        if (GetAsyncKeyState(key) & 0x8000) return false;

    INPUT keys[4]{};
    for (auto &key : keys) key.type = INPUT_KEYBOARD;
    keys[0].ki.wVk = keys[3].ki.wVk = VK_CONTROL;
    keys[1].ki.wVk = keys[2].ki.wVk = 'V';
    keys[2].ki.dwFlags = keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    const UINT sent = SendInput(4,keys,sizeof(INPUT));
    // Release any key this call pressed if Windows accepted only part of the chord.
    if (sent==1 || sent==3) SendInput(1,keys+3,sizeof(INPUT));
    else if (sent==2) SendInput(2,keys+2,sizeof(INPUT));
    return sent==4;
}
}
#endif

void ClipboardPaste::setTrackingEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    if (QApplication::platformName()=="windows") {
        static std::optional<ForegroundTracker> tracker;
        if (enabled && !tracker) tracker.emplace();
        if (!enabled) {
            tracker.reset();
            previous = {};
        }
    }
#else
    Q_UNUSED(enabled)
#endif
}

void ClipboardPaste::request(QWidget *source, quint32 writtenSequence, std::function<void()> onSkipped)
{
#ifdef Q_OS_WIN
    if (QApplication::platformName()=="windows") {
        const auto target = previous;
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
        if (writtenSequence && GetClipboardSequenceNumber()==writtenSequence && foregroundProcess==GetCurrentProcessId()
            && available(target) && SetForegroundWindow(target.handle)) {
            QTimer::singleShot(60,source,[source,target,writtenSequence,onSkipped] {
                if (source->isVisible() || !paste(target,writtenSequence)) onSkipped();
            });
            return;
        }
    }
#else
    Q_UNUSED(source)
    Q_UNUSED(writtenSequence)
#endif
    onSkipped();
}
