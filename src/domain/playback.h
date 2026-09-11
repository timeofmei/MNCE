#pragma once

#include <QString>
#include <QVariant>
#include <QVector>

#include <optional>

namespace mnce {

enum class PlaybackState { Unloaded, Loading, Stopped, Playing, Paused, Error };
enum class MediaSourceKind { SingleFile, SeriesItem };
enum class PlaybackErrorKind { MissingFile, PermissionDenied, UnsupportedFormat, DecodeFailed };

struct MediaSourceReference
{
    MediaSourceKind kind = MediaSourceKind::SingleFile;
    qint64 mediaId = 0;
    QString absolutePath;
    bool available = true;
    std::optional<qint64> knownDurationMs;
};

struct PlaybackError
{
    PlaybackErrorKind kind = PlaybackErrorKind::DecodeFailed;
    QString userMessage;
    QString diagnostic;
};

[[nodiscard]] QString formatPlaybackTime(qint64 milliseconds);
[[nodiscard]] const QVector<qreal>& supportedPlaybackRates();
[[nodiscard]] bool isSupportedPlaybackRate(qreal rate);
[[nodiscard]] qreal playbackRateFromSetting(const QVariant& value);

} // namespace mnce
