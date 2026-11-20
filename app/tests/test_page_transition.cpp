#include "ui/contentworkspace.h"
#include "ui/motion.h"
#include "ui/pagetransition.h"

#include <QApplication>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QShortcut>
#include <QStackedWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QtTest>
#include <qt_windows.h>

namespace {
using PageTransition::Direction;

QList<QWidget *> layers(const QStackedWidget &stack)
{
    return stack.findChildren<QWidget *>(QStringLiteral("pageTransition"), Qt::FindDirectChildrenOnly);
}

QWidget *visibleLayer(const QStackedWidget &stack)
{
    for (auto *layer : layers(stack)) if (layer->isVisible()) return layer;
    return nullptr;
}

void showWindow(QWidget &window)
{
    window.resize(520, 300);
    window.show();
    window.activateWindow();
    QCoreApplication::processEvents();
}
}

class PageTransitionNativeTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
    }

    void disabledAndExplicitInstantNavigationUpdateSynchronously()
    {
        auto &motion = UiMotion::instance();
        const auto restore = qScopeGuard([&motion, preference = motion.preference()] {
            motion.setPreference(preference);
        });
        QStackedWidget stack;
        auto *first = new QWidget;
        auto *second = new QWidget;
        stack.addWidget(first);
        stack.addWidget(second);
        showWindow(stack);

        PageTransition::setCurrent(&stack, second, Direction::Instant);
        QCOMPARE(stack.currentWidget(), second);
        QVERIFY(second->isVisible());
        QVERIFY(layers(stack).isEmpty());

        motion.setPreference(UiMotion::Preference::Off);
        PageTransition::setCurrent(&stack, first, Direction::Module);
        QCOMPARE(stack.currentWidget(), first);
        QVERIFY(first->isVisible());
        QVERIFY(layers(stack).isEmpty());
    }

    void editorReceivesFirstKeyWithItsLiveCaretVisible()
    {
        if (!UiMotion::enabled()) QSKIP("This platform has disabled desktop motion.");
        QStackedWidget stack;
        auto *first = new QWidget;
        first->setStyleSheet(QStringLiteral("background: rgb(90, 20, 130)"));
        auto *second = new QLineEdit;
        stack.addWidget(first);
        stack.addWidget(second);
        showWindow(stack);
        QTRY_VERIFY_WITH_TIMEOUT(stack.isActiveWindow(), 500);
        const auto oldColor = stack.grab().toImage().pixelColor(10, 10);

        PageTransition::setCurrent(&stack, second, Direction::Module);
        second->setFocus();
        QCOMPARE(stack.currentWidget(), second);
        QCOMPARE(QApplication::focusWidget(), second);
        QCOMPARE(stack.grab().toImage().pixelColor(10, 10), oldColor);
        auto *layer = visibleLayer(stack);
        QVERIFY(layer);
        QCOMPARE(stack.childAt(stack.rect().center()), second);
        QCoreApplication::sendPostedEvents(layer, QEvent::MetaCall);
        QVERIFY(visibleLayer(stack));

        bool layerVisibleWhenEdited = true;
        connect(second, &QLineEdit::textEdited, this, [&] { layerVisibleWhenEdited = visibleLayer(stack); });
        QTest::keyClick(second, Qt::Key_A);
        QCOMPARE(second->text(), QStringLiteral("a"));
        QCOMPARE(second->cursorPosition(), 1);
        QVERIFY(!layerVisibleWhenEdited);
        QVERIFY(!visibleLayer(stack));
    }

    void rapidMouseShortcutAndDirectSwitchesSettleOnLatestPage()
    {
        if (!UiMotion::enabled()) QSKIP("This platform has disabled desktop motion.");
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *goNext = new QPushButton(QStringLiteral("Next"));
        auto *stack = new QStackedWidget;
        layout->addWidget(goNext);
        layout->addWidget(stack);
        auto *first = new QWidget;
        auto *second = new QStackedWidget;
        auto *openChild = new QPushButton(QStringLiteral("Open child"));
        auto *child = new QWidget;
        second->addWidget(openChild);
        second->addWidget(child);
        auto *third = new QWidget;
        stack->addWidget(first);
        stack->addWidget(second);
        stack->addWidget(third);
        connect(goNext, &QPushButton::clicked, this, [=] {
            PageTransition::setCurrent(stack, stack->currentWidget() == first ? second : third, Direction::Module);
        });
        connect(openChild, &QPushButton::clicked, this, [=] {
            PageTransition::setCurrent(second, child, Direction::Forward);
        });
        auto *back = new QShortcut(QKeySequence(Qt::Key_F9), &window);
        connect(back, &QShortcut::activated, this, [=] { PageTransition::setCurrent(stack, first, Direction::Module); });
        showWindow(window);
        QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 500);

        QTest::mouseClick(goNext, Qt::LeftButton);
        QCOMPARE(stack->currentWidget(), second);
        QVERIFY(visibleLayer(*stack));
        QTest::mouseClick(openChild, Qt::LeftButton);
        QCOMPARE(second->currentWidget(), child);
        QVERIFY(!visibleLayer(*stack));
        QVERIFY(!visibleLayer(*second));
        QTest::mouseClick(goNext, Qt::LeftButton);
        QCOMPARE(stack->currentWidget(), third);
        QVERIFY(!visibleLayer(*stack));
        QTest::keyClick(goNext, Qt::Key_F9);
        QCOMPARE(stack->currentWidget(), first);
        QVERIFY(!visibleLayer(*stack));

        QTest::qWait(250);
        PageTransition::setCurrent(stack, second, Direction::Module);
        QVERIFY(visibleLayer(*stack));
        PageTransition::setCurrent(stack, third, Direction::Module);
        QCOMPARE(stack->currentWidget(), third);
        QVERIFY(!visibleLayer(*stack));
    }

    void resizeCompletionAndReducedMotionClearLayers()
    {
        if (!UiMotion::enabled()) QSKIP("This platform has disabled desktop motion.");
        auto &motion = UiMotion::instance();
        const auto restore = qScopeGuard([&motion, preference = motion.preference()] {
            motion.setPreference(preference);
        });
        QStackedWidget stack;
        auto *first = new QWidget;
        auto *second = new QWidget;
        stack.addWidget(first);
        stack.addWidget(second);
        showWindow(stack);

        PageTransition::setCurrent(&stack, second, Direction::Forward);
        QVERIFY(visibleLayer(stack));
        stack.resize(stack.width() + 20, stack.height());
        QVERIFY(!visibleLayer(stack));
        QCOMPARE(stack.currentWidget(), second);

        QTest::qWait(250);
        PageTransition::setCurrent(&stack, first, Direction::Back);
        QVERIFY(visibleLayer(stack));
        QTRY_VERIFY_WITH_TIMEOUT(!visibleLayer(stack), 750);
        QCOMPARE(stack.currentWidget(), first);

        PageTransition::setCurrent(&stack, second, Direction::Forward);
        QVERIFY(visibleLayer(stack));
        motion.setPreference(UiMotion::Preference::Off);
        QVERIFY(!visibleLayer(stack));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(layers(stack).isEmpty());
        QCOMPARE(stack.currentWidget(), second);
    }

    void movingAndClosingAWorkspaceEntryCancelPendingMotion()
    {
        if (!UiMotion::enabled()) QSKIP("This platform has disabled desktop motion.");
        QStackedWidget modules;
        auto *root = new QWidget;
        auto *workspace = new ContentWorkspace(root);
        auto *other = new QWidget;
        modules.addWidget(workspace);
        modules.addWidget(other);
        connect(workspace, &ContentWorkspace::activationRequested, this, [&] {
            PageTransition::setCurrent(&modules, workspace, Direction::Instant);
        });
        auto *stack = workspace->findChild<QStackedWidget *>(Qt::FindDirectChildrenOnly);
        QVERIFY(stack);
        showWindow(modules);

        const auto parentKey = QStringLiteral("parent");
        auto *parent = new QDialog;
        workspace->present(parentKey, parent, root);
        QCOMPARE(workspace->currentKey(), parentKey);
        QVERIFY(visibleLayer(*stack));
        auto *detach = parent->parentWidget()->findChild<QPushButton *>(QStringLiteral("contentDetach"));
        QVERIFY(detach);
        detach->click();
        QPointer<QDialog> host = workspace->findChild<QDialog *>(QStringLiteral("contentWindow"));
        QVERIFY(host);
        auto *hostStack = host->findChild<QStackedWidget *>();
        QVERIFY(hostStack);
        QCOMPARE(parent->window(), host.data());
        QCOMPARE(stack->currentWidget(), root);
        QVERIFY(!visibleLayer(*stack));
        QVERIFY(!visibleLayer(*hostStack));
        PageTransition::setCurrent(&modules, other, Direction::Instant);
        QCoreApplication::processEvents();

        const auto key = QStringLiteral("parent/detail");
        auto *detail = new QDialog;
        auto *layout = new QVBoxLayout(detail);
        auto *editor = new QLineEdit(QStringLiteral("draft"), detail);
        layout->addWidget(editor);
        workspace->present(key, detail, parent);
        QCOMPARE(detail->window(), host.data());
        QVERIFY(visibleLayer(*hostStack));
        auto *attach = detail->parentWidget()->findChild<QPushButton *>(QStringLiteral("contentDetach"));
        QVERIFY(attach);
        attach->click();
        QCOMPARE(detail->window(), &modules);
        QCOMPARE(modules.currentWidget(), workspace);
        QCOMPARE(workspace->currentKey(), key);
        QVERIFY(!visibleLayer(modules));
        QVERIFY(!visibleLayer(*stack));
        QVERIFY(!visibleLayer(*hostStack));
        QCOMPARE(editor->text(), QStringLiteral("draft"));

        QVERIFY(workspace->closePage(key));
        QCOMPARE(workspace->currentKey(), parentKey);
        QVERIFY(!visibleLayer(*stack));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!host);
        QVERIFY(layers(*stack).isEmpty());
    }
};

int main(int argc, char **argv)
{
    // Set the private desktop before Qt initializes the Windows platform plugin.
    const HDESK previous = GetThreadDesktop(GetCurrentThreadId());
    const auto name = QUuid::createUuid().toString(QUuid::Id128).toStdWString();
    const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) {
        qCritical() << "CreateDesktop failed" << GetLastError();
        return 1;
    }
    const auto cleanup = qScopeGuard([&] {
        SetThreadDesktop(previous);
        CloseDesktop(desktop);
    });
    if (!SetThreadDesktop(desktop)) {
        qCritical() << "SetThreadDesktop failed" << GetLastError();
        return 1;
    }
    QApplication app(argc, argv);
    PageTransitionNativeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_page_transition.moc"
