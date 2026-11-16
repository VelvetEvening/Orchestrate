#include "widgets/headingtextedit.h"
#include "widgets/mathpreview.h"
#include "widgets/mathtext.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTextEdit>
#include <QTextBlock>
#include <QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
const QString attention = QString::fromUtf8(R"(# 注意力与 KV Cache
当前位置是 \(t\)，查询向量为 \(q_t\)，历史键和值为 $K_{1:t}$、$V_{1:t}$。

\[
o_t=\operatorname{softmax}\left(\frac{q_tK_{1:t}^{\mathsf T}}{\sqrt{d_k}}\right)V_{1:t}
\]

## 对 V 加权求和
\[\boxed{o_t=\sum_{j=1}^{t}\alpha_{t,j}v_j}\]

\[\underbrace{o_t^{(1)},\ldots,o_t^{(H)}}_{\text{多个注意力头的输出}}\;\xrightarrow{\text{拼接，再乘 }W_O}\;\text{多头注意力输出}\]

$$\begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}$$

代码示例 `\(x_i\)` 保留原样。
)");
int imageCount(QTextEdit *preview) {
    int count = 0;
    for (auto block = preview->document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) if (it.fragment().charFormat().isImageFormat()) ++count;
    return count;
}
}
class MathPreviewTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts"));
        for (const auto &name : {"msyh.ttc", "msyhbd.ttc", "segoeui.ttf"}) QFontDatabase::addApplicationFont(fonts.filePath(name));
        QApplication::setFont(QFont("Microsoft YaHei", 10));
        // Fresh Windows runners can spend several seconds discovering fallback
        // fonts. Warm that OS cache before assertions against the preview's
        // intentional five-second worker deadline; keep all rendering checks.
        QProcess warmup;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "windows");
        warmup.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
        warmup.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        warmup.start(QDir(QCoreApplication::applicationDirPath()).filePath("OrchestrateMath.exe"), {});
        QVERIFY(warmup.waitForStarted());
        warmup.write(QString::fromUtf8(R"({"size":16,"formulas":[{"code":"\\text{中文字体预热}","display":true}]})").toUtf8());
        warmup.closeWriteChannel();
        QVERIFY(warmup.waitForFinished(30000));
        QCOMPARE(warmup.exitStatus(), QProcess::NormalExit);
        QCOMPARE(warmup.exitCode(), 0);
        const auto rendered = QJsonDocument::fromJson(warmup.readAllStandardOutput().trimmed()).object();
        QVERIFY2(!rendered.value("png").toString().isEmpty(), warmup.readAllStandardError().constData());
    }
    void delimitersAndCode() {
        QCOMPARE(MathText::spans(R"(\(x\) \[y\] $z$ $$a$$)").size(), 4);
        for (const auto &text : {R"(`$x$`)", R"(``\(x\)``)", "```tex\n$x$\n```", "~~~\n$x$\n~~~",
                 R"(    $x$)", R"(\$x\$)", R"(\\(x\))", "$5 and $10", "$ x $", "\\(unfinished", "$x\ny$"})
            QVERIFY2(MathText::spans(QString::fromUtf8(text)).isEmpty(), text);
        QCOMPARE(MathText::spans("```\n$x$\n```\n$y$").size(), 1);
        QCOMPARE(MathText::spans("$5 and $10, then $x$").size(), 1);
        QCOMPARE(MathText::spans("\\[\n    x+y\n\\]").size(), 1);
        QCOMPARE(MathText::spans(QString("$x$ ").repeated(1000)).size(), 128);
    }
    void renderCopyZoomAndScreenshots() {
        HeadingTextEdit editor;
        editor.resize(900, 680); editor.setTextPointSize(13); editor.show();
        auto *preview = editor.findChild<QTextEdit *>("headingPreview");
        auto *math = editor.findChild<MathPreview *>();
        QSignalSpy ready(math, &MathPreview::ready), changed(&editor, &HeadingTextEdit::textChanged);
        QElapsedTimer timer; timer.start();
        editor.setPlainText(attention); editor.setPreviewMode(true);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 8000);
        qInfo() << "attention-preview-ms" << timer.elapsed();
        QCOMPARE(imageCount(preview), 8);
        QCOMPARE(editor.toPlainText(), attention);
        QCOMPARE(changed.size(), 1);
        preview->selectAll(); preview->copy();
        QVERIFY(QApplication::clipboard()->text().contains(R"(\operatorname{softmax})"));
        QVERIFY(QApplication::clipboard()->text().contains(R"(\(q_t\))"));
        QVERIFY(!QApplication::clipboard()->text().contains(QChar::ObjectReplacementCharacter));
        const auto copied = QApplication::clipboard()->text();
        const auto dir = QCoreApplication::applicationDirPath() + "/artifacts";
        QDir().mkpath(dir);
        preview->moveCursor(QTextCursor::Start);
        QVERIFY(editor.grab().save(dir + "/math-preview.png"));
        editor.resize(480, 440);
        QCoreApplication::processEvents();
        QVERIFY(editor.grab().save(dir + "/math-preview-compact.png"));
        editor.setTextPointSize(18);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 8000);
        QCOMPARE(imageCount(preview), 8);
        preview->selectAll(); preview->copy(); QCOMPARE(QApplication::clipboard()->text(), copied);
        QCOMPARE(changed.size(), 1);
        editor.setPreviewMode(false); editor.setPreviewMode(true);
        QCOMPARE(imageCount(preview), 8);
        QTest::qWait(200); QCOMPARE(ready.size(), 2);
        QVERIFY(!math->pending());
    }
    void invalidAndRecovery() {
        MathPreview renderer(this);
        QSignalSpy ready(&renderer, &MathPreview::ready);
        const QString input = R"($\unknowncommand{x}$ $\frac{a}{$ $\newcommand{\a}{\a}\a$ $\includegraphics{C:/x}$ $\magnification{100000}$ $x^2$)";
        renderer.request(input, 16);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 8000);
        QCOMPARE(renderer.images().size(), 1);
        QVERIFY(renderer.images().contains("$x^2$"));
        renderer.request(R"(\[\frac{a}{b}\])", 20);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 8000);
        QCOMPARE(renderer.images().size(), 1);
    }
    void rapidChangesAndIdle() {
        HeadingTextEdit editor; editor.show();
        auto *math = editor.findChild<MathPreview *>();
        auto *preview = editor.findChild<QTextEdit *>("headingPreview");
        QSignalSpy ready(math, &MathPreview::ready);
        editor.setPreviewMode(true);
        for (int i = 0; i < 25; ++i) editor.setPlainText(QString("$x_{%1}$").arg(i));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 8000);
        QCOMPARE(imageCount(preview), 1); QVERIFY(math->images().contains("$x_{24}$"));
        editor.setPlainText(attention); QTest::qWait(90);
        editor.setPlainText("ordinary replacement");
        QTest::qWait(400);
        QCOMPARE(preview->toPlainText(), QString("ordinary replacement"));
        QVERIFY(!math->pending());
        QVERIFY(math->images().isEmpty());
        const auto completions = ready.size();
        QTest::qWait(400); QCOMPARE(ready.size(), completions);
        QElapsedTimer timer; timer.start();
        editor.setPlainText(QString("ordinary long text\n").repeated(10000));
        QVERIFY(timer.elapsed() < 2000); QVERIFY(!math->pending());
        qInfo() << "plain-180k-preview-ms" << timer.elapsed();
    }
    void workerCrashDoesNotCrashEditor() {
        HeadingTextEdit editor;
        auto *math = editor.findChild<MathPreview *>();
        QSignalSpy ready(math, &MathPreview::ready);
        editor.setPlainText(attention); editor.setPreviewMode(true);
        QTRY_VERIFY_WITH_TIMEOUT(!math->findChildren<QProcess *>().isEmpty(), 2000);
        auto *worker = math->findChildren<QProcess *>().first();
        worker->kill();
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 8000);
        QCOMPARE(editor.toPlainText(), attention);
        editor.setPlainText("$a+b$");
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 8000);
        QCOMPARE(imageCount(editor.findChild<QTextEdit *>("headingPreview")), 1);
    }
    void resourceAndLongTextRegression() {
        HeadingTextEdit editor; editor.show();
        auto *math = editor.findChild<MathPreview *>();
        QSignalSpy ready(math, &MathPreview::ready);
        editor.setPlainText(attention); editor.setPreviewMode(true);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 8000);
