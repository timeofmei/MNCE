#pragma once

#include <QPoint>
#include <QWidget>

class QLabel;
class QPushButton;

namespace mnce {

class WindowTitleBar final : public QWidget
{
    Q_OBJECT

public:
    explicit WindowTitleBar(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setWindowState(bool maximized, bool canMinimize, bool canMaximize,
                        bool canClose);
    [[nodiscard]] bool isDraggableAt(const QPoint& position) const;

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();
    void systemMoveRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QLabel* titleLabel_ = nullptr;
    QPushButton* minimizeButton_ = nullptr;
    QPushButton* maximizeRestoreButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
    QPoint moveStartPosition_;
    bool movePending_ = false;
};

} // namespace mnce
