#pragma once
#include "git/gitreader.h"
#include <QWidget>
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTimer;

class RepositoryBinding final : public QWidget {
    Q_OBJECT
public:
    explicit RepositoryBinding(const RepositoryTarget &target, QWidget *parent = nullptr);
    ~RepositoryBinding() override;
    RepositoryTarget value() const;
    void resolve(std::function<void(bool, RepositoryTarget, QString)> callback);
private:
    void changeEnvironment();
    void discoverUsers();
    void cancelDiscovery();
    QComboBox *type_, *location_, *user_;
    QLineEdit *directory_;
    QLabel *locationLabel_, *userLabel_, *status_;
    QPushButton *browse_, *refresh_, *validate_;
    Git::Reader reader_;
    QObject *discovery_ = nullptr;
    QTimer *discoveryTimer_;
    bool resolving_ = false;
};
