#include "ui/frameless-window-controller.h"
#include "ui/window-title-bar.h"

#include <QCloseEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QVBoxLayout>
#include <QtTest>

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

    WindowFixture()
    {
        window.setWindowTitle(QStringLiteral("Initial title"));
        window.resize(640, 400);
        auto* layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        titleBar = new mnce::WindowTitleBar(&window);
        layout->addWidget(titleBar);
        layout->addStretch();
        controller = std::make_unique<mnce::FramelessWindowController>(
            &window, titleBar, &window, &operations);
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
    void usesPlatformSpecificFramelessPolicy();
    void tracksTitleAndStandardButtonIcons();
    void routesWindowButtonOperationsAndState();
    void movesAndTogglesOnlyFromDraggableArea();
    void mapsAllResizeEdgesAndCorners();
    void requestsResizeOnlyForNormalWindows();
    void updatesResizeCursorFromPointerPosition();
    void ignoresOwnedTopLevelDialogs();
};

void WindowFrameTest::usesPlatformSpecificFramelessPolicy()
{
    WindowFixture fixture;
#ifdef Q_OS_WIN
    QVERIFY(mnce::FramelessWindowController::enabledForCurrentPlatform());
    QVERIFY(fixture.window.windowFlags().testFlag(Qt::FramelessWindowHint));
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

void WindowFrameTest::requestsResizeOnlyForNormalWindows()
{
    WindowFixture fixture;
    const QPoint edgePoint(1, fixture.window.height() / 2);
    const QRect originalGeometry = fixture.window.geometry();
    fixture.operations.systemRequestResult = false;
    QTest::mousePress(&fixture.window, Qt::LeftButton, Qt::NoModifier, edgePoint);
#ifdef Q_OS_WIN
    QCOMPARE(fixture.operations.resizeRequests,
             QVector<Qt::Edges>{Qt::Edges(Qt::LeftEdge)});
#else
    QVERIFY(fixture.operations.resizeRequests.isEmpty());
#endif
    QCOMPARE(fixture.window.geometry(), originalGeometry);

    fixture.window.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();
    QTest::mousePress(&fixture.window, Qt::LeftButton, Qt::NoModifier, edgePoint);
#ifdef Q_OS_WIN
    QCOMPARE(fixture.operations.resizeRequests.size(), 1);
#else
    QVERIFY(fixture.operations.resizeRequests.isEmpty());
#endif
    QVERIFY(fixture.window.cursor().shape() == Qt::ArrowCursor);
}

void WindowFrameTest::updatesResizeCursorFromPointerPosition()
{
    WindowFixture fixture;
    const auto moveTo = [&fixture](const QPoint& position) {
        const QPoint globalPosition = fixture.window.mapToGlobal(position);
        QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(globalPosition),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&fixture.window, &event);
    };

    moveTo({1, fixture.window.height() / 2});
#ifdef Q_OS_WIN
    QCOMPARE(fixture.window.cursor().shape(), Qt::SizeHorCursor);
    moveTo({fixture.window.width() / 2, 1});
    QCOMPARE(fixture.window.cursor().shape(), Qt::SizeVerCursor);
    moveTo({1, 1});
    QCOMPARE(fixture.window.cursor().shape(), Qt::SizeFDiagCursor);
    moveTo({fixture.window.width() - 2, 1});
    QCOMPARE(fixture.window.cursor().shape(), Qt::SizeBDiagCursor);
#endif
    moveTo(fixture.window.rect().center());
    QCOMPARE(fixture.window.cursor().shape(), Qt::ArrowCursor);

    fixture.window.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();
    moveTo({1, fixture.window.height() / 2});
    QCOMPARE(fixture.window.cursor().shape(), Qt::ArrowCursor);
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

QTEST_MAIN(WindowFrameTest)

#include "window-frame-test.moc"
