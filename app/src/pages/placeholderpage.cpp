#include "placeholderpage.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

PlaceholderPage::PlaceholderPage(const QString &title,
                                 const QString &subtitle,
                                 const QString &message,
                                 QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(34, 30, 34, 34);
    layout->setSpacing(18);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel(subtitle, this);
    subtitleLabel->setObjectName(QStringLiteral("pageSubtitle"));
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Plain);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(30, 30, 30, 30);
    cardLayout->setSpacing(10);

    auto *emptyTitle = new QLabel(QStringLiteral("页面骨架已就位"), card);
    emptyTitle->setObjectName(QStringLiteral("emptyStateTitle"));
    cardLayout->addWidget(emptyTitle);

    auto *messageLabel = new QLabel(message, card);
    messageLabel->setObjectName(QStringLiteral("muted"));
    messageLabel->setWordWrap(true);
    cardLayout->addWidget(messageLabel);
    cardLayout->addStretch(1);

    layout->addWidget(card, 1);
}
