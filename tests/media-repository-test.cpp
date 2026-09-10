#include "persistence/media-repository.h"
#include "services/file-hash-service.h"
#include "services/media-import-service.h"
#include "services/media-refresh-service.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class MediaRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsSchemaAndPreservesDataOnReopen();
    void rejectsFutureSchema();
    void insertsReadsAndScopesDuplicates();
    void rejectsUnknownLanguageAndHashSizeConflict();
    void updatesRelocatesDeletesAndReimports();
    void refreshMarksMissingAndCreatesReplacement();
    void relocationRequiresMatchingSizeAndHash();
    void catalogSupportsAdditionalLanguage();
    void importKeepsTheLanguageBoundAtTaskCreation();
};

static mnce::MediaItem makeItem(const QString& path,
                                const QString& language,
                                const QByteArray& hash,
                                qint64 size)
{
    mnce::MediaItem item;
    item.contentHash = hash;
    item.fileSize = size;
    item.currentPath = path;
    item.displayName = QFileInfo(path).fileName();
    item.targetLanguageId = language;
    return item;
}

static QString writeBytes(QTemporaryDir& directory, const QString& name, const QByteArray& bytes)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    return path;
}

static int readUserVersion(const QString& path)
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

void MediaRepositoryTest::createsSchemaAndPreservesDataOnReopen()
{
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    qint64 insertedId = 0;
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY2(repository.open(databasePath, &error), qPrintable(error));
        QCOMPARE(readUserVersion(databasePath), 1);
        const auto result = repository.insert(makeItem(directory.filePath(QStringLiteral("a.mp3")),
                                                       QStringLiteral("en"), QByteArray(32, 'a'), 10));
        QCOMPARE(result.status, mnce::InsertMediaStatus::Inserted);
        insertedId = result.mediaId;
    }
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY2(repository.open(databasePath, &error), qPrintable(error));
        const auto items = repository.itemsForLanguage(QStringLiteral("en"), &error);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.constFirst().id, insertedId);
        QCOMPARE(items.constFirst().transcriptionState, mnce::TranscriptionState::NotStarted);
    }
}

void MediaRepositoryTest::rejectsFutureSchema()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("future.sqlite3"));
    const QString connection = QUuid::createUuid().toString();
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 2")));
    }
    QSqlDatabase::removeDatabase(connection);

    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(!repository.open(path, &error));
    QVERIFY(error.contains(QStringLiteral("高于")));
}

void MediaRepositoryTest::insertsReadsAndScopesDuplicates()
{
    QTemporaryDir directory;
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("db.sqlite3")), &error));
    const QByteArray hash(32, 'x');
    QCOMPARE(repository.insert(makeItem(directory.filePath(QStringLiteral("one.mp3")),
                                             QStringLiteral("en"), hash, 8)).status,
             mnce::InsertMediaStatus::Inserted);
    const auto duplicate = repository.insert(makeItem(directory.filePath(QStringLiteral("two.mp3")),
                                                      QStringLiteral("en"), hash, 8));
    QCOMPARE(duplicate.status, mnce::InsertMediaStatus::Duplicate);
    QCOMPARE(repository.insert(makeItem(directory.filePath(QStringLiteral("two.mp3")),
                                             QStringLiteral("ja"), hash, 8)).status,
             mnce::InsertMediaStatus::Inserted);
    QCOMPARE(repository.itemsForLanguage(QStringLiteral("en")).size(), 1);
    QCOMPARE(repository.itemsForLanguage(QStringLiteral("ja")).size(), 1);
    QCOMPARE(repository.allItems().size(), 2);
    QVERIFY(repository.itemsForLanguage(QStringLiteral("en")).constFirst().currentPath
            .endsWith(QStringLiteral("one.mp3")));
}

void MediaRepositoryTest::rejectsUnknownLanguageAndHashSizeConflict()
{
    QTemporaryDir directory;
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("db.sqlite3")), &error));
    const QByteArray hash(32, 'h');
    QCOMPARE(repository.insert(makeItem(QStringLiteral("a"), QStringLiteral("fr"), hash, 1)).status,
             mnce::InsertMediaStatus::Error);
    QCOMPARE(repository.insert(makeItem(QStringLiteral("a"), QStringLiteral("en"), hash, 1)).status,
             mnce::InsertMediaStatus::Inserted);
    QCOMPARE(repository.insert(makeItem(QStringLiteral("b"), QStringLiteral("en"), hash, 2)).status,
             mnce::InsertMediaStatus::HashSizeConflict);
}

