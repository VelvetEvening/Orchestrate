#include "gitreader.h"
#include "automation/wslprocess.h"
#include "automation/sshconnection.h"
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QSet>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace Git {
namespace {
QString environmentScript()
{
    return QStringLiteral("unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_NAMESPACE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES; "
        "export GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=Never GIT_OPTIONAL_LOCKS=0 GIT_NO_LAZY_FETCH=1 LC_ALL=C; "
        "export GIT_SSH_COMMAND='ssh -oBatchMode=yes -oStrictHostKeyChecking=yes -oConnectTimeout=10'; ");
}
QString pathScript()
{
    return QStringLiteral("p=$1; shift; case \"$p\" in '~') p=$HOME;; '~/'*) p=\"$HOME/${p#\\~/}\";; /*) ;; "
        "*) printf '%s\\n' 'Use an absolute path or ~/.' >&2; exit 2;; esac; "
        "cd -P -- \"$p\" || exit; ");
}
QStringList gitPrefix()
{
    return {QStringLiteral("--no-pager"),
        QStringLiteral("-c"), QStringLiteral("color.ui=false"), QStringLiteral("-c"), QStringLiteral("core.quotePath=false"),
        QStringLiteral("-c"), QStringLiteral("log.showSignature=false"), QStringLiteral("-c"), QStringLiteral("i18n.logOutputEncoding=utf-8")};
}
QString command(const QString &script, const QStringList &args)
{
    QString result = QStringLiteral("sh -c ") + PosixRegistration::quote(script) + QStringLiteral(" orchestrate-git");
    for (const auto &arg : args) result += QLatin1Char(' ') + PosixRegistration::quote(arg);
    return result;
}
// Destroying the request first disconnects callbacks, then terminates its child.
class Request final : public QObject {
public:
    QProcess *process;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
#endif
    explicit Request(QObject *parent) : QObject(parent), process(new QProcess(this)) {
#ifdef Q_OS_WIN
        job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
        connect(process, &QProcess::started, this, [this] {
            if (!job) return;
            HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process->processId()));
            if (child) { AssignProcessToJobObject(job, child); CloseHandle(child); }
        });
#endif
    }
    ~Request() override {
        process->disconnect(this);
#ifdef Q_OS_WIN
        if (job) CloseHandle(job);
#endif
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
};
}

Reader::~Reader() { cancel(); }
void Reader::cancel() { delete request_; request_ = nullptr; }

bool Reader::validTarget(const RepositoryTarget &t, QString *error)
{
    bool ok = !t.directory.isEmpty() && !t.directory.contains(QChar::Null);
    if (t.type == QStringLiteral("wsl")) ok &= WslProcess::validDistribution(t.location);
    else if (t.type == QStringLiteral("ssh")) ok &= SshConnection::validHost(t.location);
    else if (t.type != QStringLiteral("local")) ok = false;
    if (t.type != QStringLiteral("local")) {
        ok &= PosixRegistration::validPath(t.directory)
            && (t.directory.startsWith(QLatin1Char('/')) || t.directory == QStringLiteral("~") || t.directory.startsWith(QStringLiteral("~/")))
            && (t.user.isEmpty() || PosixRegistration::validUser(t.user));
    }
    if (!ok && error) *error = QStringLiteral("请检查环境、主机或发行版、用户和目录；Linux 目录须为绝对路径或 ~/ 开头。");
    return ok;
}

QString Reader::shellCommand(const RepositoryTarget &target, const QStringList &arguments)
{
    QStringList args{target.directory}; args.append(gitPrefix()); args.append(arguments);
    return command(environmentScript() + pathScript() + QStringLiteral("exec git \"$@\""), args);
}

void Reader::run(const RepositoryTarget &target, const QStringList &arguments,
                 std::function<void(Result)> callback, qint64 limit, const QByteArray &input)
{
    cancel();
    QString error;
    if (!validTarget(target, &error)) { callback({false, {}, error}); return; }
    if (target.type == QStringLiteral("local")) {
        auto args = gitPrefix(); args << QStringLiteral("-C") << target.directory; args.append(arguments);
        start(QStringLiteral("git"), args, std::move(callback), limit, input);
    } else if (target.type == QStringLiteral("wsl")) {
        QStringList args{target.directory}; args.append(gitPrefix()); args.append(arguments);
        start(WslProcess::program(), WslProcess::arguments(target.location, target.user, QStringLiteral("sh"),
            QStringList{QStringLiteral("-c"), environmentScript() + pathScript() + QStringLiteral("exec git \"$@\""),
             QStringLiteral("orchestrate-git")} + args), std::move(callback), limit, input);
    } else {
        start(QStringLiteral("ssh"), SshConnection::arguments(target.location, target.user, shellCommand(target, arguments)),
            std::move(callback), limit, input);
    }
}

