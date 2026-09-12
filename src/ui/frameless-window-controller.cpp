#include "ui/frameless-window-controller.h"

#include "ui/window-title-bar.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QWidget>

namespace mnce {
namespace {

class QtWindowSystemOperations final : public WindowSystemOperations
{
public:
    bool startSystemMove(QWidget* window) override
    {
        auto* handle = window->windowHandle();
        return handle != nullptr && handle->startSystemMove();
    }

    bool startSystemResize(QWidget* window, Qt::Edges edges) override
    {
        auto* handle = window->windowHandle();
        return handle != nullptr && handle->startSystemResize(edges);
    }

    void showMinimized(QWidget* window) override { window->showMinimized(); }
    void showMaximized(QWidget* window) override { window->showMaximized(); }
    void showNormal(QWidget* window) override { window->showNormal(); }
    void close(QWidget* window) override { window->close(); }
};

QtWindowSystemOperations defaultOperations;

void enableMouseTracking(QWidget* widget)
{
    widget->setMouseTracking(true);
    const auto descendants = widget->findChildren<QWidget*>();
    for (auto* descendant : descendants) {
        descendant->setMouseTracking(true);
    }
}

Qt::CursorShape cursorForEdges(Qt::Edges edges)
{
    if (edges == (Qt::TopEdge | Qt::LeftEdge)
        || edges == (Qt::BottomEdge | Qt::RightEdge)) {
        return Qt::SizeFDiagCursor;
    }
    if (edges == (Qt::TopEdge | Qt::RightEdge)
        || edges == (Qt::BottomEdge | Qt::LeftEdge)) {
        return Qt::SizeBDiagCursor;
    }
    if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) {
        return Qt::SizeHorCursor;
    }
    return Qt::SizeVerCursor;
}

} // namespace

FramelessWindowController::FramelessWindowController(
    QWidget* window, WindowTitleBar* titleBar, QObject* parent,
    WindowSystemOperations* operations)
    : QObject(parent)
    , window_(window)
    , titleBar_(titleBar)
    , operations_(operations != nullptr ? operations : &defaultOperations)
{
    Q_ASSERT(window_ != nullptr);
    Q_ASSERT(titleBar_ != nullptr);

    if (enabledForCurrentPlatform()) {
        window_->setWindowFlag(Qt::FramelessWindowHint, true);
        enableMouseTracking(window_);
    }

    window_->installEventFilter(this);
    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }

    connect(window_, &QWidget::windowTitleChanged, titleBar_, &WindowTitleBar::setTitle);
    connect(titleBar_, &WindowTitleBar::minimizeRequested, this, [this] {
        operations_->showMinimized(window_);
    });
    connect(titleBar_, &WindowTitleBar::maximizeRestoreRequested,
            this, &FramelessWindowController::toggleMaximized);
    connect(titleBar_, &WindowTitleBar::closeRequested, this, [this] {
        operations_->close(window_);
    });
    connect(titleBar_, &WindowTitleBar::systemMoveRequested,
            this, &FramelessWindowController::requestSystemMove);

    titleBar_->setTitle(window_->windowTitle());
    syncWindowState();
}

bool FramelessWindowController::enabledForCurrentPlatform()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

Qt::Edges FramelessWindowController::edgesAt(const QSize& windowSize,
                                              const QPoint& position, int margin)
{
    if (margin <= 0 || windowSize.isEmpty()
        || position.x() < 0 || position.y() < 0
        || position.x() >= windowSize.width() || position.y() >= windowSize.height()) {
        return {};
    }

    Qt::Edges edges;
    if (position.x() < margin) {
        edges |= Qt::LeftEdge;
    } else if (position.x() >= windowSize.width() - margin) {
        edges |= Qt::RightEdge;
    }
    if (position.y() < margin) {
        edges |= Qt::TopEdge;
    } else if (position.y() >= windowSize.height() - margin) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

int FramelessWindowController::resizeMargin() const
{
    return resizeMargin_;
}

bool FramelessWindowController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_) {
        if (event->type() == QEvent::WindowStateChange
            || event->type() == QEvent::Show
            || event->type() == QEvent::WindowActivate) {
            syncWindowState();
        }
        if (event->type() == QEvent::Leave) {
            window_->unsetCursor();
        }
    }

    if (!enabledForCurrentPlatform()) {
        return QObject::eventFilter(watched, event);
    }

    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    if (watchedWidget == nullptr
        || watchedWidget->window() != window_) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseMove) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        updateResizeCursor(mouseEvent->globalPosition().toPoint());
    } else if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && !window_->isMaximized()) {
            const Qt::Edges edges = edgesAtGlobalPosition(
                mouseEvent->globalPosition().toPoint());
            if (edges != Qt::Edges{}) {
                (void) operations_->startSystemResize(window_, edges);
                return true;
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

void FramelessWindowController::requestSystemMove()
{
    if (enabledForCurrentPlatform()) {
        (void) operations_->startSystemMove(window_);
    }
}

void FramelessWindowController::toggleMaximized()
{
    if (window_->isMaximized()) {
        operations_->showNormal(window_);
    } else {
        operations_->showMaximized(window_);
    }
}

void FramelessWindowController::syncWindowState()
{
    const Qt::WindowFlags flags = window_->windowFlags();
    const bool fixedSize = window_->minimumSize() == window_->maximumSize();
    titleBar_->setWindowState(
        window_->isMaximized(), flags.testFlag(Qt::WindowMinimizeButtonHint),
        flags.testFlag(Qt::WindowMaximizeButtonHint) && !fixedSize,
        flags.testFlag(Qt::WindowCloseButtonHint));
    if (window_->isMaximized()) {
        window_->unsetCursor();
    }
}

void FramelessWindowController::updateResizeCursor(const QPoint& globalPosition)
{
    if (window_->isMaximized()) {
        window_->unsetCursor();
        return;
    }
    const Qt::Edges edges = edgesAtGlobalPosition(globalPosition);
    if (edges == Qt::Edges{}) {
        window_->unsetCursor();
    } else {
        window_->setCursor(cursorForEdges(edges));
    }
}

Qt::Edges FramelessWindowController::edgesAtGlobalPosition(
    const QPoint& globalPosition) const
{
    return edgesAt(window_->size(), window_->mapFromGlobal(globalPosition), resizeMargin_);
}

} // namespace mnce
