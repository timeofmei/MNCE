#include "ui/frameless-window-controller.h"
#include "ui/window-title-bar.h"
#ifdef Q_OS_WIN
#include "ui/windows-window-frame-adapter.h"
#endif

#include <QCloseEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QVBoxLayout>
#include <QtTest>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

class RecordingOperations final : public mnce::WindowSystemOperations
{
public:
    bool startSystemMove(QWidget*) override
    {
        ++moveRequests;
        return systemRequestResult;
    }
    bool startSystemResize(QWidget*, Qt::Edges edges) override
    {
        resizeRequests.push_back(edges);
        return systemRequestResult;
    }
    void showMinimized(QWidget*) override { ++minimizeRequests; }
    void showMaximized(QWidget* window) override
    {
        ++maximizeRequests;
        window->setWindowState(window->windowState() | Qt::WindowMaximized);
    }
    void showNormal(QWidget* window) override
    {
        ++normalRequests;
        window->setWindowState(window->windowState() & ~Qt::WindowMaximized);
    }
    void close(QWidget* window) override
    {
        ++closeRequests;
        window->close();
    }

    bool systemRequestResult = true;
    int moveRequests = 0;
    QVector<Qt::Edges> resizeRequests;
    int minimizeRequests = 0;
    int maximizeRequests = 0;
    int normalRequests = 0;
    int closeRequests = 0;
};

class CloseTrackingWindow final : public QWidget
{
public:
    int closeEvents = 0;

protected:
    void closeEvent(QCloseEvent* event) override
    {
        ++closeEvents;
        QWidget::closeEvent(event);
    }
};

struct WindowFixture
{
    CloseTrackingWindow window;
    mnce::WindowTitleBar* titleBar = nullptr;
    RecordingOperations operations;
    std::unique_ptr<mnce::FramelessWindowController> controller;
#ifdef Q_OS_WIN
    std::unique_ptr<mnce::WindowsWindowFrameAdapter> windowsAdapter;
#endif

    WindowFixture()
    {
        window.setWindowFlags(Qt::Window | Qt::WindowTitleHint
                              | Qt::WindowSystemMenuHint
                              | Qt::WindowMinimizeButtonHint
                              | Qt::WindowMaximizeButtonHint
                              | Qt::WindowCloseButtonHint);
        window.setWindowTitle(QStringLiteral("Initial title"));
        window.resize(640, 400);
        auto* layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        titleBar = new mnce::WindowTitleBar(&window);
        layout->addWidget(titleBar);
        layout->addStretch();
        controller = std::make_unique<mnce::FramelessWindowController>(
            &window, titleBar, &window, &operations);
#ifdef Q_OS_WIN
        windowsAdapter = std::make_unique<mnce::WindowsWindowFrameAdapter>(
            &window, titleBar, controller.get());
#endif
        window.show();
        if (!QTest::qWaitForWindowExposed(&window)) {
            qFatal("Test window was not exposed");
        }
    }

    QPushButton* button(const char* name) const
    {
        return titleBar->findChild<QPushButton*>(QString::fromLatin1(name));
    }
};

} // namespace

class WindowFrameTest final : public QObject
{
    Q_OBJECT

private slots:
    void usesPlatformSpecificCustomFramePolicy();
    void tracksTitleAndStandardButtonIcons();
    void routesWindowButtonOperationsAndState();
    void movesAndTogglesOnlyFromDraggableArea();
    void mapsAllResizeEdgesAndCorners();
    void ignoresOwnedTopLevelDialogs();
#ifdef Q_OS_WIN
    void mapsWindowsNativeHitTestRegions();
    void limitsHandledWindowsMessages();
#endif
};

