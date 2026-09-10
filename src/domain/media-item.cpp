#include "domain/media-item.h"

namespace mnce {

QString toDatabaseValue(FileState state)
{
    return state == FileState::Available ? QStringLiteral("available")
                                         : QStringLiteral("missing");
}

QString toDatabaseValue(TranscriptionState)
{
    return QStringLiteral("not_started");
}

bool fileStateFromDatabase(const QString& value, FileState* state)
{
    if (value == QStringLiteral("available")) {
        *state = FileState::Available;
        return true;
    }
    if (value == QStringLiteral("missing")) {
        *state = FileState::Missing;
        return true;
    }
    return false;
}

bool transcriptionStateFromDatabase(const QString& value, TranscriptionState* state)
{
    if (value != QStringLiteral("not_started")) {
        return false;
    }
    *state = TranscriptionState::NotStarted;
    return true;
}

} // namespace mnce
