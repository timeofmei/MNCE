#pragma once

#include <QByteArray>
#include <QObject>
#include <QPoint>

class QWidget;

namespace mnce {

class FramelessWindowController;
class WindowTitleBar;

class WindowsWindowFrameAdapter final : public QObject
{
public:
    explicit WindowsWindowFrameAdapter(QWidget* window, WindowTitleBar* titleBar,
                                       FramelessWindowController* controller,
                                       QObject* parent = nullptr);

    [[nodiscard]] bool applyNativeFrame();
    [[nodiscard]] bool frameApplied() const;
    [[nodiscard]] qintptr nativeHitTestAt(const QPoint& globalPosition) const;
    bool handleNativeEvent(const QByteArray& eventType, void* message, qintptr* result);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* window_ = nullptr;
    WindowTitleBar* titleBar_ = nullptr;
    FramelessWindowController* controller_ = nullptr;
    quintptr nativeHandle_ = 0;
    bool frameApplied_ = false;
};

} // namespace mnce
