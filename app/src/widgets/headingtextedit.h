#pragma once

#include <QWidget>

class QPlainTextEdit;
class QStackedWidget;
class QTabBar;
class QTextEdit;

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

    static QString getText(QWidget *parent, const QString &title,
                           const QString &initial, bool *accepted, bool previewFirst = false);

signals:
    void textChanged();
    void textPointSizeChanged(qreal size);
    void previewModeChanged(bool preview);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updatePreview();
    void zoomText(qreal steps);
    QTabBar *tabs_;
    QStackedWidget *pages_;
    QPlainTextEdit *source_;
    QTextEdit *preview_;
};
