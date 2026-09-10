#include "domain/natural-sort.h"
#include "persistence/media-repository.h"
#include "persistence/series-repository.h"
#include "services/file-hash-service.h"
#include "services/series-scan-service.h"
#include "services/series-service.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class SeriesServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void migratesVersionOneAndPreservesSingleFiles();
    void failedMigrationRollsBack();
    void naturalSortIsDeterministic();
    void scanFiltersSortsAndDeduplicates();
    void validatesNamesAndSeparatesLanguagesAndContainers();
    void refreshTracksNewMissingChangedAndDuplicateFallback();
    void missingDirectoryPreservesMediaAndRelocationRestoresIt();
    void deleteCascadesWithoutTouchingOtherContainersOrFiles();
    void cancellationDoesNotApplyPartialRefresh();
    void stalePreparedRefreshIsRejected();
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

bool createVersionOneDatabase(const QString& path, bool addConflictingSeriesTable = false)
{
    const QString connection = QUuid::createUuid().toString();
    bool succeeded = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            succeeded = query.exec(QStringLiteral(
                "CREATE TABLE media_items ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, content_hash BLOB NOT NULL, "
                "file_size INTEGER NOT NULL CHECK (file_size >= 0), current_path TEXT NOT NULL, "
                "display_name TEXT NOT NULL, target_language_id TEXT NOT NULL, "
                "file_state TEXT NOT NULL, transcription_state TEXT NOT NULL, "
                "created_at TEXT NOT NULL, updated_at TEXT NOT NULL, "
                "UNIQUE (target_language_id, content_hash))"))
                && query.exec(QStringLiteral(
                    "INSERT INTO media_items (content_hash, file_size, current_path, display_name, "
                    "target_language_id, file_state, transcription_state, created_at, updated_at) "
                    "VALUES (zeroblob(32), 3, 'old.mp3', 'old.mp3', 'en', 'available', "
                    "'not_started', '2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z')"));
            if (succeeded && addConflictingSeriesTable) {
                succeeded = query.exec(QStringLiteral("CREATE TABLE media_series (bad INTEGER)"));
            }
            succeeded = succeeded && query.exec(QStringLiteral("PRAGMA user_version = 1"));
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return succeeded;
}

int userVersion(const QString& path)
{
    const QString connection = QUuid::createUuid().toString();
    int result = -1;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
                result = query.value(0).toInt();
            }
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

mnce::MediaItem singleItem(const QString& path, const QByteArray& hash)
{
    mnce::MediaItem item;
    item.contentHash = hash;
    item.fileSize = QFileInfo(path).size();
    item.currentPath = path;
    item.displayName = QFileInfo(path).fileName();
    item.targetLanguageId = QStringLiteral("en");
    return item;
}

mnce::SeriesOperationResult createSeries(
    const mnce::SeriesService& service, mnce::SeriesRepository& repository,
    const mnce::CreateSeriesRequest& request,
    const std::atomic_bool* stopRequested = nullptr)
{
    const auto prepared = service.prepareCreate(request, stopRequested);
    return service.apply(repository, prepared);
}

mnce::SeriesOperationResult refreshSeries(
    const mnce::SeriesService& service, mnce::SeriesRepository& repository,
    qint64 seriesId, const std::atomic_bool* stopRequested = nullptr)
{
    mnce::MediaSeries captured;
    QString error;
    if (!repository.find(seriesId, &captured, &error)) {
        return {mnce::SeriesOperationStatus::PersistenceFailed, seriesId, {}, error};
    }
    const auto prepared = service.prepareRefresh(captured, stopRequested);
    return service.apply(repository, prepared);
}

mnce::SeriesOperationResult relocateSeries(
    const mnce::SeriesService& service, mnce::SeriesRepository& repository,
    qint64 seriesId, const QString& newDirectory)
{
    mnce::MediaSeries captured;
    QString error;
    if (!repository.find(seriesId, &captured, &error)) {
        return {mnce::SeriesOperationStatus::PersistenceFailed, seriesId, {}, error};
    }
    const auto prepared = service.prepareRelocation(captured, newDirectory);
    return service.apply(repository, prepared);
}

} // namespace

void SeriesServiceTest::migratesVersionOneAndPreservesSingleFiles()
{
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("v1.sqlite3"));
    QVERIFY(createVersionOneDatabase(path));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::SeriesRepository repository(catalog);
    QString error;
    QVERIFY2(repository.open(path, &error), qPrintable(error));
    QCOMPARE(userVersion(path), 2);
    repository.close();

    mnce::MediaRepository singles(catalog);
    QVERIFY2(singles.open(path, &error), qPrintable(error));
    const auto items = singles.itemsForLanguage(QStringLiteral("en"), &error);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.constFirst().displayName, QStringLiteral("old.mp3"));
    singles.close();

    QVERIFY2(repository.open(path, &error), qPrintable(error));
    QCOMPARE(userVersion(path), 2);
}

