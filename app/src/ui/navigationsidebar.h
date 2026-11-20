#pragma once

#include <QFrame>
#include <QPixmap>
#include <QVariantAnimation>

class QPainter;
class QPushButton;

class NavigationSidebar final : public QFrame
{
public:
    explicit NavigationSidebar(QWidget *parent);

    QPushButton *addButton(const QString &label, const QString &iconName, bool pinnedToBottom);
    void setButtonLabel(QPushButton *button, const QString &label);
    bool isCollapsed() const;
    void setCollapsed(bool collapsed, bool animated);
    void setActiveButton(QPushButton *button, bool animated);
    void setArtworkPath(const QString &path);

protected:
    bool event(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void finishMotion();
    qreal indicatorCenter() const;
    void paintArtwork(QPainter &painter) const;

    QVariantAnimation widthMotion_;
    QVariantAnimation indicatorMotion_;
    QPushButton *activeButton_ = nullptr;
    qreal indicatorStart_ = 0.0;
    int targetWidth_;
    QString artworkPath_;
    QPixmap artwork_;
};
