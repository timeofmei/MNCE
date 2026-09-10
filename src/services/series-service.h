#pragma once

#include "persistence/series-repository.h"
#include "services/series-scan-service.h"

#include <QString>

#include <atomic>

namespace mnce {

enum class SeriesOperationStatus {
    Succeeded,
    DirectoryMarkedMissing,
    ScanFailed,
    PersistenceFailed,
};

enum class SeriesOperationKind { Create, Refresh, Relocate };

struct SeriesOperationResult
{
    SeriesOperationStatus status = SeriesOperationStatus::PersistenceFailed;
    qint64 seriesId = 0;
    QString failedPath;
    QString error;

    [[nodiscard]] bool succeeded() const
    {
        return status == SeriesOperationStatus::Succeeded
            || status == SeriesOperationStatus::DirectoryMarkedMissing;
    }
};

struct CreateSeriesRequest
{
    QString name;
    QString targetLanguageId;
    QString directory;
};

// Produced off the UI thread, then applied on the database connection's thread.
struct PreparedSeriesOperation
{
    SeriesOperationKind kind = SeriesOperationKind::Create;
    qint64 seriesId = 0;
    QString name;
    QString targetLanguageId;
    QString directory;
    QString expectedCurrentDirectory;
    SeriesScanResult scan;

    [[nodiscard]] bool readyToApply() const
    {
        return scan.succeeded()
            || (kind == SeriesOperationKind::Refresh
                && scan.error == SeriesScanError::DirectoryUnavailable);
    }
};

class SeriesService
{
public:
    explicit SeriesService(SeriesScanService scanService = SeriesScanService{});

    // prepare* performs filesystem and hashing work only. The caller schedules it
    // away from the UI thread.
    [[nodiscard]] PreparedSeriesOperation prepareCreate(
        const CreateSeriesRequest& request,
        const std::atomic_bool* stopRequested = nullptr) const;
    [[nodiscard]] PreparedSeriesOperation prepareRefresh(
        const MediaSeries& capturedSeries,
        const std::atomic_bool* stopRequested = nullptr) const;
    [[nodiscard]] PreparedSeriesOperation prepareRelocation(
        const MediaSeries& capturedSeries,
        const QString& newDirectory,
        const std::atomic_bool* stopRequested = nullptr) const;

    // apply performs database work only on the repository connection's thread.
    [[nodiscard]] SeriesOperationResult apply(
        SeriesRepository& repository,
        const PreparedSeriesOperation& operation) const;
    [[nodiscard]] SeriesOperationResult remove(SeriesRepository& repository,
                                               qint64 seriesId) const;

private:
    [[nodiscard]] static SeriesOperationResult persistenceResult(
        const SeriesWriteResult& result);

    SeriesScanService scanService_;
};

} // namespace mnce