void SeriesServiceTest::failedMigrationRollsBack()
{
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("broken.sqlite3"));
    QVERIFY(createVersionOneDatabase(path, true));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::SeriesRepository repository(catalog);
    QString error;
    QVERIFY(!repository.open(path, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(userVersion(path), 1);

    const QString connection = QUuid::createUuid().toString();
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM media_items")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='series_media_items'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
    }
    QSqlDatabase::removeDatabase(connection);
}

void SeriesServiceTest::naturalSortIsDeterministic()
{
    QVERIFY(mnce::compareNatural(u"Lesson 2", u"lesson 10") < 0);
    QCOMPARE(mnce::compareNatural(u"LESSON 02", u"lesson 2"), 0);
    QVERIFY(mnce::compareNatural(u"第２课", u"第10课") < 0);

    QVector<mnce::SeriesFileSnapshot> files = {
        {{}, 0, QStringLiteral("c"), QStringLiteral("lesson10.mp3")},
        {{}, 0, QStringLiteral("b"), QStringLiteral("Lesson2.mp3")},
        {{}, 0, QStringLiteral("a"), QStringLiteral("lesson1.mp3")},
    };
    mnce::SeriesScanService::sortNaturally(files);
    QCOMPARE(files.at(0).displayName, QStringLiteral("lesson1.mp3"));
    QCOMPARE(files.at(1).displayName, QStringLiteral("Lesson2.mp3"));
    QCOMPARE(files.at(2).displayName, QStringLiteral("lesson10.mp3"));
    mnce::SeriesScanService::sortNaturally(files, mnce::SortDirection::Descending);
    QCOMPARE(files.constFirst().displayName, QStringLiteral("lesson10.mp3"));
}

void SeriesServiceTest::scanFiltersSortsAndDeduplicates()
{
    QTemporaryDir temporary;
    QVERIFY(QDir(temporary.path()).mkdir(QStringLiteral("nested")));
    QVERIFY(!writeBytes(temporary.path(), QStringLiteral("lesson10.MP3"), "ten").isEmpty());
    QVERIFY(!writeBytes(temporary.path(), QStringLiteral("lesson2.mp3"), "same").isEmpty());
    QVERIFY(!writeBytes(temporary.path(), QStringLiteral("lesson3.wav"), "same").isEmpty());
    QVERIFY(!writeBytes(temporary.path(), QStringLiteral("notes.txt"), "ignored").isEmpty());
    QVERIFY(!writeBytes(QDir(temporary.path()).filePath(QStringLiteral("nested")),
                        QStringLiteral("lesson1.mp3"), "nested").isEmpty());

    const auto result = mnce::SeriesScanService{}.scan(temporary.path());
    QVERIFY2(result.succeeded(), qPrintable(result.message));
    QCOMPARE(result.files.size(), 2);
    QCOMPARE(result.files.at(0).displayName, QStringLiteral("lesson2.mp3"));
    QCOMPARE(result.files.at(1).displayName, QStringLiteral("lesson10.MP3"));
}

void SeriesServiceTest::validatesNamesAndSeparatesLanguagesAndContainers()
{
    QTemporaryDir temporary;
    const QString mediaDirectory = temporary.filePath(QStringLiteral("audio"));
    QVERIFY(QDir().mkpath(mediaDirectory));
    const QString mediaPath = writeBytes(mediaDirectory, QStringLiteral("one.mp3"), "audio");
    const QString databasePath = temporary.filePath(QStringLiteral("library.sqlite3"));
    const mnce::TargetLanguageCatalog catalog({
        {QStringLiteral("en"), QStringLiteral("英语"), true},
        {QStringLiteral("ja"), QStringLiteral("日语"), false},
        {QStringLiteral("de"), QStringLiteral("德语"), false},
    });
    mnce::SeriesRepository series(catalog);
    QString error;
    QVERIFY2(series.open(databasePath, &error), qPrintable(error));
    mnce::SeriesService service;
    const auto empty = createSeries(service, series,
        {QStringLiteral("  "), QStringLiteral("en"), mediaDirectory});
    QCOMPARE(empty.status, mnce::SeriesOperationStatus::PersistenceFailed);
    const auto english = createSeries(service, series,
        {QStringLiteral(" Course "), QStringLiteral("en"), mediaDirectory});
    QVERIFY2(english.succeeded(), qPrintable(english.error));
    QCOMPARE(series.seriesForLanguage(QStringLiteral("en")).constFirst().name,
             QStringLiteral("Course"));
    const auto duplicate = createSeries(service, series,
        {QStringLiteral("course"), QStringLiteral("en"), mediaDirectory});
    QCOMPARE(duplicate.status, mnce::SeriesOperationStatus::PersistenceFailed);
    QVERIFY(createSeries(service, series,
        {QStringLiteral("course"), QStringLiteral("ja"), mediaDirectory}).succeeded());
    QVERIFY(createSeries(service, series,
        {QStringLiteral("Kurs"), QStringLiteral("de"), mediaDirectory}).succeeded());
    QCOMPARE(series.seriesForLanguage(QStringLiteral("en")).size(), 1);
    QCOMPARE(series.seriesForLanguage(QStringLiteral("ja")).size(), 1);
    QCOMPARE(series.seriesForLanguage(QStringLiteral("de")).size(), 1);

    const auto hashed = mnce::FileHashService{}.hashFile(mediaPath);
    mnce::MediaRepository singles(catalog);
    QVERIFY2(singles.open(databasePath, &error), qPrintable(error));
    QCOMPARE(singles.insert(singleItem(mediaPath, hashed.hash)).status,
             mnce::InsertMediaStatus::Inserted);
    QCOMPARE(singles.itemsForLanguage(QStringLiteral("en")).size(), 1);
    QCOMPARE(series.mediaForSeries(english.seriesId).size(), 1);
}

