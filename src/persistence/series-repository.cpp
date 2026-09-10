#include "persistence/series-repository.h"

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

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool isUniqueConstraint(const QSqlError& error)
{
    return error.nativeErrorCode() == QStringLiteral("2067")
        || error.text().contains(QStringLiteral("UNIQUE constraint failed"));
}

MediaSeries seriesFromQuery(const QSqlQuery& query, bool* valid)
{
    MediaSeries series;
    series.id = query.value(0).toLongLong();
    series.name = query.value(1).toString();
    series.normalizedName = query.value(2).toString();
    series.targetLanguageId = query.value(3).toString();
    series.currentDirectory = query.value(4).toString();
    *valid = directoryStateFromDatabase(query.value(5).toString(), &series.directoryState);
    series.createdAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
    series.updatedAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs);
    return series;
}

SeriesMediaItem mediaFromQuery(const QSqlQuery& query, bool* valid)
{
    SeriesMediaItem item;
    item.id = query.value(0).toLongLong();
    item.seriesId = query.value(1).toLongLong();
    item.contentHash = query.value(2).toByteArray();
    item.fileSize = query.value(3).toLongLong();
    item.currentPath = query.value(4).toString();
    item.displayName = query.value(5).toString();
    *valid = fileStateFromDatabase(query.value(6).toString(), &item.fileState)
        && transcriptionStateFromDatabase(query.value(7).toString(),
                                          &item.transcriptionState);
    item.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs);
    item.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
    return item;
}

} // namespace

SeriesRepository::SeriesRepository(const TargetLanguageCatalog& catalog)
    : catalog_(catalog)
    , connectionName_(QStringLiteral("mnce-series-%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

SeriesRepository::~SeriesRepository()
{
    close();
}

bool SeriesRepository::open(const QString& databasePath, QString* error)
{
    close();
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
    if (!initializeDatabaseSchema(database_, error)) {
        close();
        return false;
    }
    return true;
}

void SeriesRepository::close()
{
    if (database_.isValid()) {
        database_.close();
        database_ = {};
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool SeriesRepository::isOpen() const
{
    return database_.isOpen();
}

bool SeriesRepository::begin(QString* error)
{
    if (database_.transaction()) {
        return true;
    }
    if (error != nullptr) {
        *error = database_.lastError().text();
    }
    return false;
}

bool SeriesRepository::commit(QString* error)
{
    if (database_.commit()) {
        return true;
    }
    if (error != nullptr) {
        *error = database_.lastError().text();
    }
    return false;
}

void SeriesRepository::rollback()
{
    database_.rollback();
}

SeriesWriteResult SeriesRepository::create(const QString& name,
                                           const QString& targetLanguageId,
                                           const QString& absoluteDirectory,
                                           const QVector<SeriesFileSnapshot>& files)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        return {SeriesWriteStatus::InvalidName, 0, QStringLiteral("系列名称不能为空")};
    }
    if (!catalog_.contains(targetLanguageId)) {
        return {SeriesWriteStatus::UnknownLanguage, 0, QStringLiteral("目标语言 ID 不在目录中")};
    }
    QString error;
    if (!begin(&error)) {
        return {SeriesWriteStatus::Error, 0, error};
    }
    const QString now = utcNow();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO media_series (name, normalized_name, target_language_id, current_directory, "
        "directory_state, created_at, updated_at) VALUES (?, ?, ?, ?, 'available', ?, ?)"));
    query.addBindValue(trimmedName);
    query.addBindValue(trimmedName.toCaseFolded());
    query.addBindValue(targetLanguageId);
    query.addBindValue(normalizedPath(absoluteDirectory));
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        const bool duplicate = isUniqueConstraint(query.lastError());
        error = queryError(QStringLiteral("创建系列失败"), query);
        rollback();
        return {duplicate ? SeriesWriteStatus::DuplicateName : SeriesWriteStatus::Error, 0, error};
    }
    const qint64 seriesId = query.lastInsertId().toLongLong();
    auto applied = applyFilesInTransaction(seriesId, files);
    if (!applied.succeeded()) {
        rollback();
        return applied;
    }
    if (!commit(&error)) {
        rollback();
        return {SeriesWriteStatus::Error, 0, error};
    }
    return {SeriesWriteStatus::Succeeded, seriesId, {}};
}

