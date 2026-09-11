#pragma once

#include "domain/media-item.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace mnce {

enum class DirectoryState { Available, Missing };
enum class LibrarySortField { Name, CreatedAt };
enum class SortDirection { Ascending, Descending };

struct MediaSeries
{
    qint64 id = 0;
    QString name;
    QString normalizedName;
    QString targetLanguageId;
    QString currentDirectory;
    DirectoryState directoryState = DirectoryState::Available;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct SeriesMediaItem
{
    qint64 id = 0;
    qint64 seriesId = 0;
    QByteArray contentHash;
    qint64 fileSize = 0;
    QString currentPath;
    QString displayName;
    FileState fileState = FileState::Available;
    TranscriptionState transcriptionState = TranscriptionState::NotStarted;
    std::optional<qint64> durationMs;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct SeriesFileSnapshot
{
    QByteArray contentHash;
    qint64 fileSize = 0;
    QString absolutePath;
    QString displayName;
};

[[nodiscard]] QString toDatabaseValue(DirectoryState state);
[[nodiscard]] bool directoryStateFromDatabase(const QString& value, DirectoryState* state);

} // namespace mnce
