#pragma once

#include "domain/media-item.h"
#include "services/file-hash-service.h"

#include <QString>
#include <QVector>

#include <atomic>

namespace mnce {

class MediaRepository;

struct MediaRefreshInspection
{
    MediaItem original;
    FileHashResult currentFile;
};

class MediaRefreshService
{
public:
    explicit MediaRefreshService(FileHashService hashService = FileHashService{});

    [[nodiscard]] QVector<MediaRefreshInspection> inspect(
        const QVector<MediaItem>& items,
        const std::atomic_bool* stopRequested = nullptr) const;
    [[nodiscard]] bool apply(MediaRepository& repository,
                             const QVector<MediaRefreshInspection>& inspections,
                             QString* error) const;
    [[nodiscard]] static bool matchesOriginal(const MediaItem& original,
                                              const FileHashResult& candidate);

private:
    FileHashService hashService_;
};

} // namespace mnce
