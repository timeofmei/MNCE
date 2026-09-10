#pragma once

#include <QSqlDatabase>
#include <QString>

namespace mnce {

inline constexpr int supportedSchemaVersion = 2;

[[nodiscard]] bool initializeDatabaseSchema(QSqlDatabase& database, QString* error);

} // namespace mnce
