#include "data/appdatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QUuid>
#include <stdexcept>

namespace {
void require(bool condition, const QString &message)
{
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

AppDatabase::AutomationTool findTool(AppDatabase &database, int id)
{
    QString error;
    const auto tools = database.automationTools(0, &error);
    require(error.isEmpty(), error);
    for (const auto &tool : tools) {
        if (tool.id == id) {
            return tool;
        }
    }
    throw std::runtime_error("Tool missing after update");
}

void runCase(bool initiallyBuiltin, bool incomingBuiltin)
{
    int toolId = 0;
    int groupId = 0;
    QString error;
    const QString uniqueId = QUuid::createUuid().toString(QUuid::Id128);
    const bool expectedBuiltin = initiallyBuiltin || incomingBuiltin;
    {
        AppDatabase database;
        require(database.open(&error), error);
        require(database.addToolGroup(uniqueId, &groupId, &error), error);
        AppDatabase::AutomationTool original;
        original.externalId = uniqueId;
        original.name = QStringLiteral("Original tool");
        original.targetType = QStringLiteral("windows-local");
        original.registrationPath = QStringLiteral("original/manifest.json");
        original.statePath = QStringLiteral("original/state.json");
        original.builtin = initiallyBuiltin;
        original.groupId = groupId;
        original.refreshEnabled = true;
        original.refreshMode = QStringLiteral("daily");
        original.refreshIntervalSeconds = 900;
        original.dailyRefreshTime = QStringLiteral("21:30");
        require(database.addAutomationTool(original, &toolId, &error), error);

        // Parsing a manifest creates a fresh tool with default local settings.
        // Built-in synchronization is the only caller that explicitly sets true.
        AppDatabase::AutomationTool manifest;
        manifest.id = toolId;
        manifest.externalId = uniqueId;
        manifest.name = QStringLiteral("Updated tool");
        manifest.description = QStringLiteral("Updated description");
        manifest.targetType = QStringLiteral("windows-local");
        manifest.registrationPath = QStringLiteral("updated/manifest.json");
        manifest.statePath = QStringLiteral("updated/state.json");
        manifest.workingDirectory = QStringLiteral("updated");
        manifest.builtin = incomingBuiltin;
        require(database.updateAutomationTool(manifest, &error), error);
        require(findTool(database, toolId).builtin == expectedBuiltin,
                QStringLiteral("Initial update changed built-in identity incorrectly"));

        // Both reload and re-import use this update + replace-commands sequence.
        // Repeating it must not demote an existing (or newly adopted) built-in.
        for (int pass = 0; pass < 2; ++pass) {
            manifest.builtin = false;
            require(database.updateAutomationTool(manifest, &error), error);
            AppDatabase::ToolCommand command;
            command.name = QStringLiteral("Replacement %1").arg(pass);
            command.executable = QStringLiteral("not-executed-by-this-test");
            require(database.replaceToolCommands(toolId, {command}, &error), error);
            const auto saved = findTool(database, toolId);
            require(saved.builtin == expectedBuiltin, QStringLiteral("Manifest update demoted a built-in"));
            require(saved.groupId == groupId, QStringLiteral("Group was overwritten"));
            require(saved.refreshEnabled && saved.refreshMode == original.refreshMode
                        && saved.refreshIntervalSeconds == original.refreshIntervalSeconds
                        && saved.dailyRefreshTime == original.dailyRefreshTime,
                    QStringLiteral("Refresh preferences were overwritten"));
            require(saved.name == manifest.name && saved.description == manifest.description
                        && saved.registrationPath == manifest.registrationPath
                        && saved.statePath == manifest.statePath
                        && saved.workingDirectory == manifest.workingDirectory,
                    QStringLiteral("Manifest fields were not updated"));
            const auto commands = database.toolCommands(toolId, &error);
            require(error.isEmpty() && commands.size() == 1 && commands.first().name == command.name,
                    QStringLiteral("Commands were not replaced"));
        }
    }
    // Verify persistence after the connection is closed, not only in-memory state.
    AppDatabase reopened;
    require(reopened.open(&error), error);
    require(findTool(reopened, toolId).builtin == expectedBuiltin,
            QStringLiteral("Built-in identity was not persisted"));
    require(reopened.deleteAutomationTool(toolId, &error), error);
    require(reopened.deleteToolGroup(groupId, &error), error);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Never migrate real user data into a regression fixture.
    app.setOrganizationName(QStringLiteral("OrchestrateRegressionTests"));
    app.setApplicationName(QUuid::createUuid().toString(QUuid::Id128));
    QTextStream output(stdout);
    // CMake puts this executable in a dedicated directory, never next to Orchestrate.
    if (QFileInfo(QCoreApplication::applicationDirPath()).fileName() != QStringLiteral("builtin-identity-test")) {
        output << "Refusing to open a database outside the dedicated test directory." << Qt::endl;
        return 2;
    }
    int failures = 0;
    for (bool initial : {false, true}) {
        for (bool incoming : {false, true}) {
            try {
                runCase(initial, incoming);
                output << "PASS builtin=" << initial << ", incoming=" << incoming << Qt::endl;
            } catch (const std::exception &error) {
                ++failures;
                output << "FAIL builtin=" << initial << ", incoming=" << incoming
                       << ": " << error.what() << Qt::endl;
            }
        }
    }
    return failures == 0 ? 0 : 1;
}
