#pragma once

class QStackedWidget;
class QWidget;

namespace PageTransition {
enum class Direction { Module, Forward, Back, Instant };

void setCurrent(QStackedWidget *stack, QWidget *page, Direction direction);
void cancel(QStackedWidget *stack);
}
