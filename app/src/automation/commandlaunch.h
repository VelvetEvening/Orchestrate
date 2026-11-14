#pragma once

#include "data/appdatabase.h"
#include "sshconnection.h"
#include "wslprocess.h"
#include <QProcessEnvironment>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Build argv once for both captured execution and the external console. Values
// never become shell syntax, including when the Windows console hosts SSH/WSL.
namespace CommandLaunch {
struct Spec {
    QString program;
    QStringList arguments;
    QString directory;
    QJsonObject environment;
};
inline QString posixQuote(QString value) { return "'" + value.replace("'", "'\"'\"'") + "'"; }
inline QString remoteLine(const AppDatabase::AutomationTool &tool, const QString &program, const QStringList &args)
{
    QStringList words {QStringLiteral("exec")};
    if (!tool.environment.isEmpty()) {
        words << QStringLiteral("env") << QStringLiteral("--");
        for (auto it = tool.environment.begin(); it != tool.environment.end(); ++it)
            words << posixQuote(it.key() + "=" + it.value().toString());
    }
    words << posixQuote(program);
    for (const auto &arg : args) words << posixQuote(arg);
    QString line = words.join(' ');
    if (!tool.workingDirectory.isEmpty()) line = "cd " + posixQuote(tool.workingDirectory) + " && " + line;
    return line;
}
inline Spec build(const AppDatabase::AutomationTool &tool, const AppDatabase::ToolCommand &command, const QStringList &args)
{
    Spec spec;
    if (tool.targetType == "wsl") {
        QString program = command.executable;
        QStringList arguments = args;
        if (!tool.environment.isEmpty()) {
            arguments.clear(); arguments << "--";
            for (auto it = tool.environment.begin(); it != tool.environment.end(); ++it)
                arguments << it.key() + "=" + it.value().toString();
            arguments << program; arguments.append(args); program = "env";
        }
        spec.program = WslProcess::program();
        spec.arguments = WslProcess::arguments(tool.wslDistribution, tool.wslUser, program, arguments,
            tool.workingDirectory.isEmpty() ? QStringLiteral("~") : tool.workingDirectory);
    } else if (tool.targetType == "ssh") {
        spec.program = "ssh";
        spec.arguments = SshConnection::arguments(tool.sshHost, tool.sshUser, remoteLine(tool, command.executable, args));
        if (command.interactive) {
            spec.arguments[0] = "-tt";
            spec.arguments.removeAt(2); // BatchMode=yes: interactive authentication belongs in the terminal.
            spec.arguments.removeAt(1);
        }
    } else {
        spec.program = command.executable;
        spec.arguments = args;
        spec.directory = tool.workingDirectory.isEmpty()
            ? (tool.registrationPath.isEmpty() ? QDir::homePath() : QFileInfo(tool.registrationPath).absolutePath())
            : tool.workingDirectory;
        // Resolve relative program paths against the configured working directory.
        if (QFileInfo(spec.program).isRelative() && (spec.program.contains('/') || spec.program.contains('\\')))
            spec.program = QDir(spec.directory).absoluteFilePath(spec.program);
        spec.environment = tool.environment;
    }
    return spec;
}
inline void configure(QProcess *process, const Spec &spec)
{
    process->setProgram(spec.program);
    process->setArguments(spec.arguments);
    process->setWorkingDirectory(spec.directory);
    if (!spec.environment.isEmpty()) {
        auto environment = QProcessEnvironment::systemEnvironment();
        for (auto it = spec.environment.begin(); it != spec.environment.end(); ++it)
            environment.insert(it.key(), it.value().toString());
        process->setProcessEnvironment(environment);
    }
}
inline QString windowsQuote(const QString &value)
{
    QString quoted = "\"";
    int slashes = 0;
    for (const auto c : value) {
        if (c == '\\') { ++slashes; continue; }
        quoted += QString(slashes * (c == '"' ? 2 : 1), '\\');
        slashes = 0;
        if (c == '"') quoted += '\\';
        quoted += c;
    }
    return quoted + QString(slashes * 2, '\\') + '"';
}
inline QString powershellQuote(QString value) { return "'" + value.replace("'", "''") + "'"; }
inline QString terminalScript(const Spec &spec)
{
    QStringList args;
    for (const auto &arg : spec.arguments) args << windowsQuote(arg);
    // ProcessStartInfo passes a native command line directly, avoiding Windows
    // PowerShell's native argument binder (which loses empty/quoted arguments).
    QString script = "$ErrorActionPreference='Stop'\ntry {\n"
        "$launchInfo = New-Object System.Diagnostics.ProcessStartInfo\n"
        "$launchInfo.UseShellExecute = $false\n"
        "$launchInfo.FileName = " + powershellQuote(spec.program) + "\n"
        "$launchInfo.Arguments = " + powershellQuote(args.join(' ')) + "\n";
    if (!spec.directory.isEmpty()) script += "$launchInfo.WorkingDirectory = " + powershellQuote(spec.directory) + "\n";
    for (auto it = spec.environment.begin(); it != spec.environment.end(); ++it)
        script += "$launchInfo.EnvironmentVariables[" + powershellQuote(it.key()) + "] = " + powershellQuote(it.value().toString()) + "\n";
    script += "$launchedProcess = [System.Diagnostics.Process]::Start($launchInfo)\n"
        "$launchedProcess.WaitForExit()\n"
        "Write-Host ('Exit code: ' + $launchedProcess.ExitCode)\n"
        "} catch { Write-Host $_ -ForegroundColor Red }\n";
    return script;
}
inline bool openTerminal(const Spec &spec, QString *error)
{
#ifdef Q_OS_WIN
    const QString script = terminalScript(spec);
    QByteArray utf16;
    utf16.reserve(script.size() * 2);
    for (const auto c : script) { utf16 += char(c.unicode() & 0xff); utf16 += char(c.unicode() >> 8); }
    if (utf16.toBase64().size() > 28000) { if (error) *error = QStringLiteral("终端命令过长，请减少参数或改用应用内执行。"); return false; }
    QProcess terminal;
    terminal.setProgram(QDir(qEnvironmentVariable("SystemRoot", "C:/Windows"))
        .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe")));
    terminal.setArguments({"-NoLogo", "-NoProfile", "-NoExit", "-EncodedCommand", QString::fromLatin1(utf16.toBase64())});
    terminal.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags &= ~CREATE_NO_WINDOW;
        args->flags |= CREATE_NEW_CONSOLE;
        args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_SHOWNORMAL;
    });
    if (terminal.startDetached()) return true;
    if (error) *error = terminal.errorString();
#else
    Q_UNUSED(spec)
    if (error) *error = QStringLiteral("外部终端目前仅支持 Windows 客户端。");
#endif
    return false;
}
}