void SeriesServiceTest::refreshTracksNewMissingChangedAndDuplicateFallback()
{
    QTemporaryDir temporary;
    const QString mediaDirectory = temporary.filePath(QStringLiteral("audio"));
    QVERIFY(QDir().mkpath(mediaDirectory));
    const QString first = writeBytes(mediaDirectory, QStringLiteral("lesson2.mp3"), "same");
    const QString duplicate = writeBytes(mediaDirectory, QStringLiteral("lesson10.mp3"), "same");
    const QString changed = writeBytes(mediaDirectory, QStringLiteral("changed.wav"), "old");
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::SeriesRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(temporary.filePath(QStringLiteral("db.sqlite3")), &error));
    mnce::SeriesService service;
    const auto created = createSeries(service, repository,
        {QStringLiteral("Lessons"), QStringLiteral("en"), mediaDirectory});
    QVERIFY(created.succeeded());
    QCOMPARE(repository.mediaForSeries(created.seriesId).size(), 2);

    QVERIFY(QFile::remove(first));
    QVERIFY(!writeBytes(mediaDirectory, QStringLiteral("changed.wav"), "new-value").isEmpty());
    QVERIFY(!writeBytes(mediaDirectory, QStringLiteral("added.opus"), "added").isEmpty());
    const auto refreshed = refreshSeries(service, repository, created.seriesId);
    QVERIFY2(refreshed.succeeded(), qPrintable(refreshed.error));
    const auto items = repository.mediaForSeries(created.seriesId);
    QCOMPARE(items.size(), 4);
    int missing = 0;
    bool fallbackFound = false;
    for (const auto& item : items) {
        if (item.fileState == mnce::FileState::Missing) {
            ++missing;
        }
        if (item.displayName == QStringLiteral("lesson10.mp3")
            && item.fileState == mnce::FileState::Available) {
            fallbackFound = true;
        }
    }
    QCOMPARE(missing, 1);
    QVERIFY(fallbackFound);
    QVERIFY(QFileInfo::exists(duplicate));
    QVERIFY(QFileInfo::exists(changed));
}

void SeriesServiceTest::missingDirectoryPreservesMediaAndRelocationRestoresIt()
{
    QTemporaryDir temporary;
    const QString original = temporary.filePath(QStringLiteral("original"));
    const QString moved = temporary.filePath(QStringLiteral("moved"));
    QVERIFY(QDir().mkpath(original));
    QVERIFY(!writeBytes(original, QStringLiteral("one.mp3"), "audio").isEmpty());
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::SeriesRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(temporary.filePath(QStringLiteral("db.sqlite3")), &error));
    mnce::SeriesService service;
    const auto created = createSeries(service, repository,
        {QStringLiteral("Move"), QStringLiteral("en"), original});
    QVERIFY(created.succeeded());
    QCOMPARE(repository.mediaForSeries(created.seriesId).constFirst().fileState,
             mnce::FileState::Available);
    QVERIFY(QDir().rename(original, moved));

    const auto missing = refreshSeries(service, repository, created.seriesId);
    QCOMPARE(missing.status, mnce::SeriesOperationStatus::DirectoryMarkedMissing);
    mnce::MediaSeries series;
    QVERIFY(repository.find(created.seriesId, &series, &error));
    QCOMPARE(series.directoryState, mnce::DirectoryState::Missing);
    QCOMPARE(repository.mediaForSeries(created.seriesId).constFirst().fileState,
             mnce::FileState::Available);

    const auto relocated = relocateSeries(service, repository, created.seriesId, moved);
    QVERIFY2(relocated.succeeded(), qPrintable(relocated.error));
    QVERIFY(repository.find(created.seriesId, &series, &error));
    QCOMPARE(series.directoryState, mnce::DirectoryState::Available);
    QCOMPARE(series.currentDirectory, QDir::cleanPath(QFileInfo(moved).absoluteFilePath()));
    const auto media = repository.mediaForSeries(created.seriesId);
    QCOMPARE(media.size(), 1);
    QVERIFY(media.constFirst().currentPath.endsWith(QStringLiteral("moved/one.mp3"))
            || media.constFirst().currentPath.endsWith(QStringLiteral("moved\\one.mp3")));
}

