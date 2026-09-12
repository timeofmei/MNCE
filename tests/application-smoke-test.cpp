#include "ui/main-window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QString>
#include <QVersionNumber>
#include <QtGlobal>
#include <QtTest>

class ApplicationSmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void constructsApplicationAndMainWindow();
    void showsAndClosesMainWindow();
    void usesExpectedQtRuntime();
    void opensInMemorySqliteDatabase();
};

void ApplicationSmokeTest::constructsApplicationAndMainWindow()
{
    QVERIFY(QCoreApplication::instance() != nullptr);
    QVERIFY(qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr);

    const mnce::MainWindow window;
    QVERIFY(window.windowTitle().contains(QStringLiteral("MNCE")));
}

void ApplicationSmokeTest::showsAndClosesMainWindow()
{
    mnce::MainWindow window;

    window.show();
    QVERIFY(window.isVisible());
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
}

void ApplicationSmokeTest::usesExpectedQtRuntime()
{
    QVERIFY(QVersionNumber::fromString(QString::fromLatin1(qVersion()))
            >= QVersionNumber(6, 10, 2));
}

void ApplicationSmokeTest::opensInMemorySqliteDatabase()
{
    constexpr auto connectionName = "application-smoke-test";

    {
        QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));

        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QString::fromLatin1(connectionName));
        database.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));
        database.close();
    }

    QSqlDatabase::removeDatabase(QString::fromLatin1(connectionName));
}

QTEST_MAIN(ApplicationSmokeTest)

#include "application-smoke-test.moc"
