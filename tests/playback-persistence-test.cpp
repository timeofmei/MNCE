#include "persistence/media-repository.h"
#include "persistence/series-repository.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class PlaybackPersistenceTest final : public QObject
{
    Q_OBJECT

private slots:
    void migratesVersionTwoAndPreservesBothMediaKinds();
    void failedVersionThreeMigrationRollsBack();
    void persistsAndValidatesDurations();
};

static bool createVersionTwoDatabase(const QString& path, bool seriesAlreadyHasDuration = false)
{
    const QString connection = QUuid::createUuid().toString();
    bool ok = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            ok = query.exec(QStringLiteral(
                "CREATE TABLE media_items (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "content_hash BLOB NOT NULL, file_size INTEGER NOT NULL CHECK(file_size >= 0), "
                "current_path TEXT NOT NULL, display_name TEXT NOT NULL, "
                "target_language_id TEXT NOT NULL, file_state TEXT NOT NULL, "
                "transcription_state TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL, "
                "UNIQUE(target_language_id, content_hash))"))
                && query.exec(QStringLiteral(
                "CREATE TABLE media_series (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
                "normalized_name TEXT NOT NULL, target_language_id TEXT NOT NULL, "
                "current_directory TEXT NOT NULL, directory_state TEXT NOT NULL, "
                "created_at TEXT NOT NULL, updated_at TEXT NOT NULL, "
                "UNIQUE(target_language_id, normalized_name))"));
            const QString durationColumn = seriesAlreadyHasDuration
                ? QStringLiteral(", duration_ms INTEGER NULL CHECK(duration_ms >= 0)") : QString();
            ok = ok && query.exec(QStringLiteral(
                "CREATE TABLE series_media_items (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "series_id INTEGER NOT NULL REFERENCES media_series(id), content_hash BLOB NOT NULL, "
                "file_size INTEGER NOT NULL, current_path TEXT NOT NULL, display_name TEXT NOT NULL, "
                "file_state TEXT NOT NULL, transcription_state TEXT NOT NULL%1, "
                "created_at TEXT NOT NULL, updated_at TEXT NOT NULL, UNIQUE(series_id, content_hash))")
                .arg(durationColumn));
            ok = ok && query.exec(QStringLiteral(
                "INSERT INTO media_items VALUES (11, zeroblob(32), 10, 'single.mp3', 'single.mp3', "
                "'en', 'available', 'not_started', 'old-created', 'old-updated')"))
                && query.exec(QStringLiteral(
                "INSERT INTO media_series VALUES (21, 'Lessons', 'lessons', 'en', '.', "
                "'available', 'old-created', 'old-updated')"));
            if (!seriesAlreadyHasDuration) {
                ok = ok && query.exec(QStringLiteral(
                    "INSERT INTO series_media_items VALUES (31, 21, zeroblob(32), 10, "
                    "'series.mp3', 'series.mp3', 'available', 'not_started', "
                    "'old-created', 'old-updated')"));
            }
            ok = ok && query.exec(QStringLiteral("PRAGMA user_version = 2"));
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

static int userVersion(const QString& path)
{
    const QString connection = QUuid::createUuid().toString();
    int version = -1;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
                version = query.value(0).toInt();
            }
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return version;
}

static bool hasColumn(const QString& path, const QString& table, const QString& column)
{
    const QString connection = QUuid::createUuid().toString();
    bool found = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
                while (query.next()) found = found || query.value(1).toString() == column;
            }
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return found;
}

