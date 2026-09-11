#pragma once

#include "services/playback-backend.h"

class QAudioOutput;
class QMediaPlayer;

namespace mnce {

class QtPlaybackBackend final : public PlaybackBackend
{
    Q_OBJECT

public:
    explicit QtPlaybackBackend(QObject* parent = nullptr);
    ~QtPlaybackBackend() override;

    void load(const QString& absolutePath, quint64 requestId, qreal rate) override;
    void play() override;
    void pause() override;
    void stop() override;
    void setPosition(qint64 positionMs) override;
    void setPlaybackRate(qreal rate) override;

private:
    void releasePlayer();

    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QString sourcePath_;
    quint64 requestId_ = 0;
    bool loadReported_ = false;
};

} // namespace mnce
