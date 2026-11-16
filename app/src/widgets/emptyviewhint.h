#pragma once

#include <QAbstractItemView>
#include <QEvent>
#include <QLabel>

// A viewport hint does not add fake model rows or intercept drag/drop targets.
class EmptyViewHint final : public QLabel
{
public:
    explicit EmptyViewHint(QWidget *viewport) : QLabel(viewport)
    {
        setObjectName(QStringLiteral("emptyViewHint"));
        setTextFormat(Qt::PlainText);
        setWordWrap(true);
        setAlignment(Qt::AlignCenter);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        viewport->installEventFilter(this);
        fit();
    }
    static void update(QAbstractItemView *view, const QString &message)
    {
        auto *label = view->viewport()->findChild<QLabel *>(QStringLiteral("emptyViewHint"), Qt::FindDirectChildrenOnly);
        if (!label) label = new EmptyViewHint(view->viewport());
        label->setText(message);
        label->setVisible(!message.isEmpty());
        label->raise();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Resize) fit();
        return false;
    }
private:
    void fit() { setGeometry(parentWidget()->rect().adjusted(12, 8, -12, -8)); }
};
