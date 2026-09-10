#include "persistence/media-repository.h"

#include "persistence/database-schema.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace mnce {
namespace {

QString queryError(const QString& operation, const QSqlQuery& query)
{
    return QStringLiteral("%1：%2").arg(operation, query.lastError().text());
}

MediaItem itemFromQuery(const QSqlQuery& query, bool* valid)
{
    MediaItem item;
    item.id = query.value(0).toLongLong();
    item.contentHash = query.value(1).toByteArray();
    item.fileSize = query.value(2).toLongLong();
    item.currentPath = query.value(3).toString();
    item.displayName = query.value(4).toString();
    item.targetLanguageId = query.value(5).toString();
    *valid = fileStateFromDatabase(query.value(6).toString(), &item.fileState)
        && transcriptionStateFromDatabase(query.value(7).toString(),
                                          &item.transcriptionState);
    item.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs);
    item.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
    return item;
}

} // namespace

MediaRepository::MediaRepository(const TargetLanguageCatalog& catalog)
    : catalog_(catalog)
    , connectionName_(QStringLiteral("mnce-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

MediaRepository::~MediaRepository()
{
    close();
}

bool MediaRepository::open(const QString& databasePath, QString* error)
{
    close();
    databasePath_ = databasePath;
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath);
    if (!database_.open()) {
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        close();
        return false;
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))
        || !query.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("无法配置数据库"), query);
        }
        close();
        return false;
    }
    if (!initializeSchema(error)) {
        close();
        return false;
    }
    return true;
}

void MediaRepository::close()
{
    if (database_.isValid()) {
        database_.close();
        database_ = {};
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool MediaRepository::isOpen() const
{
    return database_.isOpen();
}

QString MediaRepository::databasePath() const
{
    return databasePath_;
}

bool MediaRepository::initializeSchema(QString* error)
{
    return initializeDatabaseSchema(database_, error);
}

InsertMediaResult MediaRepository::insert(const MediaItem& item)
{
    if (!catalog_.contains(item.targetLanguageId)) {
        return {InsertMediaStatus::Error, 0, QStringLiteral("目标语言 ID 不在目录中")};
    }
    QSqlQuery existing(database_);
    existing.prepare(QStringLiteral(
        "SELECT id, file_size FROM media_items "
        "WHERE target_language_id = ? AND content_hash = ?"));
    existing.addBindValue(item.targetLanguageId);
    existing.addBindValue(item.contentHash);
    if (!existing.exec()) {
        return {InsertMediaStatus::Error, 0, queryError(QStringLiteral("查询重复媒体失败"), existing)};
    }
    if (existing.next()) {
        const qint64 id = existing.value(0).toLongLong();
        if (existing.value(1).toLongLong() != item.fileSize) {
            return {InsertMediaStatus::HashSizeConflict, id,
                    QStringLiteral("相同内容哈希对应了不同文件大小，无法自动合并")};
        }
        return {InsertMediaStatus::Duplicate, id, {}};
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSqlQuery insertQuery(database_);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO media_items (content_hash, file_size, current_path, display_name, "
        "target_language_id, file_state, transcription_state, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insertQuery.addBindValue(item.contentHash);
    insertQuery.addBindValue(item.fileSize);
    insertQuery.addBindValue(QDir::cleanPath(QFileInfo(item.currentPath).absoluteFilePath()));
    insertQuery.addBindValue(item.displayName);
    insertQuery.addBindValue(item.targetLanguageId);
    insertQuery.addBindValue(toDatabaseValue(item.fileState));
    insertQuery.addBindValue(toDatabaseValue(item.transcriptionState));
    insertQuery.addBindValue(now.toString(Qt::ISODateWithMs));
    insertQuery.addBindValue(now.toString(Qt::ISODateWithMs));
    if (!insertQuery.exec()) {
        return {InsertMediaStatus::Error, 0,
                queryError(QStringLiteral("保存媒体记录失败"), insertQuery)};
    }
    return {InsertMediaStatus::Inserted, insertQuery.lastInsertId().toLongLong(), {}};
}

QVector<MediaItem> MediaRepository::itemsForLanguage(const QString& languageId,
                                                     QString* error) const
{
    return queryItems(QStringLiteral(
                          "SELECT id, content_hash, file_size, current_path, display_name, "
                          "target_language_id, file_state, transcription_state, created_at, updated_at "
                          "FROM media_items WHERE target_language_id = ? ORDER BY id"),
                      {languageId}, error);
}

QVector<MediaItem> MediaRepository::allItems(QString* error) const
{
    return queryItems(QStringLiteral(
                          "SELECT id, content_hash, file_size, current_path, display_name, "
                          "target_language_id, file_state, transcription_state, created_at, updated_at "
                          "FROM media_items ORDER BY id"),
                      {}, error);
}

QVector<MediaItem> MediaRepository::queryItems(const QString& sql,
                                               const QVariantList& values,
                                               QString* error) const
{
    QVector<MediaItem> items;
    QSqlQuery query(database_);
    query.prepare(sql);
    for (const auto& value : values) {
        query.addBindValue(value);
    }
    if (!query.exec()) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("读取媒体记录失败"), query);
        }
        return items;
    }
    while (query.next()) {
        bool valid = false;
        auto item = itemFromQuery(query, &valid);
        if (!valid) {
            if (error != nullptr) {
                *error = QStringLiteral("数据库包含无法识别的媒体状态") ;
            }
            return {};
        }
        items.push_back(std::move(item));
    }
    return items;
}

bool MediaRepository::setFileState(qint64 id, FileState state, QString* error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE media_items SET file_state = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(toDatabaseValue(state));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("更新文件状态失败"), query);
        }
        return false;
    }
    return true;
}

bool MediaRepository::relocate(qint64 id,
                               const QString& absolutePath,
                               const QString& displayName,
                               QString* error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE media_items SET current_path = ?, display_name = ?, file_state = 'available', "
        "updated_at = ? WHERE id = ?"));
    query.addBindValue(QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath()));
    query.addBindValue(displayName);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("重新定位媒体失败"), query);
        }
        return false;
    }
    return true;
}

bool MediaRepository::remove(qint64 id, QString* error)
{
    if (!database_.transaction()) {
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM media_items WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1 || !database_.commit()) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("删除媒体记录失败"), query);
        }
        database_.rollback();
        return false;
    }
    return true;
}

} // namespace mnce
