#pragma once

#include "headingtextedit.h"
#include "../data/appdatabase.h"
#include <QMessageBox>
#include <QtMath>

// Each kind of document remembers its display preference in the portable DB.
// Connect after restoring so opening a document never writes a setting.
inline void bindPersistentTextZoom(HeadingTextEdit *editor, AppDatabase *database,
                                   const QString &key)
{
    bool ok = false;
    const qreal size = database->setting(key).toDouble(&ok);
    if (ok && qIsFinite(size) && size >= 6.0 && size <= 72.0)
        editor->setTextPointSize(size);
    QObject::connect(editor, &HeadingTextEdit::textPointSizeChanged, editor,
                     [editor, database, key](qreal value) {
        QString error;
        if (!database->setSetting(key, QString::number(value, 'g', 16), &error))
            QMessageBox::warning(editor, QStringLiteral("字号保存失败"),
                                 QStringLiteral("当前字号已调整，但下次打开可能无法恢复。\n") + error);
    });
}
