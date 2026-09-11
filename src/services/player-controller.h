#pragma once

#include "domain/playback.h"
#include "services/playback-backend.h"

#include <QObject>

#include <optional>

namespace mnce {

class PlayerController final : public QObject
{
    Q_OBJECT

public:
    explicit PlayerController(PlaybackBackend& backend, QObject* parent = nullptr);
    ~PlayerController() override;

    void load(const MediaSourceReference& source);
    void unload();
    void shutdown();
    [[nodiscard]] bool play();
    [[nodiscard]] bool pause();
    [[nodiscard]] bool seek(qint64 positionMs);
    [[nodiscard]] bool setPlaybackRate(qreal rate);

    [[nodiscard]] PlaybackState state() const;
    [[nodiscard]] qint64 positionMs() const;
    [[nodiscard]] std::optional<qint64> durationMs() const;
    [[nodiscard]] bool isSeekable() const;
    [[nodiscard]] qreal playbackRate() const;
    [[nodiscard]] std::optional<PlaybackError> error() const;
    [[nodiscard]] std::optional<MediaSourceReference> source() const;
    [[nodiscard]] quint64 requestId() const;

signals:
    void snapshotChanged();
    void durationDiscovered(const mnce::MediaSourceReference& source, qint64 durationMs);

private:
    void setState(PlaybackState state);
    [[nodiscard]] bool accepts(quint64 requestId) const;

    PlaybackBackend& backend_;
    quint64 requestId_ = 0;
    PlaybackState state_ = PlaybackState::Unloaded;
    qint64 positionMs_ = 0;
    std::optional<qint64> durationMs_;
    bool seekable_ = false;
    qreal playbackRate_ = 1.0;
    std::optional<PlaybackError> error_;
    std::optional<MediaSourceReference> source_;
    bool shutdown_ = false;
};

} // namespace mnce

Q_DECLARE_METATYPE(mnce::MediaSourceReference)
