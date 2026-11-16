#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QPixmap>
#include <QWidget>

namespace TestSupport {
// Screenshots are review artifacts, not assertions. Pixel checks stay in tests.
inline void capture(QWidget &widget, const QString &name)
{
    QCoreApplication::processEvents();
    if (qEnvironmentVariableIntValue("ORCHESTRATE_TEST_ARTIFACTS") != 1) return;
    const QDir directory(QCoreApplication::applicationDirPath() + "/artifacts");
    if (!directory.mkpath(".") || !widget.grab().save(directory.filePath(name + ".png")))
        qFatal("Could not save test screenshot: %s", qPrintable(name));
}

inline int repetitions(int regular, int extended)
{
    return qEnvironmentVariableIntValue("ORCHESTRATE_TEST_STRESS") == 1 ? extended : regular;
}
}
