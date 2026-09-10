#pragma once

#include "domain/media-item.h"
#include "domain/target-language.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace mnce {

enum class InsertMediaStatus { Inserted, Duplicate, HashSizeConflict, Error };

struct InsertMediaResult
{
    InsertMediaStatus status = InsertMediaStatus::Error;
    qint64 mediaId = 0;
    QString error;
};

class MediaRepository
{
public:
    static constexpr int supportedSchemaVersion = 2;

    explicit MediaRepository(const TargetLanguageCatalog& catalog);
    ~MediaRepository();

    MediaRepository(const MediaRepository&) = delete;
    MediaRepository& operator=(const MediaRepository&) = delete;

    [[nodiscard]] bool open(const QString& databasePath, QString* error);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString databasePath() const;

    [[nodiscard]] InsertMediaResult insert(const MediaItem& item);
    [[nodiscard]] QVector<MediaItem> itemsForLanguage(const QString& languageId,
                                                     QString* error = nullptr) const;
    [[nodiscard]] QVector<MediaItem> allItems(QString* error = nullptr) const;
    [[nodiscard]] bool setFileState(qint64 id, FileState state, QString* error = nullptr);
    [[nodiscard]] bool relocate(qint64 id,
                                const QString& absolutePath,
                                const QString& displayName,
                                QString* error = nullptr);
    [[nodiscard]] bool remove(qint64 id, QString* error = nullptr);

private:
    [[nodiscard]] bool initializeSchema(QString* error);
    [[nodiscard]] QVector<MediaItem> queryItems(const QString& sql,
                                               const QVariantList& values,
                                               QString* error) const;

    const TargetLanguageCatalog& catalog_;
    QString connectionName_;
    QString databasePath_;
    QSqlDatabase database_;
};

} // namespace mnce
