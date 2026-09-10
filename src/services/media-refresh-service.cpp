#include "services/media-refresh-service.h"

#include "persistence/media-repository.h"

#include <QFileInfo>

#include <utility>

namespace mnce {

MediaRefreshService::MediaRefreshService(FileHashService hashService)
    : hashService_(std::move(hashService))
{
}

QVector<MediaRefreshInspection> MediaRefreshService::inspect(
    const QVector<MediaItem>& items,
    const std::atomic_bool* stopRequested) const
{
    QVector<MediaRefreshInspection> inspections;
    inspections.reserve(items.size());
    for (const auto& item : items) {
        if (stopRequested != nullptr && stopRequested->load()) {
            break;
        }
        inspections.push_back({item, hashService_.hashFile(item.currentPath, stopRequested)});
    }
    return inspections;
}

bool MediaRefreshService::apply(MediaRepository& repository,
                                const QVector<MediaRefreshInspection>& inspections,
                                QString* error) const
{
    for (const auto& inspection : inspections) {
        if (!inspection.currentFile.succeeded()) {
            if (inspection.currentFile.error == FileHashError::NotAReadableFile
                && !repository.setFileState(inspection.original.id, FileState::Missing, error)) {
                return false;
            }
            if (inspection.currentFile.error == FileHashError::ReadFailed
                || inspection.currentFile.error == FileHashError::FileChanged) {
                if (error != nullptr) {
                    *error = inspection.currentFile.message;
                }
                return false;
            }
            continue;
        }
        if (matchesOriginal(inspection.original, inspection.currentFile)) {
            if (!repository.setFileState(inspection.original.id, FileState::Available, error)) {
                return false;
            }
            continue;
        }
        if (!repository.setFileState(inspection.original.id, FileState::Missing, error)) {
            return false;
        }

        const QFileInfo info(inspection.original.currentPath);
        MediaItem replacement;
        replacement.contentHash = inspection.currentFile.hash;
        replacement.fileSize = inspection.currentFile.fileSize;
        replacement.currentPath = info.absoluteFilePath();
        replacement.displayName = info.fileName();
        replacement.targetLanguageId = inspection.original.targetLanguageId;
        const auto inserted = repository.insert(replacement);
        if (inserted.status == InsertMediaStatus::Error
            || inserted.status == InsertMediaStatus::HashSizeConflict) {
            if (error != nullptr) {
                *error = inserted.error;
            }
            return false;
        }
    }
    return true;
}

bool MediaRefreshService::matchesOriginal(const MediaItem& original,
                                          const FileHashResult& candidate)
{
    return candidate.succeeded() && candidate.fileSize == original.fileSize
        && candidate.hash == original.contentHash;
}

} // namespace mnce
