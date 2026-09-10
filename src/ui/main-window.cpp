#include "ui/main-window.h"

#include <QLabel>
#include <Qt>

namespace mnce {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("MNCE"));
    resize(960, 640);

    auto* placeholder = new QLabel(QStringLiteral("MNCE 桌面应用正在开发中"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}

} // namespace mnce
