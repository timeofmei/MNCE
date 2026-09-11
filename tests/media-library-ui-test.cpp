#include "ui/main-window.h"
#include "persistence/media-repository.h"
#include "persistence/series-repository.h"
#include "services/file-hash-service.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class FakeLibraryDialogs final : public mnce::LibraryUiDialogs
{
public:
    QString chooseAudioFile(QWidget*, bool) override
    {
        return audioFiles.isEmpty() ? QString{} : audioFiles.takeFirst();
    }

    QString chooseSeriesDirectory(QWidget*, bool) override
    {
        return directories.isEmpty() ? QString{} : directories.takeFirst();
    }

    std::optional<QString> requestSeriesName(QWidget*) override
    {
        if (names.isEmpty()) {
            return std::nullopt;
        }
        return names.takeFirst();
    }

    bool confirmSeriesDeletion(QWidget*, const QString& seriesName, int itemCount) override
    {
        confirmedSeriesName = seriesName;
        confirmedItemCount = itemCount;
        ++confirmationCount;
        return confirmDeletion;
    }

    void showMessage(QWidget*, mnce::UserMessageKind kind,
                     const QString& title, const QString& text) override
    {
        messages.push_back({kind, title, text});
    }

    struct Message
    {
        mnce::UserMessageKind kind;
        QString title;
        QString text;
    };

    QStringList audioFiles;
    QStringList directories;
    QStringList names;
    QVector<Message> messages;
    bool confirmDeletion = true;
    int confirmationCount = 0;
    QString confirmedSeriesName;
    int confirmedItemCount = -1;
};

class MediaLibraryUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void showsEmptyLibraryAndCatalogLanguages();
    void restoresSavedLanguageAndFallsBackFromInvalidValue();
    void displaysOnlyTheCurrentLanguageRecords();
    void missingRecordActionsHaveEnoughSpace();
    void createsAndRefreshesSeriesThroughTheWindow();
    void seriesTaskKeepsCapturedLanguage();
    void sortsAllListsAndRestoresPreferences();
    void relocatesAndDeletesSeriesThroughTheWindow();
};

namespace {

QString writeBytes(const QString& directory, const QString& name, const QByteArray& bytes)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(bytes) != bytes.size()) {
        return {};
    }
    return path;
}

QPushButton* firstButton(QWidget& window, const QString& objectName)
{
    const auto buttons = window.findChildren<QPushButton*>(objectName);
    return buttons.isEmpty() ? nullptr : buttons.constFirst();
}

mnce::SeriesFileSnapshot snapshot(const QString& path)
{
    const auto identity = mnce::FileHashService{}.hashFile(path);
    return {identity.hash, identity.fileSize,
            QFileInfo(path).absoluteFilePath(), QFileInfo(path).fileName()};
}

} // namespace

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
    auto* actions = table->cellWidget(0, 5);
    QVERIFY(actions != nullptr);
    const auto buttons = actions->findChildren<QPushButton*>();
    QCOMPARE(buttons.size(), 2);
    QVERIFY(table->columnWidth(5) >= actions->minimumWidth());
    for (const auto* button : buttons) {
        QVERIFY(button->width() >= button->sizeHint().width());
    }
}

