#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace mnce {

enum class FileState { Available, Missing };
enum class TranscriptionState { NotStarted };

struct MediaItem
{
    qint64 id = 0;
    QByteArray contentHash;
    qint64 fileSize = 0;
    QString currentPath;
    QString displayName;
    QString targetLanguageId;
    FileState fileState = FileState::Available;
    TranscriptionState transcriptionState = TranscriptionState::NotStarted;
    QDateTime createdAt;
    QDateTime updatedAt;
};

[[nodiscard]] QString toDatabaseValue(FileState state);
[[nodiscard]] QString toDatabaseValue(TranscriptionState state);
[[nodiscard]] bool fileStateFromDatabase(const QString& value, FileState* state);
[[nodiscard]] bool transcriptionStateFromDatabase(const QString& value,
                                                  TranscriptionState* state);

} // namespace mnce
