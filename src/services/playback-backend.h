#pragma once

#include "domain/playback.h"

#include <QObject>

namespace mnce {

enum class BackendPlaybackState { Stopped, Playing, Paused };

class PlaybackBackend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~PlaybackBackend() override = default;

    virtual void load(const QString& absolutePath, quint64 requestId, qreal rate) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void setPosition(qint64 positionMs) = 0;
    virtual void setPlaybackRate(qreal rate) = 0;

signals:
    void loaded(quint64 requestId, qint64 durationMs, bool seekable);
    void stateChanged(quint64 requestId, mnce::BackendPlaybackState state);
    void positionChanged(quint64 requestId, qint64 positionMs);
    void durationChanged(quint64 requestId, qint64 durationMs);
    void seekableChanged(quint64 requestId, bool seekable);
    void ended(quint64 requestId);
    void failed(quint64 requestId, const mnce::PlaybackError& error);
};

} // namespace mnce

Q_DECLARE_METATYPE(mnce::BackendPlaybackState)
Q_DECLARE_METATYPE(mnce::PlaybackError)