void MediaLibraryUiTest::createsAndRefreshesSeriesThroughTheWindow()
{
    QTemporaryDir temporary;
    const QString audio = temporary.filePath(QStringLiteral("audio"));
    QVERIFY(QDir().mkpath(QDir(audio).filePath(QStringLiteral("nested"))));
    const QString lesson2 = writeBytes(audio, QStringLiteral("lesson2.mp3"), "two");
    QVERIFY(!lesson2.isEmpty());
    QVERIFY(!writeBytes(audio, QStringLiteral("lesson10.MP3"), "ten").isEmpty());
    QVERIFY(!writeBytes(audio, QStringLiteral("notes.txt"), "ignored").isEmpty());
    QVERIFY(!writeBytes(QDir(audio).filePath(QStringLiteral("nested")),
                        QStringLiteral("lesson1.mp3"), "nested").isEmpty());

    auto dialogs = std::make_shared<FakeLibraryDialogs>();
    dialogs->directories << audio;
    dialogs->names << QStringLiteral(" Lessons ");
    mnce::MainWindow window(mnce::TargetLanguageCatalog::builtIn(), dialogs);
    QString error;
    QVERIFY(window.initialize(temporary.filePath(QStringLiteral("db.sqlite3")),
                              temporary.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("libraryTabs"));
    auto* add = window.findChild<QPushButton*>(QStringLiteral("addFolderButton"));
    auto* seriesTable = window.findChild<QTableWidget*>(QStringLiteral("seriesTable"));
    auto* mediaTable = window.findChild<QTableWidget*>(QStringLiteral("seriesMediaTable"));
    QVERIFY(tabs != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(seriesTable != nullptr);
    QVERIFY(mediaTable != nullptr);
    tabs->setCurrentIndex(1);
    QTRY_VERIFY(add->isEnabled());
    QTest::mouseClick(add, Qt::LeftButton);
    QTRY_COMPARE(seriesTable->rowCount(), 1);
    QTRY_COMPARE(mediaTable->rowCount(), 2);
    QCOMPARE(seriesTable->item(0, 0)->text(), QStringLiteral("Lessons"));
    QCOMPARE(mediaTable->item(0, 0)->text(), QStringLiteral("lesson2.mp3"));
    QCOMPARE(mediaTable->item(1, 0)->text(), QStringLiteral("lesson10.MP3"));

    QVERIFY(QFile::remove(lesson2));
    QVERIFY(!writeBytes(audio, QStringLiteral("lesson3.opus"), "three").isEmpty());
    auto* refresh = window.findChild<QPushButton*>(QStringLiteral("seriesRefreshButton"));
    QVERIFY(refresh != nullptr);
    QTRY_VERIFY(refresh->isEnabled());
    QTest::mouseClick(refresh, Qt::LeftButton);
    QTRY_COMPARE(mediaTable->rowCount(), 3);
    bool missingFound = false;
    bool newFound = false;
    for (int row = 0; row < mediaTable->rowCount(); ++row) {
        missingFound = missingFound
            || (mediaTable->item(row, 0)->text() == QStringLiteral("lesson2.mp3")
                && mediaTable->item(row, 2)->text() == QStringLiteral("文件缺失"));
        newFound = newFound
            || mediaTable->item(row, 0)->text() == QStringLiteral("lesson3.opus");
    }
    QVERIFY(missingFound);
    QVERIFY(newFound);
}

void MediaLibraryUiTest::seriesTaskKeepsCapturedLanguage()
{
    QTemporaryDir temporary;
    const QString audio = temporary.filePath(QStringLiteral("audio"));
    QVERIFY(QDir().mkpath(audio));
    QVERIFY(!writeBytes(audio, QStringLiteral("lesson.mp3"), QByteArray(4 * 1024 * 1024, 'x')).isEmpty());
    auto dialogs = std::make_shared<FakeLibraryDialogs>();
    dialogs->directories << audio;
    dialogs->names << QStringLiteral("English course");
    mnce::MainWindow window(mnce::TargetLanguageCatalog::builtIn(), dialogs);
    QString error;
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    QVERIFY(window.initialize(databasePath, temporary.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    auto* language = window.findChild<QComboBox*>(QStringLiteral("targetLanguageCombo"));
    auto* add = window.findChild<QPushButton*>(QStringLiteral("addFolderButton"));
    auto* seriesTable = window.findChild<QTableWidget*>(QStringLiteral("seriesTable"));
    QTRY_VERIFY(add->isEnabled());
    QTest::mouseClick(add, Qt::LeftButton);
    language->setCurrentIndex(language->findData(QStringLiteral("ja")));
    QTRY_VERIFY(add->isEnabled());
    QCOMPARE(seriesTable->rowCount(), 0);
    language->setCurrentIndex(language->findData(QStringLiteral("en")));
    QCOMPARE(seriesTable->rowCount(), 1);

    mnce::SeriesRepository repository(mnce::TargetLanguageCatalog::builtIn());
    QVERIFY(repository.open(databasePath, &error));
    QCOMPARE(repository.seriesForLanguage(QStringLiteral("en")).size(), 1);
    QCOMPARE(repository.seriesForLanguage(QStringLiteral("ja")).size(), 0);
}

void MediaLibraryUiTest::sortsAllListsAndRestoresPreferences()
{
    QTemporaryDir temporary;
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    const QString settingsPath = temporary.filePath(QStringLiteral("settings.ini"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        mnce::MediaItem item;
        item.fileSize = 1;
        item.targetLanguageId = QStringLiteral("en");
        item.contentHash = QByteArray(32, 'a');
        item.currentPath = temporary.filePath(QStringLiteral("lesson10.mp3"));
        item.displayName = QStringLiteral("lesson10.mp3");
        QVERIFY(repository.insert(item).status == mnce::InsertMediaStatus::Inserted);
        item.contentHash = QByteArray(32, 'b');
        item.currentPath = temporary.filePath(QStringLiteral("lesson2.mp3"));
        item.displayName = QStringLiteral("lesson2.mp3");
        QVERIFY(repository.insert(item).status == mnce::InsertMediaStatus::Inserted);
    }
    {
        const QString mediaDirectory = temporary.filePath(QStringLiteral("series"));
        QVERIFY(QDir().mkpath(mediaDirectory));
        const QString ten = writeBytes(mediaDirectory, QStringLiteral("part10.mp3"), "ten");
        const QString two = writeBytes(mediaDirectory, QStringLiteral("part2.mp3"), "two");
        mnce::SeriesRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        QVERIFY(repository.create(QStringLiteral("Beta"), QStringLiteral("en"), mediaDirectory,
                                  {snapshot(ten), snapshot(two)}).succeeded());
        QVERIFY(repository.create(QStringLiteral("Alpha"), QStringLiteral("en"), mediaDirectory,
                                  {}).succeeded());
    }

    auto dialogs = std::make_shared<FakeLibraryDialogs>();
    {
        mnce::MainWindow window(catalog, dialogs);
        QString error;
        QVERIFY(window.initialize(databasePath, settingsPath, &error));
        window.show();
        auto* singleField = window.findChild<QComboBox*>(QStringLiteral("singleSortFieldCombo"));
        auto* singleDirection = window.findChild<QComboBox*>(QStringLiteral("singleSortDirectionCombo"));
        auto* seriesField = window.findChild<QComboBox*>(QStringLiteral("seriesSortFieldCombo"));
        auto* seriesDirection = window.findChild<QComboBox*>(QStringLiteral("seriesSortDirectionCombo"));
        auto* mediaDirection = window.findChild<QComboBox*>(QStringLiteral("seriesMediaSortDirectionCombo"));
        auto* singleTable = window.findChild<QTableWidget*>(QStringLiteral("mediaTable"));
        auto* seriesTable = window.findChild<QTableWidget*>(QStringLiteral("seriesTable"));
        auto* mediaTable = window.findChild<QTableWidget*>(QStringLiteral("seriesMediaTable"));
        QTRY_VERIFY(window.findChild<QPushButton*>(QStringLiteral("addFolderButton"))->isEnabled());
        singleField->setCurrentIndex(singleField->findData(QStringLiteral("name")));
        singleDirection->setCurrentIndex(singleDirection->findData(QStringLiteral("ascending")));
        QCOMPARE(singleTable->item(0, 0)->text(), QStringLiteral("lesson2.mp3"));
        seriesField->setCurrentIndex(seriesField->findData(QStringLiteral("name")));
        seriesDirection->setCurrentIndex(seriesDirection->findData(QStringLiteral("descending")));
        QCOMPARE(seriesTable->item(0, 0)->text(), QStringLiteral("Beta"));
        QVERIFY(window.findChildren<QPushButton*>(QStringLiteral("openSeriesButton")).isEmpty());
        const QRect nameCell = seriesTable->visualItemRect(seriesTable->item(0, 0));
        QTest::mouseClick(seriesTable->viewport(), Qt::LeftButton, Qt::NoModifier,
                          nameCell.center());
        QVERIFY(seriesTable->isEnabled());
        QTRY_COMPARE(mediaTable->rowCount(), 2);
        QCOMPARE(mediaTable->item(0, 0)->text(), QStringLiteral("part2.mp3"));
        mediaDirection->setCurrentIndex(mediaDirection->findData(QStringLiteral("descending")));
        QCOMPARE(mediaTable->item(0, 0)->text(), QStringLiteral("part10.mp3"));
        QTRY_VERIFY(seriesTable->isEnabled());
        const QRect otherNameCell = seriesTable->visualItemRect(seriesTable->item(1, 0));
        QTest::mouseClick(seriesTable->viewport(), Qt::LeftButton, Qt::NoModifier,
                          otherNameCell.center());
        QTRY_COMPARE(mediaTable->rowCount(), 0);
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("seriesDetailName"))->text(),
                 QStringLiteral("系列：Alpha"));
        auto* language = window.findChild<QComboBox*>(QStringLiteral("targetLanguageCombo"));
        language->setCurrentIndex(language->findData(QStringLiteral("ja")));
        QCOMPARE(singleField->currentData().toString(), QStringLiteral("name"));
        QCOMPARE(seriesDirection->currentData().toString(), QStringLiteral("descending"));
    }
    {
        mnce::MainWindow window(catalog, dialogs);
        QString error;
        QVERIFY(window.initialize(databasePath, settingsPath, &error));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("singleSortFieldCombo"))
                     ->currentData().toString(), QStringLiteral("name"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("singleSortDirectionCombo"))
                     ->currentData().toString(), QStringLiteral("ascending"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("seriesSortFieldCombo"))
                     ->currentData().toString(), QStringLiteral("name"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("seriesSortDirectionCombo"))
                     ->currentData().toString(), QStringLiteral("descending"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("seriesMediaSortDirectionCombo"))
                     ->currentData().toString(), QStringLiteral("descending"));
    }
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("library/singleFiles/sortField"), QStringLiteral("invalid"));
        settings.setValue(QStringLiteral("library/singleFiles/sortDirection"), QStringLiteral("invalid"));
        settings.setValue(QStringLiteral("library/series/sortField"), QStringLiteral("invalid"));
        settings.setValue(QStringLiteral("library/series/sortDirection"), QStringLiteral("invalid"));
        settings.setValue(QStringLiteral("library/seriesMedia/sortDirection"), QStringLiteral("invalid"));
    }
    {
        mnce::MainWindow window(catalog, dialogs);
        QString error;
        QVERIFY(window.initialize(databasePath, settingsPath, &error));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("singleSortFieldCombo"))
                     ->currentData().toString(), QStringLiteral("created_at"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("singleSortDirectionCombo"))
                     ->currentData().toString(), QStringLiteral("descending"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("seriesSortFieldCombo"))
                     ->currentData().toString(), QStringLiteral("created_at"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("seriesSortDirectionCombo"))
                     ->currentData().toString(), QStringLiteral("descending"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("seriesMediaSortDirectionCombo"))
                     ->currentData().toString(), QStringLiteral("ascending"));
    }
}

