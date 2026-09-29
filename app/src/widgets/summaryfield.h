#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

// A fixed shaded caption is visually separate from the editable value.
inline QLineEdit *addSummaryField(QVBoxLayout *layout, QWidget *parent, const QString &name)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("summaryField"));
    frame->setStyleSheet(QStringLiteral(
        "QFrame#summaryField { border: 1px solid #cfdaea; border-radius: 7px; background: white; }"
        "QLabel#summaryCaption { background: #eef2f8; color: #526176; font-weight: 600; padding: 9px 14px; border: none; border-right: 1px solid #cfdaea; border-top-left-radius: 6px; border-bottom-left-radius: 6px; }"
        "QLineEdit { border: none; background: transparent; padding: 8px 10px; }"));
    auto *row = new QHBoxLayout(frame);
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(0);
    auto *caption = new QLabel(QStringLiteral("简介"),frame);
    caption->setObjectName(QStringLiteral("summaryCaption"));
    row->addWidget(caption);
    auto *edit = new QLineEdit(frame);
    edit->setObjectName(name);
    edit->setAccessibleName(QStringLiteral("简介"));
    edit->setPlaceholderText(QStringLiteral("填写简介"));
    row->addWidget(edit,1);
    layout->addWidget(frame);
    return edit;
}

inline QString summaryText(const QString &title, const QString &content)
{
    if (!title.trimmed().isEmpty()) return title;
    for (QString line : content.split('\n')) {
        line = line.trimmed();
        if (!line.isEmpty()) return line.left(120);
    }
    return QStringLiteral("未命名记录");
}
