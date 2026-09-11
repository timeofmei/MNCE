#pragma once

#include "domain/media-series.h"
#include "domain/target-language.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace mnce {

enum class SeriesWriteStatus {
    Succeeded,
    InvalidName,
    DuplicateName,
    UnknownLanguage,
    NotFound,
    HashSizeConflict,
    Error,
};

struct SeriesWriteResult
{
    SeriesWriteStatus status = SeriesWriteStatus::Error;
    qint64 seriesId = 0;
    QString error;

    [[nodiscard]] bool succeeded() const { return status == SeriesWriteStatus::Succeeded; }
};

class SeriesRepository
{
public:
    explicit SeriesRepository(const TargetLanguageCatalog& catalog);
    ~SeriesRepository();

    SeriesRepository(const SeriesRepository&) = delete;
    SeriesRepository& operator=(const SeriesRepository&) = delete;

    [[nodiscard]] bool open(const QString& databasePath, QString* error);
    void close();
    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] SeriesWriteResult create(const QString& name,
                                           const QString& targetLanguageId,
                                           const QString& absoluteDirectory,
                                           const QVector<SeriesFileSnapshot>& files);
    [[nodiscard]] QVector<MediaSeries> seriesForLanguage(const QString& languageId,
                                                         QString* error = nullptr) const;
    [[nodiscard]] bool find(qint64 seriesId, MediaSeries* series,
                            QString* error = nullptr) const;
    [[nodiscard]] QVector<SeriesMediaItem> mediaForSeries(qint64 seriesId,
                                                          QString* error = nullptr) const;
    [[nodiscard]] bool updateMediaDuration(qint64 mediaId,
                                           std::optional<qint64> durationMs,
                                           QString* error = nullptr);
    [[nodiscard]] SeriesWriteResult applyRefresh(qint64 seriesId,
                                                 const QVector<SeriesFileSnapshot>& files);
    [[nodiscard]] SeriesWriteResult relocate(qint64 seriesId,
                                             const QString& absoluteDirectory,
                                             const QVector<SeriesFileSnapshot>& files);
    [[nodiscard]] SeriesWriteResult markDirectoryMissing(qint64 seriesId);
    [[nodiscard]] SeriesWriteResult remove(qint64 seriesId);

private:
    [[nodiscard]] SeriesWriteResult applyFilesInTransaction(
        qint64 seriesId, const QVector<SeriesFileSnapshot>& files);
    [[nodiscard]] bool begin(QString* error);
    [[nodiscard]] bool commit(QString* error);
    void rollback();

    const TargetLanguageCatalog& catalog_;
    QString connectionName_;
    QSqlDatabase database_;
};

} // namespace mnce
