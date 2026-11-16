#pragma once
#include "mathtext.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QSet>

class MathPreview final : public QObject {
    Q_OBJECT
public:
    explicit MathPreview(QObject *parent);
    ~MathPreview() override;
    void request(const QString &source, qreal pixels);
    void cancel();
    bool pending() const { return process_ || debounce_.isActive(); }
    const MathText::Images &images() const { return images_; }
signals:
    void ready();
private:
    void start();
    void read();
    void finish();
    QProcess *process_ = nullptr;
    QTimer debounce_, timeout_;
    QByteArray request_, output_;
    QStringList keys_;
    QStringList requested_;
    QSet<QString> failed_;
    qreal pixels_ = 0;
    MathText::Images images_;
    qint64 imageBytes_ = 0, receivedBytes_ = 0;
    void *job_ = nullptr;
};
