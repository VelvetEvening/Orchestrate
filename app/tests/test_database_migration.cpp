#include "data/appdatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>
#include <stdexcept>

namespace {
void require(bool ok, const QString &message)
{
    if (!ok) throw std::runtime_error(message.toStdString());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), file.errorString());
    return file.readAll();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (QFileInfo(app.applicationDirPath()).fileName() != QStringLiteral("database-migration-test")) return 2;
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("OrchestrateMigrationTests-%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
    app.setApplicationName(QStringLiteral("Orchestrate"));
    const QString currentDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString organizationDirectory = QFileInfo(currentDirectory).dir().absolutePath();
    const QString portablePath = QDir(app.applicationDirPath()).filePath(QStringLiteral("data/orchestrate.sqlite3"));
    QTextStream output(stdout);
    int result = 0;
    {
        QDir().mkpath(organizationDirectory);
        QTemporaryDir previousApp(QDir(organizationDirectory).filePath(QStringLiteral("previous-name-XXXXXX")));
        QTemporaryDir otherApp(QDir(organizationDirectory).filePath(QStringLiteral("another-name-XXXXXX")));
        try {
            require(previousApp.isValid() && otherApp.isValid(), QStringLiteral("Cannot create isolated legacy directories"));
            require(!QFileInfo::exists(portablePath) || QFile::remove(portablePath), QStringLiteral("Cannot reset fixture"));
            QString error;
            AppDatabase::DiaryEntry original;
            const QDate date(2026, 9, 28);
            {
                AppDatabase seed;
                require(seed.open(&error), error);
                require(seed.addDiaryEntry(date, QStringLiteral("Original summary"), QStringLiteral("Original body\n  keep spacing"), &error), error);
                original = seed.diaryEntries(date).first();
            }
            const QString legacyPath = previousApp.filePath(QStringLiteral("orchestrate.sqlite3"));
            require(QFile::copy(portablePath, legacyPath), QStringLiteral("Cannot seed old application data"));
            const auto legacyBytes = readFile(legacyPath);
            require(QFile::remove(portablePath), QStringLiteral("Cannot remove seed"));
            {
                AppDatabase migrated;
                require(migrated.open(&error), error);
                const auto restored = migrated.diaryEntry(original.id);
                require(restored.title == original.title && restored.content == original.content
                            && restored.date == original.date && restored.createdAt == original.createdAt
                            && restored.updatedAt == original.updatedAt,
                        QStringLiteral("Renamed application failed to recover the complete diary"));
                require(readFile(legacyPath) == legacyBytes, QStringLiteral("Migration modified its source"));
                require(migrated.addDiaryEntry(date, QStringLiteral("Portable-only entry"), &error), error);
            }
            {
                AppDatabase reopened;
                require(reopened.open(&error), error);
                require(reopened.diaryEntries(date).size() == 2, QStringLiteral("Existing portable data was overwritten"));
            }
            output << "PASS previous-name recovery and portable-data precedence" << Qt::endl;

            const QString secondPath = otherApp.filePath(QStringLiteral("orchestrate.sqlite3"));
            require(QFile::copy(legacyPath, secondPath), QStringLiteral("Cannot seed ambiguous history"));
            require(QFile::remove(portablePath), QStringLiteral("Cannot reset portable fixture"));
            {
                AppDatabase ambiguous;
                error.clear();
                require(!ambiguous.open(&error) && !error.isEmpty() && !QFileInfo::exists(portablePath),
                        QStringLiteral("Ambiguous databases must not be chosen silently"));
            }
            output << "PASS ambiguous history leaves sources untouched" << Qt::endl;

            QDir().mkpath(currentDirectory);
            const QString currentPath = QDir(currentDirectory).filePath(QStringLiteral("orchestrate.sqlite3"));
            require(QFile::copy(legacyPath, currentPath), QStringLiteral("Cannot seed current-name data"));
            {
                AppDatabase current;
                require(current.open(&error), error);
                require(current.diaryEntry(original.id).content == original.content, QStringLiteral("Current-name source did not take precedence"));
            }
            require(readFile(legacyPath) == legacyBytes && readFile(secondPath) == legacyBytes,
                    QStringLiteral("Legacy source was modified"));
            require(QFile::remove(currentPath), QStringLiteral("Cannot clean current-name fixture"));
            output << "PASS current-name data takes precedence" << Qt::endl;
        } catch (const std::exception &error) {
            output << "FAIL " << error.what() << Qt::endl;
            result = 1;
        }
    }
    QDir().rmdir(currentDirectory);
    QDir().rmdir(organizationDirectory);
    return result;
}
