#pragma once

#include <QWidget>

class PlaceholderPage final : public QWidget
{
    Q_OBJECT

public:
    PlaceholderPage(const QString &title,
                    const QString &subtitle,
                    const QString &message,
                    QWidget *parent = nullptr);
};