void MediaRepositoryTest::updatesRelocatesDeletesAndReimports()
{
    QTemporaryDir directory;
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("db.sqlite3")), &error));
    const QString sourcePath = writeBytes(directory, QStringLiteral("old.mp3"), QByteArrayLiteral("abc"));
    const auto inserted = repository.insert(makeItem(sourcePath, QStringLiteral("en"),
                                                     QByteArray(32, 'z'), 3));
    QVERIFY(repository.setFileState(inserted.mediaId, mnce::FileState::Missing, &error));
    const QString relocatedPath = writeBytes(directory, QStringLiteral("new.mp3"), QByteArrayLiteral("abc"));
    QVERIFY(repository.relocate(inserted.mediaId, relocatedPath,
                                QStringLiteral("new.mp3"), &error));
    auto item = repository.itemsForLanguage(QStringLiteral("en")).constFirst();
    QCOMPARE(item.fileState, mnce::FileState::Available);
    QCOMPARE(item.displayName, QStringLiteral("new.mp3"));
    QVERIFY(repository.remove(inserted.mediaId, &error));
    QVERIFY(QFileInfo::exists(sourcePath));
    QVERIFY(QFileInfo::exists(relocatedPath));
    QVERIFY(repository.itemsForLanguage(QStringLiteral("en")).isEmpty());
    const auto reimported = repository.insert(makeItem(sourcePath, QStringLiteral("en"),
                                                       QByteArray(32, 'z'), 3));
    QCOMPARE(reimported.status, mnce::InsertMediaStatus::Inserted);
    QVERIFY(reimported.mediaId != inserted.mediaId);
}

void MediaRepositoryTest::refreshMarksMissingAndCreatesReplacement()
{
    QTemporaryDir directory;
    const QString mediaPath = writeBytes(directory, QStringLiteral("same-name.mp3"), QByteArrayLiteral("old"));
    const auto oldHash = mnce::FileHashService{}.hashFile(mediaPath);
    QVERIFY(oldHash.succeeded());
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("db.sqlite3")), &error));
    const auto inserted = repository.insert(makeItem(mediaPath, QStringLiteral("en"), oldHash.hash,
                                                     oldHash.fileSize));
    QCOMPARE(inserted.status, mnce::InsertMediaStatus::Inserted);

    QVERIFY(!writeBytes(directory, QStringLiteral("same-name.mp3"), QByteArrayLiteral("new-content")).isEmpty());
    mnce::MediaRefreshService service;
    const auto inspections = service.inspect(repository.allItems());
    QVERIFY2(service.apply(repository, inspections, &error), qPrintable(error));
    const auto items = repository.itemsForLanguage(QStringLiteral("en"));
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).fileState, mnce::FileState::Missing);
    QCOMPARE(items.at(0).currentPath, items.at(1).currentPath);
    QCOMPARE(items.at(0).displayName, items.at(1).displayName);

    QVERIFY(QFile::remove(mediaPath));
    const auto missingInspection = service.inspect(repository.allItems());
    QVERIFY(service.apply(repository, missingInspection, &error));
    const auto missingItems = repository.itemsForLanguage(QStringLiteral("en"));
    QCOMPARE(missingItems.at(1).fileState, mnce::FileState::Missing);
}

void MediaRepositoryTest::relocationRequiresMatchingSizeAndHash()
{
    const auto original = makeItem(QStringLiteral("original.mp3"), QStringLiteral("en"),
                                   QByteArray(32, 'a'), 10);
    mnce::FileHashResult same{QByteArray(32, 'a'), 10, mnce::FileHashError::None, {}};
    QVERIFY(mnce::MediaRefreshService::matchesOriginal(original, same));
    same.fileSize = 11;
    QVERIFY(!mnce::MediaRefreshService::matchesOriginal(original, same));
    same.fileSize = 10;
    same.hash = QByteArray(32, 'b');
    QVERIFY(!mnce::MediaRefreshService::matchesOriginal(original, same));
}

void MediaRepositoryTest::catalogSupportsAdditionalLanguage()
{
    const mnce::TargetLanguageCatalog catalog({
        {QStringLiteral("en"), QStringLiteral("英语"), true},
        {QStringLiteral("ja"), QStringLiteral("日语"), false},
        {QStringLiteral("de"), QStringLiteral("德语"), false},
    });
    QCOMPARE(catalog.defaultLanguageId(), QStringLiteral("en"));
    QVERIFY(catalog.contains(QStringLiteral("de")));
    QTemporaryDir directory;
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("db.sqlite3")), &error));
    QCOMPARE(repository.insert(makeItem(QStringLiteral("a.mp3"), QStringLiteral("de"),
                                             QByteArray(32, 'd'), 4)).status,
             mnce::InsertMediaStatus::Inserted);
    QCOMPARE(repository.itemsForLanguage(QStringLiteral("de")).size(), 1);
}

void MediaRepositoryTest::importKeepsTheLanguageBoundAtTaskCreation()
{
    QTemporaryDir directory;
    const QString path = writeBytes(directory, QStringLiteral("lesson.mp3"), QByteArrayLiteral("audio"));
    const mnce::MediaImportRequest request{path, QStringLiteral("en")};
    QString currentUiLanguage = QStringLiteral("ja");
    const auto prepared = mnce::MediaImportService{}.prepare(request);
    QVERIFY(prepared.succeeded());
    QCOMPARE(prepared.media.targetLanguageId, QStringLiteral("en"));
    QVERIFY(prepared.media.targetLanguageId != currentUiLanguage);

    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    mnce::MediaRepository repository(catalog);
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("db.sqlite3")), &error));
    QCOMPARE(repository.insert(prepared.media).status, mnce::InsertMediaStatus::Inserted);
    QCOMPARE(repository.itemsForLanguage(QStringLiteral("en")).size(), 1);
    QVERIFY(repository.itemsForLanguage(currentUiLanguage).isEmpty());
}

QTEST_MAIN(MediaRepositoryTest)

#include "media-repository-test.moc"
