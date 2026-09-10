#pragma once

#include "domain/media-series.h"
#include "services/file-hash-service.h"

#include <QString>
#include <QVector>

#include <atomic>

namespace mnce {

enum class SeriesScanError {
    None,
    DirectoryUnavailable,
    FileFailed,
    HashSizeConflict,
    Cancelled,
};

struct SeriesScanResult
{
    QVector<SeriesFileSnapshot> files;
    SeriesScanError error = SeriesScanError::None;
    QString failedPath;
    QString message;

    [[nodiscard]] bool succeeded() const { return error == SeriesScanError::None; }
};

class SeriesScanService
{
public:
    explicit SeriesScanService(FileHashService hashService = FileHashService{});

    [[nodiscard]] SeriesScanResult scan(
        const QString& directory,
        const std::atomic_bool* stopRequested = nullptr) const;
    [[nodiscard]] static bool isSupportedAudioFile(const QString& fileName);
    static void sortNaturally(QVector<SeriesFileSnapshot>& files,
                              SortDirection direction = SortDirection::Ascending);

private:
    FileHashService hashService_;
};

} // namespace mnce
