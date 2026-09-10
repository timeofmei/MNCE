#include "services/series-service.h"

#include <utility>

namespace mnce {

SeriesService::SeriesService(SeriesScanService scanService)
    : scanService_(std::move(scanService))
{
}

SeriesOperationResult SeriesService::persistenceResult(const SeriesWriteResult& result)
{
    return {result.succeeded() ? SeriesOperationStatus::Succeeded
                               : SeriesOperationStatus::PersistenceFailed,
            result.seriesId, {}, result.error};
}

PreparedSeriesOperation SeriesService::prepareCreate(
    const CreateSeriesRequest& request, const std::atomic_bool* stopRequested) const
{
    return {SeriesOperationKind::Create, 0, request.name, request.targetLanguageId,
            request.directory, {}, scanService_.scan(request.directory, stopRequested)};
}

PreparedSeriesOperation SeriesService::prepareRefresh(
    const MediaSeries& capturedSeries, const std::atomic_bool* stopRequested) const
{
    return {SeriesOperationKind::Refresh, capturedSeries.id, {},
            capturedSeries.targetLanguageId, capturedSeries.currentDirectory,
            capturedSeries.currentDirectory,
            scanService_.scan(capturedSeries.currentDirectory, stopRequested)};
}

PreparedSeriesOperation SeriesService::prepareRelocation(
    const MediaSeries& capturedSeries, const QString& newDirectory,
    const std::atomic_bool* stopRequested) const
{
    return {SeriesOperationKind::Relocate, capturedSeries.id, {},
            capturedSeries.targetLanguageId, newDirectory,
            capturedSeries.currentDirectory,
            scanService_.scan(newDirectory, stopRequested)};
}

SeriesOperationResult SeriesService::apply(
    SeriesRepository& repository, const PreparedSeriesOperation& operation) const
{
    if (!operation.readyToApply()) {
        return {SeriesOperationStatus::ScanFailed, operation.seriesId,
                operation.scan.failedPath, operation.scan.message};
    }
    if (operation.kind == SeriesOperationKind::Create) {
        return persistenceResult(repository.create(operation.name, operation.targetLanguageId,
                                                   operation.directory, operation.scan.files));
    }

    MediaSeries current;
    QString error;
    if (!repository.find(operation.seriesId, &current, &error)) {
        return {SeriesOperationStatus::PersistenceFailed, operation.seriesId, {}, error};
    }
    if (current.targetLanguageId != operation.targetLanguageId
        || current.currentDirectory != operation.expectedCurrentDirectory) {
        return {SeriesOperationStatus::PersistenceFailed, operation.seriesId, {},
                QStringLiteral("系列在扫描期间发生了变化，请重试")};
    }

    if (operation.kind == SeriesOperationKind::Refresh
        && operation.scan.error == SeriesScanError::DirectoryUnavailable) {
        const auto marked = repository.markDirectoryMissing(operation.seriesId);
        if (!marked.succeeded()) {
            return persistenceResult(marked);
        }
        return {SeriesOperationStatus::DirectoryMarkedMissing, operation.seriesId,
                operation.scan.failedPath, operation.scan.message};
    }
    if (operation.kind == SeriesOperationKind::Refresh) {
        return persistenceResult(repository.applyRefresh(operation.seriesId,
                                                         operation.scan.files));
    }
    return persistenceResult(repository.relocate(operation.seriesId, operation.directory,
                                                 operation.scan.files));
}

SeriesOperationResult SeriesService::remove(SeriesRepository& repository, qint64 seriesId) const
{
    return persistenceResult(repository.remove(seriesId));
}

} // namespace mnce
