#pragma once

#include <QWidget>

class QPlainTextEdit;
class QStackedWidget;
class QTabBar;
class QTextEdit;
class MathPreview;
class QLabel;
class MarkdownEdit;

class HeadingTextEdit final : public QWidget
{
    Q_OBJECT

public:
    explicit HeadingTextEdit(QWidget *parent = nullptr);
    void setPlainText(const QString &text);
    QString toPlainText() const;
    void setPlaceholderText(const QString &text);
    void clear();
    void setPreviewMode(bool preview);
    bool isPreviewMode() const;
    void setTextPointSize(qreal size);
    void setModeControlsVisible(bool visible);
    void setContentLabel(const QString &label);
    void setSourceMode(bool source);
    bool isSourceMode() const;

    static QString getText(QWidget *parent, const QString &title,
                           const QString &initial, bool *accepted, bool previewFirst = false);

signals:
    void textChanged();
    void textPointSizeChanged(qreal size);
    void previewModeChanged(bool preview);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updatePage();
    void zoomText(qreal steps);
    void applyTextPointSize();
    QTabBar *tabs_;
    QStackedWidget *pages_;
    QPlainTextEdit *source_;
    MarkdownEdit *preview_;
    bool sourceMode_ = false;
    qreal textPointSize_ = 0;
    QLabel *mathStatus_;
};
