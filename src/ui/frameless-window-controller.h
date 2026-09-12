#pragma once

#include <QObject>
#include <Qt>

class QWidget;

namespace mnce {

class WindowTitleBar;

class WindowSystemOperations
{
public:
    virtual ~WindowSystemOperations() = default;

    virtual bool startSystemMove(QWidget* window) = 0;
    virtual bool startSystemResize(QWidget* window, Qt::Edges edges) = 0;
    virtual void showMinimized(QWidget* window) = 0;
    virtual void showMaximized(QWidget* window) = 0;
    virtual void showNormal(QWidget* window) = 0;
    virtual void close(QWidget* window) = 0;
};

class FramelessWindowController final : public QObject
{
    Q_OBJECT

public:
    explicit FramelessWindowController(QWidget* window, WindowTitleBar* titleBar,
                                       QObject* parent = nullptr,
                                       WindowSystemOperations* operations = nullptr);

    [[nodiscard]] static bool enabledForCurrentPlatform();
    static void applyInitialWindowFlags(QWidget* window);
    [[nodiscard]] static Qt::Edges edgesAt(const QSize& windowSize,
                                           const QPoint& position,
                                           int margin);
    [[nodiscard]] int resizeMargin() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void requestSystemMove();
    void toggleMaximized();
    void syncWindowState();
    void updateResizeCursor(const QPoint& globalPosition);
    [[nodiscard]] Qt::Edges edgesAtGlobalPosition(const QPoint& globalPosition) const;

    QWidget* window_ = nullptr;
    WindowTitleBar* titleBar_ = nullptr;
    WindowSystemOperations* operations_ = nullptr;
    int resizeMargin_ = 6;
};

} // namespace mnce
