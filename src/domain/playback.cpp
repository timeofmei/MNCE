#include "domain/playback.h"

#include <QtGlobal>

namespace mnce {

QString formatPlaybackTime(qint64 milliseconds)
{
    const qint64 totalSeconds = qMax<qint64>(0, milliseconds) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

const QVector<qreal>& supportedPlaybackRates()
{
    static const QVector<qreal> rates{0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    return rates;
}

bool isSupportedPlaybackRate(qreal rate)
{
    for (const qreal supported : supportedPlaybackRates()) {
        if (qFuzzyCompare(rate, supported)) {
            return true;
        }
    }
    return false;
}

qreal playbackRateFromSetting(const QVariant& value)
{
    bool converted = false;
    const qreal rate = value.toDouble(&converted);
    return converted && isSupportedPlaybackRate(rate) ? rate : 1.0;
}

} // namespace mnce
