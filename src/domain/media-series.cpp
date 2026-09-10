#include "domain/media-series.h"

namespace mnce {

QString toDatabaseValue(DirectoryState state)
{
    return state == DirectoryState::Available ? QStringLiteral("available")
                                               : QStringLiteral("missing");
}

bool directoryStateFromDatabase(const QString& value, DirectoryState* state)
{
    if (value == QStringLiteral("available")) {
        *state = DirectoryState::Available;
        return true;
    }
    if (value == QStringLiteral("missing")) {
        *state = DirectoryState::Missing;
        return true;
    }
    return false;
}

} // namespace mnce