QVector<MediaSeries> SeriesRepository::seriesForLanguage(const QString& languageId,
                                                         QString* error) const
{
    QVector<MediaSeries> result;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, name, normalized_name, target_language_id, current_directory, "
        "directory_state, created_at, updated_at FROM media_series "
        "WHERE target_language_id = ? ORDER BY id"));
    query.addBindValue(languageId);
    if (!query.exec()) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("读取系列失败"), query);
        }
        return result;
    }
    while (query.next()) {
        bool valid = false;
        auto series = seriesFromQuery(query, &valid);
        if (!valid) {
            if (error != nullptr) {
                *error = QStringLiteral("数据库包含无法识别的文件夹状态");
            }
            return {};
        }
        result.push_back(std::move(series));
    }
    return result;
}

bool SeriesRepository::find(qint64 seriesId, MediaSeries* series, QString* error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, name, normalized_name, target_language_id, current_directory, "
        "directory_state, created_at, updated_at FROM media_series WHERE id = ?"));
    query.addBindValue(seriesId);
    if (!query.exec()) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("读取系列失败"), query);
        }
        return false;
    }
    if (!query.next()) {
        if (error != nullptr) {
            *error = QStringLiteral("系列不存在");
        }
        return false;
    }
    bool valid = false;
    *series = seriesFromQuery(query, &valid);
    if (!valid && error != nullptr) {
        *error = QStringLiteral("数据库包含无法识别的文件夹状态");
    }
    return valid;
}

QVector<SeriesMediaItem> SeriesRepository::mediaForSeries(qint64 seriesId,
                                                          QString* error) const
{
    QVector<SeriesMediaItem> result;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, series_id, content_hash, file_size, current_path, display_name, "
        "file_state, transcription_state, created_at, updated_at "
        "FROM series_media_items WHERE series_id = ? ORDER BY id"));
    query.addBindValue(seriesId);
    if (!query.exec()) {
        if (error != nullptr) {
            *error = queryError(QStringLiteral("读取系列媒体失败"), query);
        }
        return result;
    }
    while (query.next()) {
        bool valid = false;
        auto item = mediaFromQuery(query, &valid);
        if (!valid) {
            if (error != nullptr) {
                *error = QStringLiteral("数据库包含无法识别的媒体状态");
            }
            return {};
        }
        result.push_back(std::move(item));
    }
    return result;
}

SeriesWriteResult SeriesRepository::applyFilesInTransaction(
    qint64 seriesId, const QVector<SeriesFileSnapshot>& files)
{
    const QString now = utcNow();
    QSqlQuery missing(database_);
    missing.prepare(QStringLiteral(
        "UPDATE series_media_items SET file_state = 'missing', updated_at = ? WHERE series_id = ?"));
    missing.addBindValue(now);
    missing.addBindValue(seriesId);
    if (!missing.exec()) {
        return {SeriesWriteStatus::Error, seriesId,
                queryError(QStringLiteral("更新缺失状态失败"), missing)};
    }

    for (const auto& file : files) {
        if (file.contentHash.size() != 32 || file.fileSize < 0) {
            return {SeriesWriteStatus::Error, seriesId, QStringLiteral("扫描结果包含无效的文件身份")};
        }
        QSqlQuery existing(database_);
        existing.prepare(QStringLiteral(
            "SELECT id, file_size FROM series_media_items WHERE series_id = ? AND content_hash = ?"));
        existing.addBindValue(seriesId);
        existing.addBindValue(file.contentHash);
        if (!existing.exec()) {
            return {SeriesWriteStatus::Error, seriesId,
                    queryError(QStringLiteral("读取系列媒体失败"), existing)};
        }
        if (existing.next()) {
            if (existing.value(1).toLongLong() != file.fileSize) {
                return {SeriesWriteStatus::HashSizeConflict, seriesId,
                        QStringLiteral("相同内容哈希对应了不同文件大小，无法自动合并")};
            }
            QSqlQuery update(database_);
            update.prepare(QStringLiteral(
                "UPDATE series_media_items SET current_path = ?, display_name = ?, "
                "file_state = 'available', updated_at = ? WHERE id = ?"));
            update.addBindValue(normalizedPath(file.absolutePath));
            update.addBindValue(file.displayName);
            update.addBindValue(now);
            update.addBindValue(existing.value(0));
            if (!update.exec()) {
                return {SeriesWriteStatus::Error, seriesId,
                        queryError(QStringLiteral("更新系列媒体失败"), update)};
            }
            continue;
        }

        QSqlQuery insert(database_);
        insert.prepare(QStringLiteral(
            "INSERT INTO series_media_items (series_id, content_hash, file_size, current_path, "
            "display_name, file_state, transcription_state, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, 'available', 'not_started', ?, ?)"));
        insert.addBindValue(seriesId);
        insert.addBindValue(file.contentHash);
        insert.addBindValue(file.fileSize);
        insert.addBindValue(normalizedPath(file.absolutePath));
        insert.addBindValue(file.displayName);
        insert.addBindValue(now);
        insert.addBindValue(now);
        if (!insert.exec()) {
            return {SeriesWriteStatus::Error, seriesId,
                    queryError(QStringLiteral("保存系列媒体失败"), insert)};
        }
    }
    return {SeriesWriteStatus::Succeeded, seriesId, {}};
}

