#include "ui/windows-window-frame-adapter.h"

#include "ui/frameless-window-controller.h"
#include "ui/window-title-bar.h"

#include <QCursor>
#include <QEvent>
#include <QWidget>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace mnce {
namespace {

constexpr UINT frameChangedFlags = SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE
    | SWP_NOZORDER | SWP_NOACTIVATE;

qintptr nativeHitForEdges(Qt::Edges edges)
{
    if (edges == (Qt::TopEdge | Qt::LeftEdge)) {
        return HTTOPLEFT;
    }
    if (edges == (Qt::TopEdge | Qt::RightEdge)) {
        return HTTOPRIGHT;
    }
    if (edges == (Qt::BottomEdge | Qt::LeftEdge)) {
        return HTBOTTOMLEFT;
    }
    if (edges == (Qt::BottomEdge | Qt::RightEdge)) {
        return HTBOTTOMRIGHT;
    }
    if (edges.testFlag(Qt::LeftEdge)) {
        return HTLEFT;
    }
    if (edges.testFlag(Qt::RightEdge)) {
        return HTRIGHT;
    }
    if (edges.testFlag(Qt::TopEdge)) {
        return HTTOP;
    }
    return HTBOTTOM;
}

bool isWindowsMessageEvent(const QByteArray& eventType)
{
    return eventType == QByteArrayLiteral("windows_generic_MSG")
        || eventType == QByteArrayLiteral("windows_dispatcher_MSG");
}

} // namespace

WindowsWindowFrameAdapter::WindowsWindowFrameAdapter(
    QWidget* window, WindowTitleBar* titleBar,
    FramelessWindowController* controller, QObject* parent)
    : QObject(parent)
    , window_(window)
    , titleBar_(titleBar)
    , controller_(controller)
{
    Q_ASSERT(window_ != nullptr);
    Q_ASSERT(titleBar_ != nullptr);
    Q_ASSERT(controller_ != nullptr);

    frameApplied_ = applyNativeFrame();
    window_->installEventFilter(this);
}

bool WindowsWindowFrameAdapter::applyNativeFrame()
{
    const auto handle = reinterpret_cast<HWND>(window_->winId());
    if (handle == nullptr) {
        nativeHandle_ = 0;
        frameApplied_ = false;
        return false;
    }
    nativeHandle_ = reinterpret_cast<quintptr>(handle);

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR style = GetWindowLongPtrW(handle, GWL_STYLE);
    if (style == 0 && GetLastError() != ERROR_SUCCESS) {
        frameApplied_ = false;
        return false;
    }

    const LONG_PTR customFrameStyle = style
        & ~static_cast<LONG_PTR>(WS_CAPTION | WS_POPUP);
    if (customFrameStyle != style) {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previousStyle = SetWindowLongPtrW(
            handle, GWL_STYLE, customFrameStyle);
        if (previousStyle == 0 && GetLastError() != ERROR_SUCCESS) {
            frameApplied_ = false;
            return false;
        }
    }
    if (SetWindowPos(handle, nullptr, 0, 0, 0, 0, frameChangedFlags) == FALSE) {
        frameApplied_ = false;
        return false;
    }

    frameApplied_ = true;
    return true;
}

bool WindowsWindowFrameAdapter::frameApplied() const
{
    return frameApplied_;
}

qintptr WindowsWindowFrameAdapter::nativeHitTestAt(
    const QPoint& globalPosition) const
{
    const QPoint windowPosition = window_->mapFromGlobal(globalPosition);
    if (!window_->rect().contains(windowPosition)) {
        return HTNOWHERE;
    }
    if (!window_->isMaximized()) {
        const Qt::Edges edges = FramelessWindowController::edgesAt(
            window_->size(), windowPosition, controller_->resizeMargin());
        if (edges != Qt::Edges{}) {
            return nativeHitForEdges(edges);
        }
    }

    const QPoint titlePosition = titleBar_->mapFrom(window_, windowPosition);
    if (titleBar_->isDraggableAt(titlePosition)) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

bool WindowsWindowFrameAdapter::handleNativeEvent(
    const QByteArray& eventType, void* message, qintptr* result)
{
    if (!isWindowsMessageEvent(eventType) || message == nullptr || result == nullptr) {
        return false;
    }

    const auto* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage->hwnd != reinterpret_cast<HWND>(nativeHandle_)) {
        return false;
    }

    if (nativeMessage->message == WM_NCHITTEST) {
        const qintptr hit = nativeHitTestAt(QCursor::pos());
        if (hit == HTNOWHERE) {
            return false;
        }
        *result = hit;
        return true;
    }

    if ((nativeMessage->message == WM_NCRBUTTONDOWN
         || nativeMessage->message == WM_NCRBUTTONUP
         || nativeMessage->message == WM_NCRBUTTONDBLCLK)
        && nativeMessage->wParam == HTCAPTION) {
        *result = 0;
        return true;
    }

    return false;
}

bool WindowsWindowFrameAdapter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_ && event->type() == QEvent::Show) {
        frameApplied_ = applyNativeFrame();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace mnce