void PlaybackPersistenceTest::migratesVersionTwoAndPreservesBothMediaKinds()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("v2.sqlite3"));
    QVERIFY(createVersionTwoDatabase(path));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository singles(catalog);
    QString error;
    QVERIFY2(singles.open(path, &error), qPrintable(error));
    QCOMPARE(userVersion(path), 3);
    const auto singleItems = singles.allItems(&error);
    QCOMPARE(singleItems.size(), 1);
    QCOMPARE(singleItems.constFirst().id, 11);
    QCOMPARE(singleItems.constFirst().contentHash, QByteArray(32, '\0'));
    QCOMPARE(singleItems.constFirst().currentPath, QStringLiteral("single.mp3"));
    QCOMPARE(singleItems.constFirst().targetLanguageId, QStringLiteral("en"));
    QCOMPARE(singleItems.constFirst().fileState, mnce::FileState::Available);
    QVERIFY(!singleItems.constFirst().durationMs.has_value());
    mnce::SeriesRepository series(catalog);
    QVERIFY2(series.open(path, &error), qPrintable(error));
    const auto seriesItems = series.mediaForSeries(21, &error);
    QCOMPARE(seriesItems.size(), 1);
    QCOMPARE(seriesItems.constFirst().id, 31);
    QCOMPARE(seriesItems.constFirst().seriesId, 21);
    QCOMPARE(seriesItems.constFirst().currentPath, QStringLiteral("series.mp3"));
    QCOMPARE(seriesItems.constFirst().fileState, mnce::FileState::Available);
    QVERIFY(!seriesItems.constFirst().durationMs.has_value());
}

void PlaybackPersistenceTest::failedVersionThreeMigrationRollsBack()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("broken-v2.sqlite3"));
    QVERIFY(createVersionTwoDatabase(path, true));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(!repository.open(path, &error));
    QCOMPARE(userVersion(path), 2);
    QVERIFY(!hasColumn(path, QStringLiteral("media_items"), QStringLiteral("duration_ms")));
    QVERIFY(hasColumn(path, QStringLiteral("series_media_items"), QStringLiteral("duration_ms")));
}

void PlaybackPersistenceTest::persistsAndValidatesDurations()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("durations.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository singles(catalog);
    QString error;
    QVERIFY(singles.open(path, &error));
    mnce::MediaItem item;
    item.contentHash = QByteArray(32, 's');
    item.fileSize = 1;
    item.currentPath = directory.filePath(QStringLiteral("single.mp3"));
    item.displayName = QStringLiteral("single.mp3");
    item.targetLanguageId = QStringLiteral("en");
    const auto inserted = singles.insert(item);
    QVERIFY(singles.updateDuration(inserted.mediaId, 12'345, &error));
    QCOMPARE(singles.allItems().constFirst().durationMs, std::optional<qint64>(12'345));
    QVERIFY(!singles.updateDuration(inserted.mediaId, -1, &error));
    QVERIFY(!singles.updateDuration(9999, 1, &error));
    QVERIFY(singles.updateDuration(inserted.mediaId, std::nullopt, &error));
    QVERIFY(!singles.allItems().constFirst().durationMs.has_value());

    mnce::SeriesRepository series(catalog);
    QVERIFY(series.open(path, &error));
    const auto created = series.create(QStringLiteral("Lessons"), QStringLiteral("en"),
                                       directory.path(),
                                       {{{QByteArray(32, 'x')}, 1,
                                         directory.filePath(QStringLiteral("series.mp3")),
                                         QStringLiteral("series.mp3")}});
    QVERIFY(created.succeeded());
    const qint64 mediaId = series.mediaForSeries(created.seriesId).constFirst().id;
    QVERIFY(series.updateMediaDuration(mediaId, 67'890, &error));
    QCOMPARE(series.mediaForSeries(created.seriesId).constFirst().durationMs,
             std::optional<qint64>(67'890));
    QVERIFY(!series.updateMediaDuration(mediaId, -1, &error));
    QVERIFY(!series.updateMediaDuration(9999, 1, &error));
    QVERIFY(series.updateMediaDuration(mediaId, std::nullopt, &error));
    QVERIFY(!series.mediaForSeries(created.seriesId).constFirst().durationMs.has_value());
}

QTEST_GUILESS_MAIN(PlaybackPersistenceTest)

#include "playback-persistence-test.moc"