void Reader::probe(const RepositoryTarget &target, std::function<void(Result)> callback)
{
    cancel();
    QString error;
    if (!validTarget(target, &error)) { callback({false, {}, error}); return; }
    if (target.type == QStringLiteral("local")) {
        run(target, {QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")}, std::move(callback));
        return;
    }
    const auto script = environmentScript() + pathScript() + QStringLiteral(
        "printf '%s\\000%s\\000' \"$(id -un)\" \"$PWD\"; "
        "if ! command -v git >/dev/null 2>&1; then printf missing-git; "
        "elif git rev-parse --git-dir >/dev/null 2>&1; then printf git; else printf directory; fi");
    if (target.type == QStringLiteral("wsl"))
        start(WslProcess::program(), WslProcess::arguments(target.location, target.user, QStringLiteral("sh"),
            {QStringLiteral("-c"), script, QStringLiteral("orchestrate-git"), target.directory}), std::move(callback), 1024 * 1024);
    else start(QStringLiteral("ssh"), SshConnection::arguments(target.location, target.user, command(script, {target.directory})),
        std::move(callback), 1024 * 1024);
}

void Reader::start(const QString &program, const QStringList &arguments, std::function<void(Result)> callback,
                   qint64 limit, const QByteArray &input)
{
    cancel();
    auto *request = new Request(this); request_ = request;
    auto *process = request->process;
    auto environment = QProcessEnvironment::systemEnvironment();
    // Ignore inherited repository selectors; the binding alone chooses the repository.
    for (const auto &key : environment.keys())
        if (key.startsWith(QStringLiteral("GIT_"))) environment.remove(key);
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_NO_LAZY_FETCH"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GIT_SSH_COMMAND"), QStringLiteral("ssh -oBatchMode=yes -oStrictHostKeyChecking=yes -oConnectTimeout=10"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process->setProcessEnvironment(environment);
    process->setProgram(program); process->setArguments(arguments);
    struct Buffer { QByteArray out, err; bool done = false; };
    auto buffer = std::make_shared<Buffer>();
    auto *timer = new QTimer(request); timer->setSingleShot(true);
    const auto finish = [this, request, process, timer, buffer, callback](Result result) {
        if (buffer->done) return;
        buffer->done = true; timer->stop();
        if (process->state() != QProcess::NotRunning) process->kill();
        if (request_ == request) request_ = nullptr;
        request->deleteLater();
        callback(std::move(result));
    };
    const auto drain = [process, buffer, finish, limit] {
        if (buffer->done) return;
        buffer->out += process->readAllStandardOutput();
        const auto errors = process->readAllStandardError();
        buffer->err += errors.left(qMax<qsizetype>(0, 64 * 1024 - buffer->err.size()));
        if (limit > 0 && buffer->out.size() > limit)
            finish({false, {}, QStringLiteral("读取内容过大，已停止本次读取。可选择单个文件查看差异。")});
    };
    connect(process, &QProcess::readyReadStandardOutput, request, drain);
    connect(process, &QProcess::readyReadStandardError, request, drain);
    connect(process, &QProcess::started, request, [process, input] { if (!input.isEmpty()) process->write(input); process->closeWriteChannel(); });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), request,
        [buffer, drain, finish](int code, QProcess::ExitStatus status) {
            drain();
            if (buffer->done) return;
            const bool ok = code == 0 && status == QProcess::NormalExit;
            finish({ok, buffer->out, ok ? QString() : QStringLiteral("Git 读取失败（%1）：%2\n请检查目录、Git 安装和连接认证后重试。")
                .arg(code).arg(ProcessCapture::diagnostic(buffer->err).trimmed()), code});
        });
    connect(process, &QProcess::errorOccurred, request, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish({false, {}, QStringLiteral("无法启动 Git 或连接程序：%1。请检查安装和 PATH。").arg(process->errorString())});
    });
    connect(timer, &QTimer::timeout, request, [finish] { finish({false, {}, QStringLiteral("读取超时。请检查连接或认证后重试。")}); });
    timer->start(30000); process->start();
}

