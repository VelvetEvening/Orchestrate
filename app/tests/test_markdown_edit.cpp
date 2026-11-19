#include "widgets/headingtextedit.h"
#include "widgets/markdownedit.h"
#include "widgets/mathpreview.h"
#include "widgets/textlayoutguard.h"
#include "ui/theme.h"
#include "test_skin_fixture.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QMenu>
#include <QContextMenuEvent>
#include <QInputMethodEvent>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextTable>
#include <QTest>

class MarkdownEditTest : public QObject {
    Q_OBJECT
private:
    static int images(QTextDocument *doc) {
        int n = 0; for (auto b = doc->begin(); b.isValid(); b = b.next())
            for (auto i = b.begin(); !i.atEnd(); ++i) if (i.fragment().charFormat().isImageFormat()) ++n;
        return n;
    }
private slots:
    void initTestCase() {
        const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts"));
        for (const auto &name : {"msyh.ttc", "msyhbd.ttc", "segoeui.ttf", "consola.ttf"}) QFontDatabase::addApplicationFont(fonts.filePath(name));
        UiTheme::install(*qApp);
    }
    void themeSwitchPreservesEditorState_data() {
        QTest::addColumn<bool>("reading");
        QTest::addColumn<bool>("sourceMode");
        QTest::addColumn<qreal>("pointSize");
        QTest::newRow("body-reading") << true << false << qreal(20);
        QTest::newRow("summary-reading") << true << false << qreal(24);
        QTest::newRow("live-editing") << false << false << qreal(19.5);
        QTest::newRow("source-editing") << false << true << qreal(21);
        QTest::newRow("inherited-font") << false << false << qreal(0);
    }
    void themeSwitchPreservesEditorState() {
        QFETCH(bool, reading);
        QFETCH(bool, sourceMode);
        QFETCH(qreal, pointSize);
        UiTheme::setInstalledThemes({TestSkinFixture::theme()});
        const auto restoreTheme = qScopeGuard([] {
            UiTheme::setInstalledThemes({});
            UiTheme::apply(*qApp, "mist", "standard");
        });
        UiTheme::apply(*qApp, "mist", "standard");
        HeadingTextEdit editor;
        editor.resize(640, 480);
        editor.setPlainText(QStringLiteral("# 阅读标题\n\n正文阅读与未保存草稿。\n\n[主题链接](https://example.com)\n\n`行内代码`\n\n- 项目记录"));
        if (pointSize > 0) editor.setTextPointSize(pointSize);
        editor.setPreviewMode(reading);
        editor.setSourceMode(sourceMode);
        editor.show();
        QCoreApplication::processEvents();
        auto *source = editor.findChild<QPlainTextEdit *>(QStringLiteral("headingSource"));
        auto *view = editor.findChild<MarkdownEdit *>(QStringLiteral("headingPreview"));
        QVERIFY(source && view);
        if (pointSize == 0) pointSize = source->font().pointSizeF();
        source->moveCursor(QTextCursor::End);
        source->insertPlainText(QStringLiteral(" 未保存"));
        view->refresh();
        const QString draft = editor.toPlainText();
        const int anchor = draft.indexOf(QStringLiteral("正文阅读"));
        const int position = anchor + 4;
        auto selection = source->textCursor();
        selection.setPosition(anchor);
        selection.setPosition(position, QTextCursor::KeepAnchor);
        source->setTextCursor(selection);
        view->setSourceCursor(position, anchor);
        QSignalSpy zoomChanges(&editor, &HeadingTextEdit::textPointSizeChanged);
        const QList<QPair<QString, QString>> appearances {
            {"mist", "compact"}, {"paper", "compact"}, {"paper", "standard"},
            {TestSkinFixture::Id, "standard"}, {TestSkinFixture::Id, "compact"}, {"mist", "standard"}
        };
        for (const auto &[palette, layout] : appearances) {
            UiTheme::apply(*qApp, palette, layout);
            QCoreApplication::processEvents();
            QCOMPARE(UiTheme::paletteId(), palette);
            QCOMPARE(source->font().pointSizeF(), pointSize);
            QCOMPARE(view->font().pointSizeF(), pointSize);
            QCOMPARE(view->document()->defaultFont().pointSizeF(), pointSize);
            const auto paragraph = view->document()->find(QStringLiteral("正文阅读"));
            QVERIFY(!paragraph.isNull());
            QCOMPARE(paragraph.charFormat().fontPointSize(), pointSize);
            QCOMPARE(view->document()->find(QStringLiteral("主题链接")).charFormat().foreground().color(), QColor(UiTheme::Accent));
            QCOMPARE(view->document()->find(QStringLiteral("行内代码")).charFormat().background().color(), QColor(UiTheme::Sidebar));
            QCOMPARE(editor.toPlainText(), draft);
            QCOMPARE(source->textCursor().position(), position);
            QCOMPARE(source->textCursor().anchor(), anchor);
            QCOMPARE(view->sourcePosition(view->textCursor().position()), position);
            QCOMPARE(view->sourcePosition(view->textCursor().anchor()), anchor);
            QVERIFY(source->document()->isUndoAvailable());
        }
        QCOMPARE(zoomChanges.count(), 0);
        source->undo();
        QVERIFY(!editor.toPlainText().endsWith(QStringLiteral(" 未保存")));
    }
    void firstHeadingUsesRenderingUntilCursorActivation() {
        HeadingTextEdit editor;
        editor.resize(700, 460);
        editor.show();
        const QString original = QStringLiteral("## 标题\n\n正文 **重点**\n\n## 第二节\n这里是内容");
        editor.setPlainText(original);
        auto *live = editor.findChild<MarkdownEdit *>();
        live->setFocus();
        QCoreApplication::processEvents();
        editor.grab().save(QCoreApplication::applicationDirPath() + "/heading-on-open.png");
        QCOMPARE(live->document()->firstBlock().text(), QStringLiteral("标题"));
        QCOMPARE(editor.toPlainText(), original);

        // Clicking the existing visual caret position must also activate it.
        QTextCursor heading(live->document());
        heading.setPosition(0);
        QTest::mouseClick(live->viewport(), Qt::LeftButton, Qt::NoModifier, live->cursorRect(heading).center());
        QCOMPARE(live->document()->firstBlock().text(), QStringLiteral("## 标题"));
        QCOMPARE(live->sourcePosition(live->textCursor().position()), 3);
        editor.grab().save(QCoreApplication::applicationDirPath() + "/heading-active.png");
        QTest::keyClicks(live, "new ");
        QCOMPARE(editor.toPlainText(), QString(original).insert(3, "new "));
        QTest::keyClick(live, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor.toPlainText(), original);

        live->setSourceCursor(original.indexOf(QStringLiteral("正文")) + 1);
        QCOMPARE(live->document()->firstBlock().text(), QStringLiteral("标题"));
        QVERIFY(live->toPlainText().contains(QStringLiteral("正文 **重点**")));
        QVERIFY(!live->toPlainText().contains(QStringLiteral("## 第二节")));
        QCOMPARE(editor.toPlainText(), original);

        editor.setPlainText(original);
        QCoreApplication::processEvents();
        QCOMPARE(live->document()->firstBlock().text(), QStringLiteral("标题"));
        editor.resize(420, 380);
        editor.grab().save(QCoreApplication::applicationDirPath() + "/heading-on-open-compact.png");
        QTest::keyClick(live, Qt::Key_Right);
        QCOMPARE(live->document()->firstBlock().text(), QStringLiteral("## 标题"));
        QCOMPARE(editor.toPlainText(), original);
    }
    void sourceAndIncrementalEditing() {
        HeadingTextEdit editor; editor.resize(700, 500); editor.show();
        const QString original = QStringLiteral("# 标题\n\n**粗体** 和 `代码`\n\n最后一段");
        editor.setPlainText(original);
        auto *live = editor.findChild<MarkdownEdit *>();
        live->setSourceCursor(original.size());
        QVERIFY(live->toPlainText().contains(QStringLiteral("粗体 和 代码")));
        QTest::keyClicks(live, " abc");
        QCOMPARE(editor.toPlainText(), original + " abc");
        QVERIFY(live->renderedBlockCount() <= 1);
        QTest::keyClick(live, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor.toPlainText(), original);
        QTest::keyClick(live, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(editor.toPlainText(), original + " abc");
        editor.setSourceMode(true);
        auto *source = editor.findChild<QPlainTextEdit *>();
        QTest::keyClicks(source, "!"); editor.setSourceMode(false);
        QCOMPARE(editor.toPlainText(), original + " abc!");
        live->selectAll(); live->copy();
        QCOMPARE(QApplication::clipboard()->text(), editor.toPlainText());
        QTest::keyClick(live, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(editor.toPlainText(), QString());
        QTest::keyClick(live, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor.toPlainText(), original + " abc!");
    }
    void verticalArrowsCompleteDocumentEdges() {
        const auto exercise = [](auto *view, auto load, auto canonical) {
            view->setFocus();
            const auto place = [view](QTextCursor::MoveOperation first, QTextCursor::MoveOperation second) {
                auto cursor=view->textCursor(); cursor.movePosition(first); cursor.movePosition(second);
                view->setTextCursor(cursor); QCoreApplication::processEvents();
            };
            load(QStringLiteral("first line\nfinal line text")); QCoreApplication::processEvents();
            place(QTextCursor::Start,QTextCursor::Start);
            QTest::keyClick(view,Qt::Key_Down); QCoreApplication::processEvents();
            QVERIFY(!view->textCursor().atEnd());
            place(QTextCursor::End,QTextCursor::StartOfLine);
            QTest::keyClick(view,Qt::Key_Down); QCoreApplication::processEvents();
            QVERIFY(view->textCursor().atEnd());
            place(QTextCursor::End,QTextCursor::StartOfLine);
            const int anchor=view->textCursor().position();
            QTest::keyClick(view,Qt::Key_Down,Qt::ShiftModifier); QCoreApplication::processEvents();
            QVERIFY(view->textCursor().atEnd()); QCOMPARE(view->textCursor().anchor(),anchor);
            QCOMPARE(canonical(),QStringLiteral("first line\nfinal line text"));

            place(QTextCursor::End,QTextCursor::End);
            QTest::keyClick(view,Qt::Key_Up); QCoreApplication::processEvents();
            QVERIFY(!view->textCursor().atStart());
            place(QTextCursor::Start,QTextCursor::EndOfLine);
            QTest::keyClick(view,Qt::Key_Up); QCoreApplication::processEvents();
            QVERIFY(view->textCursor().atStart());
            place(QTextCursor::Start,QTextCursor::EndOfLine);
            const int startAnchor=view->textCursor().position();
            QTest::keyClick(view,Qt::Key_Up,Qt::ShiftModifier); QCoreApplication::processEvents();
            QVERIFY(view->textCursor().atStart()); QCOMPARE(view->textCursor().anchor(),startAnchor);
            QCOMPARE(canonical(),QStringLiteral("first line\nfinal line text"));

            const QString wrapped=QStringLiteral("自动换行的中文正文与 English text ").repeated(35);
            load(wrapped); QCoreApplication::processEvents();
            place(QTextCursor::Start,QTextCursor::Start);
            QTest::keyClick(view,Qt::Key_Down); QCoreApplication::processEvents();
            QVERIFY(!view->textCursor().atEnd());
            place(QTextCursor::End,QTextCursor::StartOfLine);
            QTest::keyClick(view,Qt::Key_Down); QCoreApplication::processEvents();
            QVERIFY(view->textCursor().atEnd()); QCOMPARE(canonical(),wrapped);
            QTest::keyClick(view,Qt::Key_Up); QCoreApplication::processEvents();
            QVERIFY(!view->textCursor().atStart());
            place(QTextCursor::Start,QTextCursor::EndOfLine);
            QTest::keyClick(view,Qt::Key_Up); QCoreApplication::processEvents();
            QVERIFY(view->textCursor().atStart()); QCOMPARE(canonical(),wrapped);

            load(QStringLiteral("first\n")); QCoreApplication::processEvents();
            place(QTextCursor::End,QTextCursor::End);
            QTest::keyClick(view,Qt::Key_Down); QCOMPARE(canonical(),QStringLiteral("first\n"));
            load(QString()); QTest::keyClick(view,Qt::Key_Down); QVERIFY(canonical().isEmpty());
            load(QStringLiteral("last line")); QCoreApplication::processEvents();
            place(QTextCursor::Start,QTextCursor::Start);
            QTest::keyClick(view,Qt::Key_Down); QTest::keyClicks(view,"!");
            QCOMPARE(canonical(),QStringLiteral("last line!"));
            QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier); QCOMPARE(canonical(),QStringLiteral("last line"));
            place(QTextCursor::End,QTextCursor::End);
            QTest::keyClick(view,Qt::Key_Up); QTest::keyClicks(view,"!");
            QCOMPARE(canonical(),QStringLiteral("!last line"));
            QTest::keyClick(view,Qt::Key_Z,Qt::ControlModifier); QCOMPARE(canonical(),QStringLiteral("last line"));
        };
        HeadingTextEdit editor; editor.resize(320,240); editor.show();
        auto load=[&](const QString &text){editor.setPlainText(text);};
        auto canonical=[&]{return editor.toPlainText();};
        exercise(editor.findChild<MarkdownEdit *>(),load,canonical);
        editor.setSourceMode(true);
        exercise(editor.findChild<QPlainTextEdit *>("headingSource"),load,canonical);
        TextLayoutGuard::Edit plain; plain.resize(320,240); plain.show();
        exercise(&plain,[&](const QString &text){TextLayoutGuard::setPlainText(&plain,text);},[&]{return plain.toPlainText();});
    }
    void newlinesAndDeletion() {
        HeadingTextEdit editor;
        auto *live = editor.findChild<MarkdownEdit *>();
        for (const auto &value : {"a\nb\nc", "a\n\nb", "# Heading\n\n", ""}) {
            editor.setPlainText(QString::fromUtf8(value));
            live->setSourceCursor(editor.toPlainText().size());
            QTest::keyClick(live, Qt::Key_Return);
            QTest::keyClicks(live, "new");
            QCOMPARE(editor.toPlainText(), QString::fromUtf8(value) + "\nnew");
            QCOMPARE(live->sourcePosition(live->document()->characterCount() - 1), editor.toPlainText().size());
            for (int j = 0; j < 4; ++j) QTest::keyClick(live, Qt::Key_Backspace);
            QCOMPARE(editor.toPlainText(), QString::fromUtf8(value));
        }
    }
    void tablesAndCellEditing() {
        HeadingTextEdit editor; editor.resize(700, 500); editor.show();
        const QString text = QStringLiteral("前言\n\n| 名称 | 结果 |\n| --- | ---: |\n| 原值 | **12** |\n\n后记");
        editor.setPlainText(text);
        auto *live = editor.findChild<MarkdownEdit *>();
        const int start = text.indexOf(QStringLiteral("原值"));
        live->setSourceCursor(start + 2, start);
        QVERIFY(live->textCursor().currentTable());
        QTest::keyClicks(live, "new");
        QCOMPARE(editor.toPlainText(), QString(text).replace(QStringLiteral("原值"), "new"));
        QTest::keyClick(live, Qt::Key_Tab);
        QVERIFY(live->textCursor().currentTable());
        QTest::keyClicks(live, "20");
        QVERIFY(editor.toPlainText().contains("| new | 20 |"));
        QTest::keyClick(live, Qt::Key_Tab);
        QVERIFY(editor.toPlainText().contains("|  |  |"));
        QVERIFY(live->textCursor().currentTable());
        live->selectAll(); live->copy(); QCOMPARE(QApplication::clipboard()->text(), editor.toPlainText());
        editor.setPreviewMode(true); const auto saved = editor.toPlainText();
        QTest::keyClicks(live, "blocked"); QCOMPARE(editor.toPlainText(), saved);
        editor.grab().save(QCoreApplication::applicationDirPath() + "/table-reading.png");
    }
    void imeAndCrossBlockSelection() {
        HeadingTextEdit editor;
        const QString original = QStringLiteral("# 标题\n\n正文\n下一段");
        editor.setPlainText(original);
        auto *live = editor.findChild<MarkdownEdit *>();
        const int pos = original.indexOf(QStringLiteral("正文")) + 2;
        live->setSourceCursor(pos);
        QInputMethodEvent preedit(QStringLiteral("zhong"), {});
        QApplication::sendEvent(live, &preedit);
        QCOMPARE(editor.toPlainText(), original);
        QInputMethodEvent commit; commit.setCommitString(QStringLiteral("中文"));
        QApplication::sendEvent(live, &commit); QCoreApplication::processEvents();
        QCOMPARE(editor.toPlainText(), QString(original).insert(pos, QStringLiteral("中文")));
        QTest::keyClick(live, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(editor.toPlainText(), original);
        live->setSourceCursor(original.size(), original.indexOf(QStringLiteral("正文")));
        QTest::keyClicks(live, "replacement");
        QCOMPARE(editor.toPlainText(), QStringLiteral("# 标题\n\nreplacement"));
    }
    void formulasPersistAndReuse() {
        HeadingTextEdit editor; editor.resize(700, 500); editor.show();
        editor.setPlainText("# Math\n\n$$\\frac{a}{b}$$\n\n| x | y |\n| --- | --- |\n| $x^2$ | 2 |\n\ntext");
        auto *live = editor.findChild<MarkdownEdit *>();
        auto *math = live->math();
        QSignalSpy ready(math, &MathPreview::ready);
        live->setSourceCursor(editor.toPlainText().size());
        QTRY_COMPARE_WITH_TIMEOUT(images(live->document()), 2, 8000);
        const auto completions = ready.size();
        QTest::keyClicks(live, " more");
        QTest::qWait(180);
        QCOMPARE(images(live->document()), 2); QCOMPARE(ready.size(), completions);
        live->setSourceCursor(editor.toPlainText().indexOf("\\frac") + 2);
        QCOMPARE(images(live->document()), 1);
        live->setSourceCursor(editor.toPlainText().size());
        QCOMPARE(images(live->document()), 2); QVERIFY(!math->pending());
        editor.grab().save(QCoreApplication::applicationDirPath() + "/live-math.png");
    }
    void compositionReplacementAndCancel() {
        HeadingTextEdit editor; const QString original = QStringLiteral("**标题**\n正文内容");
        editor.setPlainText(original); auto *live = editor.findChild<MarkdownEdit *>();
        live->setSourceCursor(original.size(), original.size() - 2);
        QInputMethodEvent preedit(QStringLiteral("zhong"), {}); QApplication::sendEvent(live, &preedit);
        QCOMPARE(editor.toPlainText(), original);
        QInputMethodEvent cancel; QApplication::sendEvent(live, &cancel); QCoreApplication::processEvents();
        QCOMPARE(editor.toPlainText(), original);
        live->setSourceCursor(original.size());
        QInputMethodEvent first(QStringLiteral("wen"), {}); first.setCommitString(QStringLiteral("中"));
        QApplication::sendEvent(live, &first);
        QInputMethodEvent second; second.setCommitString(QStringLiteral("文")); QApplication::sendEvent(live, &second);
        QCoreApplication::processEvents(); QCOMPARE(editor.toPlainText(), original + QStringLiteral("中文"));
    }
    void incrementalProjectionMatchesFresh() {
        HeadingTextEdit editor;
        auto *live = editor.findChild<MarkdownEdit *>();
        QString text = "# Heading\n\n**bold**\n\n| a | b |\n| --- | --- |\n| c | d |\n\nend";
        editor.setPlainText(text);
        quint32 random = 47119;
        for (int i = 0; i < 160; ++i) {
            random = random * 1664525 + 1013904223;
            const int p = random % (text.size() + 1);
            live->setSourceCursor(p);
            if (i % 3 == 0 && p > 0) { QTest::keyClick(live, Qt::Key_Backspace); text.remove(p - 1, 1); }
            else if (i % 5 == 0) { QTest::keyClick(live, Qt::Key_Return); text.insert(p, '\n'); }
            else { QTest::keyClicks(live, "x"); text.insert(p, 'x'); }
            QCOMPARE(editor.toPlainText(), text);
            HeadingTextEdit fresh; fresh.setPlainText(text);
            auto *reference = fresh.findChild<MarkdownEdit *>();
            reference->setSourceCursor(live->sourcePosition(live->textCursor().position()));
            QCOMPARE(live->toPlainText(), reference->toPlainText());
            QCOMPARE(live->sourcePosition(live->document()->characterCount() - 1), text.size());
        }
    }
    void tableActionsUndoAndAlignment() {
        HeadingTextEdit editor; editor.resize(700, 500); editor.show();
        auto *live = editor.findChild<MarkdownEdit *>();
        const QString original = "before\n\n| head | value |\n| :---: | ---: |\n| item | 12 |\n\nafter";
        editor.setPlainText(original); live->setSourceCursor(original.indexOf("item") + 1);
        auto action = [live](const QString &name) {
            QTimer::singleShot(0, [live, name] {
                auto *menu = live->findChild<QMenu *>();
                QVERIFY(menu);
                bool found = false;
                for (auto *item : menu->actions()) if (item->text() == name) { item->trigger(); found = true; break; }
                QVERIFY(found); menu->close();
            });
            QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(20,20), live->mapToGlobal(QPoint(20,20)));
            QApplication::sendEvent(live->viewport(), &event);
        };
        action(QStringLiteral("在右侧插入列"));
        QVERIFY(editor.toPlainText().contains("| :---: | --- | ---: |"));
        QCOMPARE(live->textCursor().currentTable()->columns(), 3);
        action(QStringLiteral("删除当前列")); QCOMPARE(editor.toPlainText(), original);
        action(QStringLiteral("在下方插入行"));
        QCOMPARE(live->textCursor().currentTable()->rows(), 3);
        action(QStringLiteral("删除当前行")); QCOMPARE(editor.toPlainText(), original);
        QTest::keyClick(live, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(editor.toPlainText().contains("|  |  |"));
        QTest::keyClick(live, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(editor.toPlainText(), original);
    }
    void longNote() {
        HeadingTextEdit editor;
        QString source;
        for (int i = 0; i < 800; ++i) source += QStringLiteral("## 第 %1 节\n中文 **重点** 与 `code`\n\n").arg(i);
        QElapsedTimer timer; timer.start(); editor.setPlainText(source);
        auto *live = editor.findChild<MarkdownEdit *>(); live->setSourceCursor(source.size());
        qInfo() << "load-and-render-ms" << timer.elapsed();
        timer.restart(); QTest::keyClicks(live, "typing");
        qInfo() << "six-keys-ms" << timer.elapsed();
        QVERIFY(live->renderedBlockCount() <= 1);
        QCOMPARE(editor.toPlainText(), source + "typing");
        QVERIFY(timer.elapsed() < 3000);
    }
    void mouseSelectionAndVisualFixture() {
        HeadingTextEdit editor; editor.resize(880, 700); editor.setTextPointSize(12); editor.show();
        const QString source = QStringLiteral(
            "# 注意力计算记录\n"
            "正文中的 **关键结论**、*解释* 与 `KV Cache`。\n\n"
            "$$o_t=\\sum_{j=1}^{t}\\alpha_{t,j}v_j$$\n\n"
            "## 对照结果\n"
            "| 项目 | 表达式 | 结果 |\n| --- | :---: | ---: |\n"
            "| 查询向量 | $q_t$ | 128 |\n| 输出 | $o_t$ | 256 |\n\n"
            "- 公式写完后保持显示\n- 表格内可以继续编辑\n\n"
            "```cpp\nconst auto result = cache.value(key);\n```\n\n"
            "继续编写下一段。");
        editor.setPlainText(source); auto *live = editor.findChild<MarkdownEdit *>();
        live->setSourceCursor(source.size());
        QTRY_COMPARE_WITH_TIMEOUT(images(live->document()), 3, 8000);
        const auto path = QCoreApplication::applicationDirPath();
        QVERIFY(editor.grab().save(path + "/native-markdown-desktop.png"));
        editor.resize(560, 400); QCoreApplication::processEvents();
        QVERIFY(editor.grab().save(path + "/native-markdown-compact.png"));
        editor.setPlainText("# Heading\n\n**first** paragraph\n\nlast paragraph");
        live->setSourceCursor(editor.toPlainText().size());
        auto cursor = live->textCursor(); cursor.setPosition(live->viewPosition(13));
        const QPoint start = live->cursorRect(cursor).center();
        cursor.setPosition(live->viewPosition(29)); const QPoint end = live->cursorRect(cursor).center();
        const QString before = live->toPlainText();
        QTest::mousePress(live->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(live->viewport(), end); QCoreApplication::processEvents();
        QCOMPARE(live->toPlainText(), before);
        QTest::mouseRelease(live->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(live->textCursor().hasSelection());
        const QString old = editor.toPlainText(); QTest::keyClicks(live, "changed");
        QTest::keyClick(live, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(editor.toPlainText(), old);
    }
};
QTEST_MAIN(MarkdownEditTest)
#include "test_markdown_edit.moc"
