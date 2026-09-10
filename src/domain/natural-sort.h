#pragma once

#include <QStringView>

namespace mnce {

// Returns a negative value when left sorts before right, zero when their natural
// keys are equal, and a positive value otherwise.
[[nodiscard]] int compareNatural(QStringView left, QStringView right);

} // namespace mnce
