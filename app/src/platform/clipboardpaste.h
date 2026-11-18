#pragma once

#include <functional>
#include <QtTypes>

class QWidget;

namespace ClipboardPaste {
void setTrackingEnabled(bool enabled);
// Called while the source still has focus. The caller can hide it immediately afterwards.
void request(QWidget *source, quint32 writtenSequence, std::function<void()> onSkipped);
}
