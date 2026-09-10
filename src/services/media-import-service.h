#pragma once

#include "domain/media-item.h"
#include "services/file-hash-service.h"

#include <QString>

#include <atomic>

namespace mnce {

struct MediaImportRequest
{
    QString path;
    QString targetLanguageId;
};

struct MediaImportPreparation
{
    MediaItem media;
    FileHashResult hashResult;

    [[nodiscard]] bool succeeded() const { return hashResult.succeeded(); }
};

class MediaImportService
{
public:
    explicit MediaImportService(FileHashService hashService = FileHashService{});

    [[nodiscard]] MediaImportPreparation prepare(
        const MediaImportRequest& request,
        const std::atomic_bool* stopRequested = nullptr) const;

private:
    FileHashService hashService_;
};

} // namespace mnce