void SeriesServiceTest::deleteCascadesWithoutTouchingOtherContainersOrFiles()
{
    QTemporaryDir temporary;
    const QString audio = temporary.filePath(QStringLiteral("audio"));
    QVERIFY(QDir().mkpath(audio));
    const QString file = writeBytes(audio, QStringLiteral("one.mp3"), "audio");
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::SeriesRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(databasePath, &error));
    mnce::SeriesService service;
    const auto first = createSeries(service, repository,
        {QStringLiteral("First"), QStringLiteral("en"), audio});
    const auto second = createSeries(service, repository,
        {QStringLiteral("Second"), QStringLiteral("en"), audio});
    QVERIFY(first.succeeded());
    QVERIFY(second.succeeded());
    const auto hash = mnce::FileHashService{}.hashFile(file);
    mnce::MediaRepository singles(catalog);
    QVERIFY(singles.open(databasePath, &error));
    QVERIFY(singles.insert(singleItem(file, hash.hash)).status == mnce::InsertMediaStatus::Inserted);

    QVERIFY(service.remove(repository, first.seriesId).succeeded());
    QVERIFY(repository.mediaForSeries(first.seriesId).isEmpty());
    QCOMPARE(repository.mediaForSeries(second.seriesId).size(), 1);
    QCOMPARE(singles.itemsForLanguage(QStringLiteral("en")).size(), 1);
    QVERIFY(QFileInfo::exists(file));
}

void SeriesServiceTest::cancellationDoesNotApplyPartialRefresh()
{
    QTemporaryDir temporary;
    const QString audio = temporary.filePath(QStringLiteral("audio"));
    QVERIFY(QDir().mkpath(audio));
    QVERIFY(!writeBytes(audio, QStringLiteral("one.mp3"), "one").isEmpty());
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::SeriesRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(temporary.filePath(QStringLiteral("db.sqlite3")), &error));
    mnce::SeriesService service;
    const auto created = createSeries(service, repository,
        {QStringLiteral("Cancel"), QStringLiteral("en"), audio});
    QVERIFY(created.succeeded());
    QVERIFY(!writeBytes(audio, QStringLiteral("two.mp3"), "two").isEmpty());
    std::atomic_bool stop = true;
    const auto cancelled = refreshSeries(service, repository, created.seriesId, &stop);
    QCOMPARE(cancelled.status, mnce::SeriesOperationStatus::ScanFailed);
    QCOMPARE(repository.mediaForSeries(created.seriesId).size(), 1);
}

void SeriesServiceTest::stalePreparedRefreshIsRejected()
{
    QTemporaryDir temporary;
    const QString firstDirectory = temporary.filePath(QStringLiteral("first"));
    const QString secondDirectory = temporary.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(firstDirectory));
    QVERIFY(QDir().mkpath(secondDirectory));
    QVERIFY(!writeBytes(firstDirectory, QStringLiteral("one.mp3"), "one").isEmpty());
    QVERIFY(!writeBytes(secondDirectory, QStringLiteral("two.mp3"), "two").isEmpty());
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::SeriesRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(temporary.filePath(QStringLiteral("db.sqlite3")), &error));
    mnce::SeriesService service;
    const auto created = createSeries(service, repository,
        {QStringLiteral("Stale"), QStringLiteral("en"), firstDirectory});
    QVERIFY(created.succeeded());

    mnce::MediaSeries captured;
    QVERIFY(repository.find(created.seriesId, &captured, &error));
    const auto staleRefresh = service.prepareRefresh(captured);
    QVERIFY(relocateSeries(service, repository, created.seriesId, secondDirectory).succeeded());
    const auto result = service.apply(repository, staleRefresh);
    QCOMPARE(result.status, mnce::SeriesOperationStatus::PersistenceFailed);
    QVERIFY(result.error.contains(QStringLiteral("发生了变化")));
    const auto media = repository.mediaForSeries(created.seriesId);
    QCOMPARE(media.size(), 2);
    QCOMPARE(media.at(0).fileState, mnce::FileState::Missing);
    QCOMPARE(media.at(1).fileState, mnce::FileState::Available);
}

QTEST_GUILESS_MAIN(SeriesServiceTest)

#include "series-service-test.moc"
