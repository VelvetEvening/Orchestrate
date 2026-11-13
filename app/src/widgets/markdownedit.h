#pragma once
#include "markdowntext.h"
#include <QTextEdit>
#include <QTimer>

class QPlainTextEdit;
class MathPreview;

class MarkdownEdit final : public QTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEdit(QPlainTextEdit *source, QWidget *parent = nullptr);
    void setReading(bool reading);
    void syncSource();
    void setBaseFont(const QFont &font);
    void setSourceCursor(int position, int anchor = -1);
    int sourcePosition(int viewPosition) const;
    int viewPosition(int sourcePosition) const;
    void refresh();
    MathPreview *math() const { return math_; }
    int renderedBlockCount() const { return renderedCount_; }
signals:
    void mathStatusChanged(const QString &status);
    void sourceModeRequested();
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *data) override;
    QMimeData *createMimeDataFromSelection() const override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    void changed(int position, int removed, int added);
    void rememberCursor();
    void replaceSource(int start, int length, const QString &text);
    void history(bool redo);
    bool tableMove(bool backward);
    void tableOperation(int operation);
    int currentTable(int *row = nullptr, int *column = nullptr) const;
    void updateMathStatus();
    QPlainTextEdit *source_;
    MathPreview *math_;
    QTimer refreshTimer_;
    QVector<MarkdownText::Block> blocks_;
    QVector<MarkdownText::Rendered> rendered_;
    QVector<int> offsets_;
    QString snapshot_;
    int position_ = 0, anchor_ = 0;
    bool updating_ = false, composing_ = false, selecting_ = false, force_ = true;
    int renderedCount_ = 0;
};
