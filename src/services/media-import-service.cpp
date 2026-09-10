#include "services/media-import-service.h"

#include <QFileInfo>

#include <utility>

namespace mnce {

MediaImportService::MediaImportService(FileHashService hashService)
    : hashService_(std::move(hashService))
{
}

MediaImportPreparation MediaImportService::prepare(
    const MediaImportRequest& request,
    const std::atomic_bool* stopRequested) const
{
    MediaImportPreparation preparation;
    preparation.hashResult = hashService_.hashFile(request.path, stopRequested);
    if (!preparation.hashResult.succeeded()) {
        return preparation;
    }
    const QFileInfo info(request.path);
    preparation.media.contentHash = preparation.hashResult.hash;
    preparation.media.fileSize = preparation.hashResult.fileSize;
    preparation.media.currentPath = info.absoluteFilePath();
    preparation.media.displayName = info.fileName();
    preparation.media.targetLanguageId = request.targetLanguageId;
    return preparation;
}

} // namespace mnce
