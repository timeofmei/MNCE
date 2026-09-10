#include "persistence/database-schema.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace mnce {
namespace {

QString queryError(const QString& operation, const QSqlQuery& query)
{
    return QStringLiteral("%1：%2").arg(operation, query.lastError().text());
}

bool execute(QSqlQuery& query, const QString& sql, QString* error)
{
    if (query.exec(sql)) {
        return true;
    }
    if (error != nullptr) {
        *error = queryError(QStringLiteral("无法升级数据库"), query);
    }
    return false;
}

bool createMediaItems(QSqlQuery& query, QString* error)
{
    return execute(query, QStringLiteral(
        "CREATE TABLE media_items ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "content_hash BLOB NOT NULL,"
        "file_size INTEGER NOT NULL CHECK (file_size >= 0),"
        "current_path TEXT NOT NULL,"
        "display_name TEXT NOT NULL,"
        "target_language_id TEXT NOT NULL,"
        "file_state TEXT NOT NULL CHECK (file_state IN ('available', 'missing')),"
        "transcription_state TEXT NOT NULL CHECK (transcription_state IN ('not_started')),"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL,"
        "UNIQUE (target_language_id, content_hash)"
        ")"), error);
}

bool createSeriesSchema(QSqlQuery& query, QString* error)
{
    return execute(query, QStringLiteral(
               "CREATE TABLE media_series ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "name TEXT NOT NULL,"
               "normalized_name TEXT NOT NULL,"
               "target_language_id TEXT NOT NULL,"
               "current_directory TEXT NOT NULL,"
               "directory_state TEXT NOT NULL CHECK (directory_state IN ('available', 'missing')),"
               "created_at TEXT NOT NULL,"
               "updated_at TEXT NOT NULL,"
               "UNIQUE (target_language_id, normalized_name)"
               ")"), error)
        && execute(query, QStringLiteral(
               "CREATE TABLE series_media_items ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "series_id INTEGER NOT NULL REFERENCES media_series(id) ON DELETE CASCADE,"
               "content_hash BLOB NOT NULL CHECK (length(content_hash) = 32),"
               "file_size INTEGER NOT NULL CHECK (file_size >= 0),"
               "current_path TEXT NOT NULL,"
               "display_name TEXT NOT NULL,"
               "file_state TEXT NOT NULL CHECK (file_state IN ('available', 'missing')),"
               "transcription_state TEXT NOT NULL CHECK (transcription_state IN ('not_started')),"
               "created_at TEXT NOT NULL,"
               "updated_at TEXT NOT NULL,"
               "UNIQUE (series_id, content_hash)"
               ")"), error)
        && execute(query, QStringLiteral(
               "CREATE INDEX idx_media_series_language_created "
               "ON media_series(target_language_id, created_at, id)"), error)
        && execute(query, QStringLiteral(
               "CREATE INDEX idx_series_media_series ON series_media_items(series_id)"), error);
}

} // namespace

bool initializeDatabaseSchema(QSqlDatabase& database, QString* error)
{
    QSqlQuery versionQuery(database);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("无法读取数据库版本"), versionQuery);
        }
        return false;
    }
    const int version = versionQuery.value(0).toInt();
    if (version > supportedSchemaVersion) {
        if (error != nullptr) {
            *error = QStringLiteral("数据库版本 %1 高于此应用支持的版本 %2")
                         .arg(version).arg(supportedSchemaVersion);
        }
        return false;
    }
    if (version == supportedSchemaVersion) {
        return true;
    }
    if (!database.transaction()) {
        if (error != nullptr) {
            *error = database.lastError().text();
        }
        return false;
    }

    QSqlQuery query(database);
    bool succeeded = true;
    if (version == 0) {
        succeeded = createMediaItems(query, error);
    }
    if (succeeded) {
        succeeded = createSeriesSchema(query, error);
    }
    if (succeeded) {
        succeeded = execute(query, QStringLiteral("PRAGMA user_version = 2"), error);
    }
    if (!succeeded || !database.commit()) {
        if (succeeded && error != nullptr) {
            *error = database.lastError().text();
        }
        database.rollback();
        return false;
    }
    return true;
}

} // namespace mnce