#ifdef Q_OS_WIN
        auto memory = [] { PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters));
            return qint64(counters.PrivateUsage); };
        const auto before = memory();
#endif
        for (int i = 0; i < 30; ++i) {
            editor.setPlainText(attention + QString("\n$x_{%1}$").arg(i));
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), i + 2, 8000);
        }
        editor.setPlainText("plain");
        QTest::qWait(200);
        QVERIFY(!math->pending()); QVERIFY(math->images().isEmpty());
        for (auto *process : editor.findChildren<QProcess *>()) QCOMPARE(process->state(), QProcess::NotRunning);
#ifdef Q_OS_WIN
        qInfo() << "private-memory-growth-after-30-previews-bytes" << memory() - before;
        QVERIFY(memory() - before < 24 * 1024 * 1024);
        auto cpu = [] { FILETIME created, exited, kernel, user;
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
            return ((quint64(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime)
                + ((quint64(user.dwHighDateTime) << 32) | user.dwLowDateTime); };
        const auto cpuBefore = cpu(); QTest::qWait(1500);
        qInfo() << "idle-cpu-ms-over-1500ms" << (cpu() - cpuBefore) / 10000;
        QVERIFY(cpu() - cpuBefore < 100 * 10000);
#endif
        QElapsedTimer timer; timer.start();
        const auto longSlashes = QString(100000, '\\') + "(unterminated";
        QVERIFY(MathText::spans(longSlashes).isEmpty());
        QVERIFY(timer.elapsed() < 500);
        qInfo() << "scan-100k-backslashes-ms" << timer.elapsed();
        auto *temporary = new HeadingTextEdit;
        temporary->setPlainText(attention); temporary->setPreviewMode(true);
        QTest::qWait(85); delete temporary; QTest::qWait(250);
        for (auto *process : qApp->findChildren<QProcess *>()) QCOMPARE(process->state(), QProcess::NotRunning);
    }
};
QTEST_MAIN(MathPreviewTest)
#include "test_math_preview.moc"