void MediaLibraryUiTest::relocatesAndDeletesSeriesThroughTheWindow()
{
    QTemporaryDir temporary;
    const QString original = temporary.filePath(QStringLiteral("original"));
    const QString moved = temporary.filePath(QStringLiteral("moved"));
    QVERIFY(QDir().mkpath(original));
    QVERIFY(!writeBytes(original, QStringLiteral("one.mp3"), "audio").isEmpty());
    auto dialogs = std::make_shared<FakeLibraryDialogs>();
    dialogs->directories << original;
    dialogs->names << QStringLiteral("Movable");
    mnce::MainWindow window(mnce::TargetLanguageCatalog::builtIn(), dialogs);
    QString error;
    QVERIFY(window.initialize(temporary.filePath(QStringLiteral("db.sqlite3")),
                              temporary.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    auto* add = window.findChild<QPushButton*>(QStringLiteral("addFolderButton"));
    auto* seriesTable = window.findChild<QTableWidget*>(QStringLiteral("seriesTable"));
    auto* refresh = window.findChild<QPushButton*>(QStringLiteral("seriesRefreshButton"));
    auto* relocate = window.findChild<QPushButton*>(QStringLiteral("seriesRelocateButton"));
    window.findChild<QTabWidget*>(QStringLiteral("libraryTabs"))->setCurrentIndex(1);
    QTRY_VERIFY(add->isEnabled());
    QTest::mouseClick(add, Qt::LeftButton);
    QTRY_COMPARE(seriesTable->rowCount(), 1);
    QVERIFY(QDir().rename(original, moved));
    QTest::mouseClick(refresh, Qt::LeftButton);
    QTRY_COMPARE(seriesTable->item(0, 1)->text(), QStringLiteral("文件夹缺失"));

    dialogs->directories << moved;
    QTest::mouseClick(relocate, Qt::LeftButton);
    QTRY_COMPARE(seriesTable->item(0, 1)->text(), QStringLiteral("可用"));
    auto* pathLabel = window.findChild<QLabel*>(QStringLiteral("seriesDirectoryLabel"));
    QVERIFY(pathLabel->text().contains(QStringLiteral("moved")));

    dialogs->confirmDeletion = false;
    auto* remove = firstButton(window, QStringLiteral("deleteSeriesButton"));
    QVERIFY(remove != nullptr);
    QTest::mouseClick(remove, Qt::LeftButton);
    QCOMPARE(seriesTable->rowCount(), 1);
    QCOMPARE(dialogs->confirmationCount, 1);
    QCOMPARE(dialogs->confirmedSeriesName, QStringLiteral("Movable"));
    QCOMPARE(dialogs->confirmedItemCount, 1);
    dialogs->confirmDeletion = true;
    QTest::mouseClick(remove, Qt::LeftButton);
    QCOMPARE(seriesTable->rowCount(), 0);
    QVERIFY(QFileInfo::exists(QDir(moved).filePath(QStringLiteral("one.mp3"))));
}

QTEST_MAIN(MediaLibraryUiTest)

#include "media-library-ui-test.moc"
