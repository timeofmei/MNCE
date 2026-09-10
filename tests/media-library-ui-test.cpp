#include "ui/main-window.h"
#include "persistence/media-repository.h"

#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

class MediaLibraryUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void showsEmptyLibraryAndCatalogLanguages();
    void restoresSavedLanguageAndFallsBackFromInvalidValue();
    void displaysOnlyTheCurrentLanguageRecords();
    void missingRecordActionsHaveEnoughSpace();
};

void MediaLibraryUiTest::showsEmptyLibraryAndCatalogLanguages()
{
    QTemporaryDir directory;
    mnce::MainWindow window(mnce::TargetLanguageCatalog({
        {QStringLiteral("en"), QStringLiteral("英语"), true},
        {QStringLiteral("ja"), QStringLiteral("日语"), false},
        {QStringLiteral("de"), QStringLiteral("德语"), false},
    }));
    QString error;
    QVERIFY2(window.initialize(directory.filePath(QStringLiteral("db.sqlite3")),
                               directory.filePath(QStringLiteral("settings.ini")), &error),
             qPrintable(error));
    auto* combo = window.findChild<QComboBox*>(QStringLiteral("targetLanguageCombo"));
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("mediaTable"));
    QVERIFY(combo != nullptr);
    QVERIFY(table != nullptr);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("en"));
    QCOMPARE(table->rowCount(), 0);
    QCOMPARE(table->selectionMode(), QAbstractItemView::NoSelection);
    QCOMPARE(table->focusPolicy(), Qt::NoFocus);
}

void MediaLibraryUiTest::restoresSavedLanguageAndFallsBackFromInvalidValue()
{
    QTemporaryDir directory;
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    {
        mnce::MainWindow window;
        QString error;
        QVERIFY(window.initialize(directory.filePath(QStringLiteral("one.sqlite3")), settingsPath, &error));
        auto* combo = window.findChild<QComboBox*>(QStringLiteral("targetLanguageCombo"));
        combo->setCurrentIndex(combo->findData(QStringLiteral("ja")));
    }
    {
        mnce::MainWindow window;
        QString error;
        QVERIFY(window.initialize(directory.filePath(QStringLiteral("one.sqlite3")), settingsPath, &error));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("targetLanguageCombo"))
                     ->currentData().toString(), QStringLiteral("ja"));
    }
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("library/currentTargetLanguage"), QStringLiteral("invalid"));
    }
    {
        mnce::MainWindow window;
        QString error;
        QVERIFY(window.initialize(directory.filePath(QStringLiteral("two.sqlite3")), settingsPath, &error));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("targetLanguageCombo"))
                     ->currentData().toString(), QStringLiteral("en"));
    }
}

void MediaLibraryUiTest::displaysOnlyTheCurrentLanguageRecords()
{
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        mnce::MediaItem english;
        english.contentHash = QByteArray(32, 'e');
        english.fileSize = 1;
        english.currentPath = directory.filePath(QStringLiteral("english.mp3"));
        english.displayName = QStringLiteral("english.mp3");
        english.targetLanguageId = QStringLiteral("en");
        QCOMPARE(repository.insert(english).status, mnce::InsertMediaStatus::Inserted);
        auto japanese = english;
        japanese.contentHash = QByteArray(32, 'j');
        japanese.displayName = QStringLiteral("japanese.mp3");
        japanese.currentPath = directory.filePath(japanese.displayName);
        japanese.targetLanguageId = QStringLiteral("ja");
        QCOMPARE(repository.insert(japanese).status, mnce::InsertMediaStatus::Inserted);
    }

    mnce::MainWindow window;
    QString error;
    QVERIFY(window.initialize(databasePath, directory.filePath(QStringLiteral("settings.ini")), &error));
    auto* combo = window.findChild<QComboBox*>(QStringLiteral("targetLanguageCombo"));
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("mediaTable"));
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("english.mp3"));
    combo->setCurrentIndex(combo->findData(QStringLiteral("ja")));
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("japanese.mp3"));
}

void MediaLibraryUiTest::missingRecordActionsHaveEnoughSpace()
{
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        mnce::MediaItem item;
        item.contentHash = QByteArray(32, 'm');
        item.fileSize = 1;
        item.currentPath = directory.filePath(QStringLiteral("missing.mp3"));
        item.displayName = QStringLiteral("missing.mp3");
        item.targetLanguageId = QStringLiteral("en");
        item.fileState = mnce::FileState::Missing;
        QCOMPARE(repository.insert(item).status, mnce::InsertMediaStatus::Inserted);
    }

    mnce::MainWindow window;
    QString error;
    QVERIFY(window.initialize(databasePath, directory.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("mediaTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 1);
    auto* actions = table->cellWidget(0, 4);
    QVERIFY(actions != nullptr);
    const auto buttons = actions->findChildren<QPushButton*>();
    QCOMPARE(buttons.size(), 2);
    QVERIFY(table->columnWidth(4) >= actions->minimumWidth());
    for (const auto* button : buttons) {
        QVERIFY(button->width() >= button->sizeHint().width());
    }
}

QTEST_MAIN(MediaLibraryUiTest)

#include "media-library-ui-test.moc"
