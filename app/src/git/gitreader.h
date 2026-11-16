#pragma once
#include "repositorytarget.h"
#include <QObject>
#include <QProcess>
#include <QMap>
#include <QDateTime>
#include <functional>

namespace Git {
struct Result {
    bool ok = false;
    QByteArray output;
    QString error;
    int exitCode = -1;
};
// Each reader owns one cancellable operation. No callback survives cancellation.
class Reader final : public QObject {
public:
    explicit Reader(QObject *parent = nullptr) : QObject(parent) {}
    ~Reader() override;
    void cancel();
    void run(const RepositoryTarget &target, const QStringList &arguments,
             std::function<void(Result)> callback, qint64 limit = 16 * 1024 * 1024,
             const QByteArray &input = {});
    void probe(const RepositoryTarget &target, std::function<void(Result)> callback);
    static bool validTarget(const RepositoryTarget &target, QString *error);
    static QString shellCommand(const RepositoryTarget &target, const QStringList &arguments);
private:
    void start(const QString &program, const QStringList &arguments,
               std::function<void(Result)> callback, qint64 limit, const QByteArray &input = {});
    QObject *request_ = nullptr;
};
struct Commit { QString id; QStringList parents; QString author; QDateTime date; QString subject; };
struct Reference { QString name; QString id; };
struct FileChange { QString status; QString path; QString oldPath; };
enum class BranchState { LocalOnly, RemoteOnly, Equal, Different };
struct Branch { QString name; QString localId; QString remoteId; BranchState state; };
bool validObjectId(const QString &id);
bool parseCommits(const QByteArray &data, QList<Commit> *commits);
bool parseReferences(const QByteArray &data, QList<Reference> *refs);
bool parseRemoteBranches(const QByteArray &data, QMap<QString, QString> *branches);
bool parseFiles(const QByteArray &data, QList<FileChange> *files);
QList<Branch> compareBranches(const QMap<QString, QString> &local, const QMap<QString, QString> &remote);
QString branchStateText(BranchState state);
QStringList logArguments(int offset, int count);
QStringList fileArguments(const QString &commit, const QString &parent, bool patch = false);
QString literalFilePath(const QString &path);
}