bool validObjectId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
    return pattern.match(id).hasMatch();
}
bool parseCommits(const QByteArray &data, QList<Commit> *commits)
{
    QList<Commit> result;
    auto fields = data.split('\0');
    if (!fields.isEmpty() && fields.last().isEmpty()) fields.removeLast();
    if (fields.size() % 5) return false;
    for (qsizetype i = 0; i < fields.size(); i += 5) {
        Commit c{QString::fromUtf8(fields[i]), QString::fromUtf8(fields[i+1]).split(' ', Qt::SkipEmptyParts),
            QString::fromUtf8(fields[i+2]), QDateTime::fromString(QString::fromUtf8(fields[i+3]), Qt::ISODate), QString::fromUtf8(fields[i+4])};
        if (!validObjectId(c.id)) return false;
        for (const auto &parent : c.parents) if (!validObjectId(parent)) return false;
        result.append(c);
    }
    *commits = result; return true;
}
bool parseReferences(const QByteArray &data, QList<Reference> *refs)
{
    QList<Reference> result;
    for (const auto &line : data.split('\n')) {
        if (line.isEmpty()) continue;
        const auto fields = line.split('\0');
        if (fields.size() != 5) return false;
        if (fields[3] != "commit" && fields[4] != "commit") continue;
        const auto name = QString::fromUtf8(fields[0]);
        const auto id = QString::fromLatin1(fields[2].isEmpty() ? fields[1] : fields[2]);
        if (!name.startsWith(QStringLiteral("refs/")) || !validObjectId(id)) return false;
        result.append({name, id});
    }
    *refs = result; return true;
}
bool parseRemoteBranches(const QByteArray &data, QMap<QString, QString> *branches)
{
    QMap<QString, QString> result;
    for (auto line : data.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        const auto fields = line.split('\t');
        if (fields.size() != 2 || !fields[1].startsWith("refs/heads/")) return false;
        const auto id = QString::fromLatin1(fields[0]);
        const auto name = QString::fromUtf8(fields[1].mid(11));
        if (!validObjectId(id) || name.isEmpty() || result.contains(name)) return false;
        result.insert(name, id);
    }
    *branches = result; return true;
}
bool parseFiles(const QByteArray &data, QList<FileChange> *files)
{
    auto fields = data.split('\0');
    if (!fields.isEmpty() && fields.last().isEmpty()) fields.removeLast();
    QList<FileChange> result;
    for (qsizetype i = 0; i < fields.size();) {
        if (i + 1 >= fields.size() || fields[i].isEmpty()) return false;
        FileChange file{QString::fromLatin1(fields[i++]), QString::fromUtf8(fields[i++]), {}};
        if (file.status.startsWith('R') || file.status.startsWith('C')) {
            if (i >= fields.size()) return false;
            file.oldPath = file.path; file.path = QString::fromUtf8(fields[i++]);
        }
        result.append(file);
    }
    *files = result; return true;
}
QList<Branch> compareBranches(const QMap<QString, QString> &local, const QMap<QString, QString> &remote)
{
    QSet<QString> names;
    for (auto i = local.cbegin(); i != local.cend(); ++i) names.insert(i.key());
    for (auto i = remote.cbegin(); i != remote.cend(); ++i) names.insert(i.key());
    auto sorted = names.values(); sorted.sort();
    QList<Branch> result;
    for (const auto &name : sorted) result.append({name, local.value(name), remote.value(name),
        !local.contains(name) ? BranchState::RemoteOnly : !remote.contains(name) ? BranchState::LocalOnly
        : local.value(name) == remote.value(name) ? BranchState::Equal : BranchState::Different});
    return result;
}
QString branchStateText(BranchState state)
{
    switch (state) {
    case BranchState::LocalOnly: return QStringLiteral("仅本地存在");
    case BranchState::RemoteOnly: return QStringLiteral("仅远端存在");
    case BranchState::Equal: return QStringLiteral("一致");
    case BranchState::Different: return QStringLiteral("不一致");
    }
    return {};
}
QStringList logArguments(int offset, int count)
{
    return {QStringLiteral("log"), QStringLiteral("--topo-order"), QStringLiteral("--no-show-signature"), QStringLiteral("--no-decorate"),
        QStringLiteral("-z"), QStringLiteral("--format=%H%x00%P%x00%an%x00%aI%x00%s"),
        QStringLiteral("--skip=%1").arg(offset), QStringLiteral("--max-count=%1").arg(count), QStringLiteral("--stdin")};
}
QStringList fileArguments(const QString &commit, const QString &parent, bool patch)
{
    QStringList args{QStringLiteral("diff-tree"), QStringLiteral("--root"), QStringLiteral("--no-commit-id"), QStringLiteral("-r"),
        QStringLiteral("--no-ext-diff"), QStringLiteral("--no-textconv"), QStringLiteral("--no-color"), QStringLiteral("--no-relative"), QStringLiteral("-M")};
    if (patch) args << QStringLiteral("-p"); else args << QStringLiteral("--name-status") << QStringLiteral("-z");
    if (!parent.isEmpty()) args << parent;
    args << commit;
    return args;
}
QString literalFilePath(const QString &path)
{
    // diff-tree emits repository-root paths even when the binding is a subfolder.
    // Explicit top + literal preserves those paths and disables wildcard magic.
    return QStringLiteral(":(top,literal)") + path;
}
}
