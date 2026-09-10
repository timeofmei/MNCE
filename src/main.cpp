#include "ui/main-window.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("timeofmei"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("timeofmei.com"));
    QCoreApplication::setApplicationName(QStringLiteral("MNCE"));

    QApplication application(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("com.timeofmei.mnce"));
    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(dataDirectory)) {
        QMessageBox::critical(nullptr, QStringLiteral("MNCE 无法启动"),
                              QStringLiteral("无法创建应用数据目录：\n%1").arg(dataDirectory));
        return 1;
    }
    const QString databasePath = QDir(dataDirectory).filePath(QStringLiteral("mnce.sqlite3"));

    mnce::MainWindow mainWindow;
    QString error;
    if (!mainWindow.initialize(databasePath, {}, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("MNCE 无法启动"),
                              QStringLiteral("无法打开媒体库数据库：\n%1\n\n%2")
                                  .arg(databasePath, error));
        return 1;
    }
    mainWindow.show();
    return application.exec();
}