SeriesWriteResult SeriesRepository::applyRefresh(
    qint64 seriesId, const QVector<SeriesFileSnapshot>& files)
{
    QString error;
    MediaSeries ignored;
    if (!find(seriesId, &ignored, &error)) {
        return {error == QStringLiteral("系列不存在") ? SeriesWriteStatus::NotFound
                                                        : SeriesWriteStatus::Error,
                seriesId, error};
    }
    if (!begin(&error)) {
        return {SeriesWriteStatus::Error, seriesId, error};
    }
    auto result = applyFilesInTransaction(seriesId, files);
    if (result.succeeded()) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "UPDATE media_series SET directory_state = 'available', updated_at = ? WHERE id = ?"));
        query.addBindValue(utcNow());
        query.addBindValue(seriesId);
        if (!query.exec()) {
            result = {SeriesWriteStatus::Error, seriesId,
                      queryError(QStringLiteral("更新系列状态失败"), query)};
        }
    }
    if (!result.succeeded() || !commit(&error)) {
        if (result.succeeded()) {
            result = {SeriesWriteStatus::Error, seriesId, error};
        }
        rollback();
    }
    return result;
}

SeriesWriteResult SeriesRepository::relocate(
    qint64 seriesId, const QString& absoluteDirectory,
    const QVector<SeriesFileSnapshot>& files)
{
    QString error;
    MediaSeries ignored;
    if (!find(seriesId, &ignored, &error)) {
        return {error == QStringLiteral("系列不存在") ? SeriesWriteStatus::NotFound
                                                        : SeriesWriteStatus::Error,
                seriesId, error};
    }
    if (!begin(&error)) {
        return {SeriesWriteStatus::Error, seriesId, error};
    }
    auto result = applyFilesInTransaction(seriesId, files);
    if (result.succeeded()) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "UPDATE media_series SET current_directory = ?, directory_state = 'available', "
            "updated_at = ? WHERE id = ?"));
        query.addBindValue(normalizedPath(absoluteDirectory));
        query.addBindValue(utcNow());
        query.addBindValue(seriesId);
        if (!query.exec() || query.numRowsAffected() != 1) {
            result = {SeriesWriteStatus::Error, seriesId,
                      queryError(QStringLiteral("重新定位系列失败"), query)};
        }
    }
    if (!result.succeeded() || !commit(&error)) {
        if (result.succeeded()) {
            result = {SeriesWriteStatus::Error, seriesId, error};
        }
        rollback();
    }
    return result;
}

SeriesWriteResult SeriesRepository::markDirectoryMissing(qint64 seriesId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE media_series SET directory_state = 'missing', updated_at = ? WHERE id = ?"));
    query.addBindValue(utcNow());
    query.addBindValue(seriesId);
    if (!query.exec()) {
        return {SeriesWriteStatus::Error, seriesId,
                queryError(QStringLiteral("更新系列状态失败"), query)};
    }
    if (query.numRowsAffected() != 1) {
        return {SeriesWriteStatus::NotFound, seriesId, QStringLiteral("系列不存在")};
    }
    return {SeriesWriteStatus::Succeeded, seriesId, {}};
}

SeriesWriteResult SeriesRepository::remove(qint64 seriesId)
{
    QString error;
    if (!begin(&error)) {
        return {SeriesWriteStatus::Error, seriesId, error};
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM media_series WHERE id = ?"));
    query.addBindValue(seriesId);
    if (!query.exec()) {
        error = queryError(QStringLiteral("删除系列失败"), query);
        rollback();
        return {SeriesWriteStatus::Error, seriesId, error};
    }
    if (query.numRowsAffected() != 1) {
        rollback();
        return {SeriesWriteStatus::NotFound, seriesId, QStringLiteral("系列不存在")};
    }
    if (!commit(&error)) {
        rollback();
        return {SeriesWriteStatus::Error, seriesId, error};
    }
    return {SeriesWriteStatus::Succeeded, seriesId, {}};
}

} // namespace mnce
