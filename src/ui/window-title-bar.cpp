#include "ui/window-title-bar.h"

#include <QApplication>
#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>

namespace mnce {
namespace {

constexpr int titleBarHeight = 36;
constexpr int windowButtonWidth = 46;

void configureButton(QPushButton* button, const QString& objectName,
                     const QString& accessibleName)
{
    button->setObjectName(objectName);
    button->setAccessibleName(accessibleName);
    button->setFlat(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(windowButtonWidth, titleBarHeight);
}

} // namespace

WindowTitleBar::WindowTitleBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("windowTitleBar"));
    setFixedHeight(titleBarHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 0, 0);
    layout->setSpacing(0);

    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("windowTitleLabel"));
    titleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
    titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(titleLabel_);

    minimizeButton_ = new QPushButton(this);
    configureButton(minimizeButton_, QStringLiteral("windowMinimizeButton"),
                    tr("最小化"));
    minimizeButton_->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    layout->addWidget(minimizeButton_);

    maximizeRestoreButton_ = new QPushButton(this);
    configureButton(maximizeRestoreButton_,
                    QStringLiteral("windowMaximizeRestoreButton"), tr("最大化"));
    maximizeRestoreButton_->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    layout->addWidget(maximizeRestoreButton_);

    closeButton_ = new QPushButton(this);
    configureButton(closeButton_, QStringLiteral("windowCloseButton"), tr("关闭"));
    closeButton_->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    layout->addWidget(closeButton_);

    connect(minimizeButton_, &QPushButton::clicked,
            this, &WindowTitleBar::minimizeRequested);
    connect(maximizeRestoreButton_, &QPushButton::clicked,
            this, &WindowTitleBar::maximizeRestoreRequested);
    connect(closeButton_, &QPushButton::clicked,
            this, &WindowTitleBar::closeRequested);
}

void WindowTitleBar::setTitle(const QString& title)
{
    titleLabel_->setText(title);
}

void WindowTitleBar::setWindowState(bool maximized, bool canMinimize,
                                    bool canMaximize, bool canClose)
{
    minimizeButton_->setEnabled(canMinimize);
    maximizeRestoreButton_->setEnabled(canMaximize);
    closeButton_->setEnabled(canClose);
    maximizeRestoreButton_->setAccessibleName(maximized ? tr("还原") : tr("最大化"));
    maximizeRestoreButton_->setIcon(style()->standardIcon(
        maximized ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
}

bool WindowTitleBar::isDraggableAt(const QPoint& position) const
{
    if (!rect().contains(position)) {
        return false;
    }

    const QWidget* child = childAt(position);
    while (child != nullptr && child != this) {
        if (qobject_cast<const QAbstractButton*>(child) != nullptr
            || child->focusPolicy() != Qt::NoFocus) {
            return false;
        }
        child = child->parentWidget();
    }
    return true;
}

void WindowTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        moveStartPosition_ = event->position().toPoint();
        movePending_ = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WindowTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (movePending_ && event->buttons().testFlag(Qt::LeftButton)
        && (event->position().toPoint() - moveStartPosition_).manhattanLength()
            >= QApplication::startDragDistance()) {
        movePending_ = false;
        emit systemMoveRequested();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void WindowTitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        movePending_ = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void WindowTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        movePending_ = false;
        emit maximizeRestoreRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace mnce
