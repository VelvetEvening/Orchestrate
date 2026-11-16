#include "manifestimport.h"
#include "manifestparser.h"
#include "sshconnection.h"
#include "wslprocess.h"
#include "platform/fileaccess.h"
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QLabel *makeMuted(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}
} // namespace

bool ManifestImport::readLocal(QWidget *parent, Callback callback)
{
    const QString path = QFileDialog::getOpenFileName(
        parent, QStringLiteral("选择工具接入声明（orchestrate-tool.json）"), QString(),
        QStringLiteral("工具接入声明 (orchestrate-tool.json);;JSON 文件 (*.json);;所有文件 (*.*)"));
    if (path.isEmpty()) return false;
    FileAccess::request(parent, QStringLiteral("read"), path,
        [path, callback](bool ok, const QByteArray &data, const QString &error) {
            callback(ok, {data, path, {}, {}, {}, {}}, error);
        });
    return true;
}

bool ManifestImport::readSsh(QWidget *parent, const AppDatabase &database, Source *result)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("注册 SSH 服务器工具"));
    dialog.setObjectName(QStringLiteral("sshRegistrationDialog"));
    dialog.resize(640, 330);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(makeMuted(QStringLiteral("选择默认 SSH 配置中直接声明的服务器，再选择或填写登录用户。注册只读取声明，不运行工具命令。"), &dialog));
    auto *form = new QFormLayout;
    auto *host = new QComboBox(&dialog);
    host->setObjectName(QStringLiteral("sshHost"));
    host->setToolTip(QStringLiteral("列表只显示默认 ~/.ssh/config 直接声明的 Host，不列出 Include 引入的工具或扩展连接。实际 SSH 连接仍使用完整配置。"));
    host->setEditable(true);
    host->lineEdit()->setPlaceholderText(QStringLiteral("仅列出 ~/.ssh/config 直接声明的别名，也可输入主机"));
    auto *refresh = new QPushButton(QStringLiteral("刷新列表"), &dialog);
    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(host, 1);
    hostRow->addWidget(refresh);
    form->addRow(QStringLiteral("服务器别名"), hostRow);
    auto *user = new QComboBox(&dialog);
    user->setObjectName(QStringLiteral("sshUser"));
    user->setEditable(true);
    user->lineEdit()->setPlaceholderText(QStringLiteral("留空沿用 SSH 配置；也可选择或输入已有用户名"));
    form->addRow(QStringLiteral("登录用户"), user);
    auto *path = new QLineEdit(&dialog);
    path->setObjectName(QStringLiteral("sshManifestPath"));
    path->setPlaceholderText(QStringLiteral("~/work/my-tool 或 /home/user/my-tool/orchestrate-tool.json"));
    form->addRow(QStringLiteral("工具目录 / 声明"), path);
    layout->addLayout(form);
    auto *status = makeMuted(QString(), &dialog);
    status->setObjectName(QStringLiteral("sshRegistrationStatus"));
    status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    layout->addStretch();
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    auto *submit = new QPushButton(QStringLiteral("读取并注册"), &dialog);
    submit->setObjectName(QStringLiteral("sshRegisterSubmit"));
    submit->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(submit);
    layout->addLayout(buttons);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto config = SshConnection::Config::load();
    const auto updateUsers = [&] {
        const QString alias = host->currentText().trimmed();
        QStringList users {QString()};
        const QString hint = config.userHint(alias);
        if (!hint.isEmpty()) users << hint;
        for (const auto &tool : database.automationTools())
            if (tool.targetType == QStringLiteral("ssh") && tool.sshHost == alias
                && !tool.sshUser.isEmpty() && !users.contains(tool.sshUser)) users << tool.sshUser;
        const QSignalBlocker blocker(user);
        user->clear();
        user->addItems(users);
        user->setCurrentIndex(0); // empty means let SSH evaluate its real config, including Match
        status->setText(hint.isEmpty()
            ? QStringLiteral("用户可留空使用 SSH 配置。注册成功后显示并保存实际用户名；这里不会创建服务器账号。")
            : QStringLiteral("配置中的用户提示：%1。留空由 SSH 决定实际用户，或显式选择；不会创建账号。").arg(hint));
    };
    const auto loadAliases = [&] {
        const QString previous = host->currentText();
        config = SshConnection::Config::load();
        {
            const QSignalBlocker blocker(host);
            host->clear();
            host->addItems(config.aliases);
            if (!previous.isEmpty()) host->setCurrentText(previous);
        }
        updateUsers();
        if (!config.warnings.isEmpty()) status->setText(config.warnings.join(QLatin1Char('\n')));
        else if (config.aliases.isEmpty()) status->setText(QStringLiteral("默认 ~/.ssh/config 中没有直接声明的 Host 别名。可输入主机；Include 中的连接不会加入列表。"));
    };
    QObject::connect(host, &QComboBox::currentTextChanged, &dialog, updateUsers);
    QObject::connect(refresh, &QPushButton::clicked, &dialog, loadAliases);
    loadAliases();
    QString registeredHost, registeredUser, registeredPath;
    QByteArray manifestData;
    QObject::connect(submit, &QPushButton::clicked, &dialog, [&] {
        const QString alias = host->currentText().trimmed();
        const QString username = user->currentText().trimmed();
        const QString source = path->text().trimmed();
        if (!SshConnection::validHost(alias) || alias.contains(QLatin1Char('@'))
            || (!username.isEmpty() && !PosixRegistration::validUser(username))) {
            status->setText(QStringLiteral("请填写服务器别名或主机，用户名在第二行单独填写；不能拼入 SSH 选项。"));
            return;
        }
        if (!PosixRegistration::validPath(source) || !(source.startsWith(QLatin1Char('/'))
            || source == QStringLiteral("~") || source.startsWith(QStringLiteral("~/")))) {
            status->setText(QStringLiteral("请使用服务器上的 Linux 绝对路径或 ~/ 路径，可填写工具目录。"));
            return;
        }
        for (QWidget *widget : QList<QWidget *> {host, user, path, refresh, submit}) widget->setEnabled(false);
        status->setText(QStringLiteral("正在连接 %1 并读取声明…请提前完成主机密钥确认和免交互认证。").arg(alias));
        const QString command = QStringLiteral("sh -c %1 orchestrate-register %2")
            .arg(PosixRegistration::quote(PosixRegistration::registrationScript()), PosixRegistration::quote(source));
        SshConnection::capture(&dialog, alias, username, command,
            [&, alias](bool ok, const QByteArray &bytes, const QString &error) {
                QString readError = error;
                if (ok) {
                    ok = PosixRegistration::registrationResult(bytes, &registeredUser, &registeredPath, &manifestData);
                    if (!ok) readError = QStringLiteral("服务器返回了无效的路径或用户名，请检查登录脚本是否向 stdout 输出额外文本。");
                }
                ManifestParser::Manifest manifest;
                if (ok) ok = ManifestParser::parse(manifestData, registeredPath, alias, &manifest, &readError, {}, {}, registeredUser);
                if (ok) { registeredHost = alias; dialog.accept(); return; }
                for (QWidget *widget : QList<QWidget *> {host, user, path, refresh, submit}) widget->setEnabled(true);
                status->setText(readError);
            });
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    *result = {manifestData, registeredPath, registeredHost, {}, {}, registeredUser};
    return true;
}

bool ManifestImport::readWsl(QWidget *parent, const AppDatabase &database, Source *result)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("注册 WSL 工具"));
    dialog.setObjectName(QStringLiteral("wslRegistrationDialog"));
    dialog.resize(600, 330);
    auto *layout = new QVBoxLayout(&dialog);
    auto *help = makeMuted(QStringLiteral("选择工具所在的 WSL 发行版。注册只读取接入声明，不运行工具命令。"), &dialog);
    layout->addWidget(help);
    auto *form = new QFormLayout;
    auto *distribution = new QComboBox(&dialog);
    distribution->setObjectName(QStringLiteral("wslDistribution"));
    distribution->setEditable(true);
    auto *refresh = new QPushButton(QStringLiteral("刷新列表"), &dialog);
    auto *distributionRow = new QHBoxLayout;
    distributionRow->addWidget(distribution, 1);
    distributionRow->addWidget(refresh);
    form->addRow(QStringLiteral("发行版"), distributionRow);
    auto *user = new QComboBox(&dialog);
    user->setObjectName(QStringLiteral("wslUser"));
    user->setEditable(true);
    user->lineEdit()->setPlaceholderText(QStringLiteral("选择或输入已有用户；留空使用发行版默认用户"));
    auto *refreshUsers = new QPushButton(QStringLiteral("读取用户"), &dialog);
    refreshUsers->setObjectName(QStringLiteral("wslRefreshUsers"));
    auto *userRow = new QHBoxLayout;
    userRow->addWidget(user, 1);
    userRow->addWidget(refreshUsers);
    form->addRow(QStringLiteral("Linux 用户"), userRow);
    auto *path = new QLineEdit(&dialog);
    path->setObjectName(QStringLiteral("wslManifestPath"));
    path->setPlaceholderText(QStringLiteral("~/work/my-tool 或 /home/user/my-tool/orchestrate-tool.json"));
    form->addRow(QStringLiteral("工具目录 / 声明"), path);
    layout->addLayout(form);
    auto *status = makeMuted(QStringLiteral("支持 ~/ 和 Linux 绝对路径；目录会自动补上 orchestrate-tool.json。"), &dialog);
    status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    layout->addStretch();
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    auto *submit = new QPushButton(QStringLiteral("读取并注册"), &dialog);
    submit->setObjectName(QStringLiteral("wslRegisterSubmit"));
    submit->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(submit);
    layout->addLayout(buttons);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    QObject::connect(distribution, &QComboBox::currentTextChanged, &dialog, [&database, distribution, user] {
        const QSignalBlocker blocker(user);
        user->clear();
        user->addItem(QString());
        for (const auto &tool : database.automationTools())
            if (tool.targetType == QStringLiteral("wsl") && tool.wslDistribution == distribution->currentText()
                && !tool.wslUser.isEmpty() && user->findText(tool.wslUser) < 0) user->addItem(tool.wslUser);
        user->setCurrentIndex(0);
    });
    QObject::connect(refreshUsers, &QPushButton::clicked, &dialog, [&, distribution, user, refreshUsers, refresh, submit, status] {
        const QString distro = distribution->currentText().trimmed();
        if (!WslProcess::validDistribution(distro)) {
            status->setText(QStringLiteral("请先选择一个有效的发行版。"));
            return;
        }
        for (QWidget *widget : QList<QWidget *> {distribution, user, refreshUsers, refresh, submit}) widget->setEnabled(false);
        status->setText(QStringLiteral("正在读取所选发行版的已有用户（会启动该发行版），不会创建或修改账号…"));
        WslProcess::capture(&dialog, WslProcess::arguments(distro, {}, QStringLiteral("sh"),
            {QStringLiteral("-c"), QStringLiteral("id -un && cat /etc/passwd")}),
            [distribution, user, refreshUsers, refresh, submit, status](bool ok, const QByteArray &bytes, const QString &error) {
                for (QWidget *widget : QList<QWidget *> {distribution, user, refreshUsers, refresh, submit}) widget->setEnabled(true);
                if (!ok) { status->setText(error); return; }
                const QString previous = user->currentText();
                const auto names = WslProcess::users(bytes);
                user->clear();
                user->addItem(QString());
                user->addItems(names);
                user->setCurrentText(previous.isEmpty() ? names.value(0) : previous);
                status->setText(QStringLiteral("已读取 %1 个用户；可选择或手动输入已有用户名。注册后固定并显示实际用户，不会创建账号。").arg(names.size()));
            });
    });
    QString resolvedDistribution, resolvedUser, resolvedPath;
    QByteArray manifestData;
    const auto discover = [&dialog, distribution, refresh, refreshUsers, submit, status] {
        refresh->setEnabled(false);
        refreshUsers->setEnabled(false);
        submit->setEnabled(false);
        status->setText(QStringLiteral("正在读取已安装的 WSL 发行版…"));
        WslProcess::capture(&dialog, {QStringLiteral("--list"), QStringLiteral("--quiet")},
            [distribution, refresh, refreshUsers, submit, status](bool ok, const QByteArray &bytes, const QString &error) {
                const QString previous = distribution->currentText();
                const QStringList names = ok ? WslProcess::distributions(bytes) : QStringList();
                distribution->clear();
                distribution->addItems(names);
                if (!previous.isEmpty()) distribution->setCurrentText(previous);
                refresh->setEnabled(true);
                refreshUsers->setEnabled(true);
                submit->setEnabled(true);
                status->setText(!ok ? error : names.isEmpty()
                    ? QStringLiteral("未找到发行版。请先安装并初始化 WSL，或输入已有发行版名称后重试。")
                    : QStringLiteral("填写 Linux 工具目录或声明路径；读取可能启动所选发行版。"));
            });
    };
    QObject::connect(refresh, &QPushButton::clicked, &dialog, discover);
    QObject::connect(submit, &QPushButton::clicked, &dialog, [&, distribution, user, path, submit, refresh, status] {
        const QString distro = distribution->currentText().trimmed();
        const QString username = user->currentText().trimmed();
        const QString source = path->text().trimmed();
        if (!WslProcess::validDistribution(distro) || (!username.isEmpty() && !WslProcess::validName(username))) {
            status->setText(QStringLiteral("请填写有效的发行版和 Linux 用户名，不能以 - 开头或包含控制字符；用户名不能含空白。"));
            return;
        }
        if (!WslProcess::validPath(source) || !(source.startsWith(QLatin1Char('/'))
            || source == QStringLiteral("~") || source.startsWith(QStringLiteral("~/")))) {
            status->setText(QStringLiteral("请使用 Linux 绝对路径或 ~/ 路径，不支持 Windows 路径或 ~其他用户。"));
            return;
        }
        distribution->setEnabled(false);
        refreshUsers->setEnabled(false);
        user->setEnabled(false);
        path->setEnabled(false);
        submit->setEnabled(false);
        refresh->setEnabled(false);
        status->setText(QStringLiteral("正在 WSL 中读取接入声明…首次启动可能需要一些时间。"));
        const auto args = WslProcess::arguments(distro, username, QStringLiteral("sh"),
            {QStringLiteral("-c"), WslProcess::registrationScript(), QStringLiteral("orchestrate-register"), source});
        WslProcess::capture(&dialog, args, [&, distro, distribution, user, path, submit, refresh, status]
                            (bool ok, const QByteArray &bytes, const QString &error) {
            QString readError = error;
            if (ok) {
                ok = WslProcess::registrationResult(bytes, &resolvedUser, &resolvedPath, &manifestData);
                if (!ok) readError = QStringLiteral("WSL 返回了无效的路径或用户信息。");
            }
            ManifestParser::Manifest manifest;
            if (ok) ok = ManifestParser::parse(manifestData, resolvedPath, {}, &manifest, &readError, distro, resolvedUser);
            if (ok) {
                resolvedDistribution = distro;
                dialog.accept();
                return;
            }
            distribution->setEnabled(true);
            refreshUsers->setEnabled(true);
            user->setEnabled(true);
            path->setEnabled(true);
            submit->setEnabled(true);
            refresh->setEnabled(true);
            status->setText(readError);
        });
    });
    discover();
    if (dialog.exec() != QDialog::Accepted) return false;
    *result = {manifestData, resolvedPath, {}, resolvedDistribution, resolvedUser, {}};
    return true;
}
