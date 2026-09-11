#include "services/qt-playback-backend.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QThread>
#include <QUrl>

namespace mnce {
namespace {

PlaybackError translateError(QMediaPlayer::Error error, const QString& sourcePath,
                             const QString& detail)
{
    switch (error) {
    case QMediaPlayer::AccessDeniedError:
        return {PlaybackErrorKind::PermissionDenied,
                QStringLiteral("无法读取音频文件，请检查文件权限。"), detail};
    case QMediaPlayer::FormatError:
        return {PlaybackErrorKind::UnsupportedFormat,
                QStringLiteral("不支持此音频格式。"), detail};
    case QMediaPlayer::ResourceError:
        if (!QFileInfo::exists(sourcePath)) {
            return {PlaybackErrorKind::MissingFile,
                    QStringLiteral("音频文件不存在，请重新定位后再试。"), detail};
        }
        return {PlaybackErrorKind::DecodeFailed,
                QStringLiteral("无法加载或解码音频文件。"), detail};
    case QMediaPlayer::NetworkError:
        return {PlaybackErrorKind::DecodeFailed,
                QStringLiteral("无法加载或解码音频文件。"), detail};
    case QMediaPlayer::NoError:
        break;
    }
    return {PlaybackErrorKind::DecodeFailed, QStringLiteral("无法播放音频文件。"), detail};
}

} // namespace

QtPlaybackBackend::QtPlaybackBackend(QObject* parent)
    : PlaybackBackend(parent)
{
}

QtPlaybackBackend::~QtPlaybackBackend()
{
    releasePlayer();
}

void QtPlaybackBackend::load(const QString& absolutePath, quint64 requestId, qreal rate)
{
    Q_ASSERT(thread() == QThread::currentThread());
    releasePlayer();
    sourcePath_ = absolutePath;
    requestId_ = requestId;
    loadReported_ = false;
    audioOutput_ = new QAudioOutput(this);
    player_ = new QMediaPlayer(this);
    player_->setAudioOutput(audioOutput_);
    player_->setPlaybackRate(rate);

    QMediaPlayer* const player = player_;
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [this, requestId, player](QMediaPlayer::PlaybackState state) {
        if (player != player_) return;
        switch (state) {
        case QMediaPlayer::StoppedState:
            emit stateChanged(requestId, BackendPlaybackState::Stopped);
            break;
        case QMediaPlayer::PlayingState:
            emit stateChanged(requestId, BackendPlaybackState::Playing);
            break;
        case QMediaPlayer::PausedState:
            emit stateChanged(requestId, BackendPlaybackState::Paused);
            break;
        }
    });
    connect(player, &QMediaPlayer::positionChanged, this,
            [this, requestId, player](qint64 value) {
        if (player == player_) emit positionChanged(requestId, value);
    });
    connect(player, &QMediaPlayer::durationChanged, this,
            [this, requestId, player](qint64 value) {
        if (player == player_) emit durationChanged(requestId, value);
    });
    connect(player, &QMediaPlayer::seekableChanged, this,
            [this, requestId, player](bool value) {
        if (player == player_) emit seekableChanged(requestId, value);
    });
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this, requestId, player](QMediaPlayer::MediaStatus status) {
        if (player != player_) return;
        if (status == QMediaPlayer::LoadedMedia && !loadReported_) {
            loadReported_ = true;
            emit loaded(requestId, player->duration(), player->isSeekable());
        } else if (status == QMediaPlayer::EndOfMedia) {
            emit ended(requestId);
        }
    });
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this, requestId, player](QMediaPlayer::Error error, const QString& detail) {
        if (player == player_) emit failed(requestId, translateError(error, sourcePath_, detail));
    });
    player_->setSource(QUrl::fromLocalFile(absolutePath));
}

void QtPlaybackBackend::play()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (player_) player_->play();
}

void QtPlaybackBackend::pause()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (player_) player_->pause();
}

void QtPlaybackBackend::stop()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (player_) player_->stop();
}

void QtPlaybackBackend::setPosition(qint64 positionMs)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (player_) player_->setPosition(positionMs);
}

void QtPlaybackBackend::setPlaybackRate(qreal rate)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (player_) player_->setPlaybackRate(rate);
}

void QtPlaybackBackend::releasePlayer()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (player_) {
        player_->stop();
        player_->disconnect(this);
        delete player_;
        player_ = nullptr;
    }
    delete audioOutput_;
    audioOutput_ = nullptr;
}

} // namespace mnce