void WindowFrameTest::usesPlatformSpecificCustomFramePolicy()
{
    WindowFixture fixture;
#ifdef Q_OS_WIN
    QVERIFY(mnce::FramelessWindowController::enabledForCurrentPlatform());
    QVERIFY(!fixture.window.windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(fixture.windowsAdapter->frameApplied());
    const auto handle = reinterpret_cast<HWND>(fixture.window.winId());
    const LONG_PTR style = GetWindowLongPtrW(handle, GWL_STYLE);
    QCOMPARE(style & static_cast<LONG_PTR>(WS_CAPTION), 0);
    QVERIFY((style & static_cast<LONG_PTR>(WS_THICKFRAME)) != 0);
    QVERIFY((style & static_cast<LONG_PTR>(WS_SYSMENU)) != 0);
    QVERIFY((style & static_cast<LONG_PTR>(WS_MINIMIZEBOX)) != 0);
    QVERIFY((style & static_cast<LONG_PTR>(WS_MAXIMIZEBOX)) != 0);
    QCOMPARE(style & static_cast<LONG_PTR>(WS_POPUP), 0);
#else
    QVERIFY(!mnce::FramelessWindowController::enabledForCurrentPlatform());
    QVERIFY(!fixture.window.windowFlags().testFlag(Qt::FramelessWindowHint));
#endif
}

void WindowFrameTest::tracksTitleAndStandardButtonIcons()
{
    WindowFixture fixture;
    auto* label = fixture.titleBar->findChild<QLabel*>(QStringLiteral("windowTitleLabel"));
    auto* maximize = fixture.button("windowMaximizeRestoreButton");
    QCOMPARE(label->text(), QStringLiteral("Initial title"));
    QCOMPARE(maximize->icon().cacheKey(),
             fixture.titleBar->style()->standardIcon(QStyle::SP_TitleBarMaxButton).cacheKey());
    QCOMPARE(maximize->accessibleName(), QStringLiteral("最大化"));

    fixture.window.setWindowTitle(QStringLiteral("Changed title"));
    QCOMPARE(label->text(), QStringLiteral("Changed title"));
    fixture.window.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();
    QCOMPARE(maximize->icon().cacheKey(),
             fixture.titleBar->style()->standardIcon(QStyle::SP_TitleBarNormalButton).cacheKey());
    QCOMPARE(maximize->accessibleName(), QStringLiteral("还原"));
    QVERIFY(maximize->isEnabled());

    fixture.window.setWindowState(Qt::WindowNoState);
    QCoreApplication::processEvents();
    QCOMPARE(maximize->accessibleName(), QStringLiteral("最大化"));
    QVERIFY(maximize->isEnabled());
}

void WindowFrameTest::routesWindowButtonOperationsAndState()
{
    WindowFixture fixture;
    QTest::mouseClick(fixture.button("windowMinimizeButton"), Qt::LeftButton);
    QCOMPARE(fixture.operations.minimizeRequests, 1);

    QTest::mouseClick(fixture.button("windowMaximizeRestoreButton"), Qt::LeftButton);
    QCOMPARE(fixture.operations.maximizeRequests, 1);
    QCoreApplication::processEvents();
    QVERIFY(fixture.window.isMaximized());
    QTest::mouseClick(fixture.button("windowMaximizeRestoreButton"), Qt::LeftButton);
    QCOMPARE(fixture.operations.normalRequests, 1);

    QTest::mouseClick(fixture.button("windowCloseButton"), Qt::LeftButton);
    QCOMPARE(fixture.operations.closeRequests, 1);
    QCOMPARE(fixture.window.closeEvents, 1);
}

void WindowFrameTest::movesAndTogglesOnlyFromDraggableArea()
{
    WindowFixture fixture;
    auto* titleLabel = fixture.titleBar->findChild<QLabel*>(
        QStringLiteral("windowTitleLabel"));
    const QPoint titlePoint(100, titleLabel->height() / 2);
    QTest::mousePress(titleLabel, Qt::LeftButton, Qt::NoModifier, titlePoint);
    QCOMPARE(fixture.operations.moveRequests, 0);
    const QPoint movePosition(fixture.titleBar->width() / 2,
                              fixture.titleBar->height() / 2);
    QMouseEvent moveEvent(QEvent::MouseMove, QPointF(movePosition),
                          QPointF(fixture.titleBar->mapToGlobal(movePosition)),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.titleBar, &moveEvent);
#ifdef Q_OS_WIN
    QCOMPARE(fixture.operations.moveRequests, 1);
#else
    QCOMPARE(fixture.operations.moveRequests, 0);
#endif
    QTest::mouseDClick(titleLabel, Qt::LeftButton, Qt::NoModifier, titlePoint);
    QCOMPARE(fixture.operations.maximizeRequests, 1);

    auto* minimize = fixture.button("windowMinimizeButton");
    QTest::mousePress(minimize, Qt::LeftButton);
    QTest::mouseDClick(minimize, Qt::LeftButton);
#ifdef Q_OS_WIN
    QCOMPARE(fixture.operations.moveRequests, 1);
#else
    QCOMPARE(fixture.operations.moveRequests, 0);
#endif
    QCOMPARE(fixture.operations.maximizeRequests, 1);
}

void WindowFrameTest::mapsAllResizeEdgesAndCorners()
{
    using Controller = mnce::FramelessWindowController;
    const QSize size(100, 80);
    constexpr int margin = 6;
    QCOMPARE(Controller::edgesAt(size, {0, 40}, margin), Qt::Edges(Qt::LeftEdge));
    QCOMPARE(Controller::edgesAt(size, {99, 40}, margin), Qt::Edges(Qt::RightEdge));
    QCOMPARE(Controller::edgesAt(size, {50, 0}, margin), Qt::Edges(Qt::TopEdge));
    QCOMPARE(Controller::edgesAt(size, {50, 79}, margin), Qt::Edges(Qt::BottomEdge));
    QCOMPARE(Controller::edgesAt(size, {0, 0}, margin),
             Qt::Edges(Qt::LeftEdge | Qt::TopEdge));
    QCOMPARE(Controller::edgesAt(size, {99, 0}, margin),
             Qt::Edges(Qt::RightEdge | Qt::TopEdge));
    QCOMPARE(Controller::edgesAt(size, {0, 79}, margin),
             Qt::Edges(Qt::LeftEdge | Qt::BottomEdge));
    QCOMPARE(Controller::edgesAt(size, {99, 79}, margin),
             Qt::Edges(Qt::RightEdge | Qt::BottomEdge));
    QCOMPARE(Controller::edgesAt(size, {50, 40}, margin), Qt::Edges{});
    QCOMPARE(Controller::edgesAt(size, {-1, 40}, margin), Qt::Edges{});
}

void WindowFrameTest::ignoresOwnedTopLevelDialogs()
{
    WindowFixture fixture;
    QDialog dialog(&fixture.window);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    const QPoint mainWindowEdge = fixture.window.mapToGlobal(
        QPoint(1, fixture.window.height() / 2));
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(10, 10),
                      QPointF(mainWindowEdge), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(&dialog, &event);
    QVERIFY(fixture.operations.resizeRequests.isEmpty());
}

#ifdef Q_OS_WIN
void WindowFrameTest::mapsWindowsNativeHitTestRegions()
{
    WindowFixture fixture;
    const auto hitAtWindowPosition = [&fixture](const QPoint& position) {
        return fixture.windowsAdapter->nativeHitTestAt(
            fixture.window.mapToGlobal(position));
    };

    QCOMPARE(hitAtWindowPosition({1, 1}), qintptr(HTTOPLEFT));
    QCOMPARE(hitAtWindowPosition({fixture.window.width() - 2, 1}),
             qintptr(HTTOPRIGHT));
    QCOMPARE(hitAtWindowPosition({1, fixture.window.height() - 2}),
             qintptr(HTBOTTOMLEFT));
    QCOMPARE(hitAtWindowPosition(
                 {fixture.window.width() - 2, fixture.window.height() - 2}),
             qintptr(HTBOTTOMRIGHT));
    QCOMPARE(hitAtWindowPosition({1, fixture.window.height() / 2}), qintptr(HTLEFT));
    QCOMPARE(hitAtWindowPosition(
                 {fixture.window.width() - 2, fixture.window.height() / 2}),
             qintptr(HTRIGHT));
    QCOMPARE(hitAtWindowPosition({-1, fixture.window.height() / 2}),
             qintptr(HTNOWHERE));

    const QPoint titleBlank(20, fixture.titleBar->height() / 2);
    QVERIFY(fixture.titleBar->isDraggableAt(titleBlank));
    QCOMPARE(fixture.windowsAdapter->nativeHitTestAt(
                 fixture.titleBar->mapToGlobal(titleBlank)),
             qintptr(HTCAPTION));

    auto* minimize = fixture.button("windowMinimizeButton");
    const QPoint buttonPosition = minimize->rect().center();
    QVERIFY(!fixture.titleBar->isDraggableAt(
        fixture.titleBar->mapFromGlobal(minimize->mapToGlobal(buttonPosition))));
    QCOMPARE(fixture.windowsAdapter->nativeHitTestAt(
                 minimize->mapToGlobal(buttonPosition)),
             qintptr(HTCLIENT));
    QCOMPARE(hitAtWindowPosition(
                 {fixture.window.width() / 2, fixture.window.height() - 20}),
             qintptr(HTCLIENT));

    fixture.window.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();
    QCOMPARE(hitAtWindowPosition({1, fixture.window.height() / 2}), qintptr(HTCLIENT));
    QCOMPARE(fixture.windowsAdapter->nativeHitTestAt(
                 fixture.titleBar->mapToGlobal(titleBlank)),
             qintptr(HTCAPTION));
}

void WindowFrameTest::limitsHandledWindowsMessages()
{
    WindowFixture fixture;
    MSG message{};
    message.hwnd = reinterpret_cast<HWND>(fixture.window.winId());
    message.message = WM_NCRBUTTONUP;
    message.wParam = HTCAPTION;
    qintptr result = -1;
    QVERIFY(fixture.windowsAdapter->handleNativeEvent(
        QByteArrayLiteral("windows_generic_MSG"), &message, &result));
    QCOMPARE(result, qintptr(0));

    message.message = WM_SYSCOMMAND;
    message.wParam = SC_KEYMENU;
    QVERIFY(!fixture.windowsAdapter->handleNativeEvent(
        QByteArrayLiteral("windows_generic_MSG"), &message, &result));
    QVERIFY(!fixture.windowsAdapter->handleNativeEvent(
        QByteArrayLiteral("not-a-windows-event"), &message, &result));
}
#endif

QTEST_MAIN(WindowFrameTest)

#include "window-frame-test.moc"
