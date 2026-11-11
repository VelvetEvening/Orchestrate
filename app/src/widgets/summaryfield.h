#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

// A quiet fixed caption leaves the editable field and its focus border intact.
inline QLineEdit *addSummaryField(QVBoxLayout *layout, QWidget *parent, const QString &name,
                                 const QString &captionText = QStringLiteral("简介"))
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("summaryField"));

    auto *row = new QHBoxLayout(frame);
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(0);
    auto *caption = new QLabel(captionText,frame);
    caption->setObjectName(QStringLiteral("summaryCaption"));
    row->addWidget(caption);
    auto *edit = new QLineEdit(frame);
    edit->setObjectName(name);
    edit->setAccessibleName(captionText);
    edit->setPlaceholderText(QStringLiteral("填写") + captionText);
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
