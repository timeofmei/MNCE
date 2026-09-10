#include "ui/main-window.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    mnce::MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
