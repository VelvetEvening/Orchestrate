#pragma once

#include <QString>

QString installationMutexName(const QString &directory);

class InstanceLock final {
public:
    explicit InstanceLock(const QString &directory);
    ~InstanceLock();
    bool acquired() const { return acquired_; }
    InstanceLock(const InstanceLock &) = delete;
    InstanceLock &operator=(const InstanceLock &) = delete;

private:
    void *handle_ = nullptr;
    bool acquired_ = false;
};
