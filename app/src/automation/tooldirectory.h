#pragma once
#include "commandlaunch.h"
#include "platform/fileaccess.h"

// The same directory inventory is produced locally and on the selected Linux
// host. The fixed POSIX reader needs no Python installation, writes no files,
// and receives the path as one argv value. NUL framing preserves spaces/quotes.
namespace ToolDirectory {
inline QString script()
{
    return QString::fromLatin1(R"SH(set -eu
p=$1
case "$p" in
  '~') p=$HOME ;;
  '~/'*) p="$HOME/${p#\~/}" ;;
  /*) ;;
  *) printf '%s\n' 'Use an absolute Linux directory or ~/.' >&2; exit 2 ;;
esac
if [ ! -d "$p" ]; then printf '%s\n' "Directory is unavailable: $p" >&2; exit 2; fi
CDPATH= cd -P -- "$p"
printf 'orchestrate-directory-v1\000R\000%s\000U\000%s\000' "$(pwd -P)" "$(id -un)"
if command -v python3 >/dev/null 2>&1; then printf 'P\000python3\000'; else printf 'P\000python\000'; fi
for f in .venv/bin/python venv/bin/python; do
  if [ -f "$f" ] && [ -x "$f" ]; then printf 'F\000%s\000' "$f"; fi
done
if [ -f orchestrate-tool.json ] && [ ! -L orchestrate-tool.json ]; then
  if [ "$(wc -c < orchestrate-tool.json)" -gt 1048576 ]; then printf '%s\n' 'Manifest exceeds 1 MiB.' >&2; exit 2; fi
  printf 'M\000'; cat -- orchestrate-tool.json; printf '\000'
fi
n=0
for f in *; do
  [ "$n" -lt 300 ] || break
  n=$((n+1))
  [ ! -L "$f" ] || continue
  if [ -d "$f" ]; then
    if [ -f "$f/__main__.py" ]; then printf 'F\000%s/__main__.py\000' "$f"; fi
    continue
  fi
  [ -f "$f" ] || continue
  printf 'F\000%s\000' "$f"
  if [ -x "$f" ]; then printf 'X\000%s\000' "$f"; fi
  case "$f" in
    settings.json)
      if [ "$(wc -c < "$f")" -le 65536 ]; then printf 'S\000'; cat -- "$f"; printf '\000'; fi ;;
    [Rr][Ee][Aa][Dd][Mm][Ee]*|*.sh|pyproject.toml|package.json)
      if [ "$(wc -c < "$f")" -le 131072 ]; then printf 'D\000%s\000' "$f"; cat -- "$f"; printf '\000'; fi ;;
  esac
done
)SH");
}
inline bool decode(const QByteArray &bytes, QJsonObject *inventory, QString *error)
{
    const auto fail = [&] { if (error) *error = QStringLiteral("目标目录返回的数据不完整或格式无效。"); return false; };
    if (bytes.size() > FileAccess::maxBytes || !bytes.endsWith('\0')) return fail();
    auto fields = bytes.split('\0');
    if (fields.takeFirst() != "orchestrate-directory-v1") return fail();
    fields.removeLast();
    QJsonObject result; QJsonArray files, documents, executables; QJsonObject defaults;
    for (int i = 0; i < fields.size();) {
        const auto tag = fields[i++];
        if (i >= fields.size()) return fail();
        const auto raw = fields[i++]; const auto value = QString::fromUtf8(raw);
        if (tag == "R") result["directory"] = value;
        else if (tag == "U") result["user"] = value;
        else if (tag == "P") result["python"] = value;
        else if (tag == "F") files.append(value);
        else if (tag == "X") executables.append(value);
        else if (tag == "M") result["manifest"] = value;
        else if (tag == "S") {
            const auto settings = QJsonDocument::fromJson(raw).object();
            for (auto it = settings.begin(); it != settings.end(); ++it)
                if (it.value().isDouble() || it.value().isBool()) defaults[it.key()] = it.value();
        } else if (tag == "D") {
            if (i >= fields.size()) return fail();
            documents.append(QJsonObject{{"name",value},{"text",QString::fromUtf8(fields[i++])}});
        } else return fail();
    }
    if (!PosixRegistration::validUser(result.value("user").toString())
        || !PosixRegistration::validPath(result.value("directory").toString())
        || !result.value("directory").toString().startsWith('/')) return fail();
    result["files"] = files; result["documents"] = documents;
    result["executables"] = executables; result["defaults"] = defaults;
    *inventory = result; return true;
}
inline CommandLaunch::Spec launch(const AppDatabase::AutomationTool &tool)
{
    auto target = tool; target.workingDirectory.clear(); target.environment = {};
    AppDatabase::ToolCommand command; command.executable = "sh";
    const QStringList args {"-c",script(),"orchestrate-directory",tool.workingDirectory};
    return CommandLaunch::build(target,command,args);
}
using Callback = std::function<void(bool,const QJsonObject &,const QString &)>;
inline void read(QObject *context, const AppDatabase::AutomationTool &tool, Callback callback)
{
    if (tool.targetType == "windows-local") {
        FileAccess::request(context,"describe-tool",tool.workingDirectory,[callback](bool ok,const QByteArray &bytes,const QString &error){
            const auto inventory = QJsonDocument::fromJson(bytes).object();
            callback(ok && !inventory.isEmpty(),inventory,error); });
    } else {
        auto *process = new QProcess;
        CommandLaunch::configure(process,launch(tool));
        ProcessCapture::capture(context,process,[callback](bool ok,const QByteArray &bytes,const QString &failure){
            QJsonObject inventory; QString error = failure;
            if (ok) ok = decode(bytes,&inventory,&error);
            callback(ok,inventory,error);
        },QStringLiteral("工具目录"),30000);
    }
}
}
