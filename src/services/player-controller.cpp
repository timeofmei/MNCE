#include "services/player-controller.h"

#include <QFileInfo>

namespace mnce {

PlayerController::PlayerController(PlaybackBackend& backend, QObject* parent)
    : QObject(parent)
    , backend_(backend)
{
    connect(&backend_, &PlaybackBackend::loaded, this,
            [this](quint64 id, qint64 duration, bool seekable) {
        if (!accepts(id)) return;
        const bool completesLoad = state_ == PlaybackState::Loading;
        const bool durationChanged = duration >= 0
            && (!durationMs_.has_value() || *durationMs_ != duration);
        if (duration >= 0) durationMs_ = duration;
        seekable_ = seekable;
        error_.reset();
        if (completesLoad) {
            setState(PlaybackState::Stopped);
        } else {
            emit snapshotChanged();
        }
        if (durationChanged) emit durationDiscovered(*source_, duration);
    });
    connect(&backend_, &PlaybackBackend::stateChanged, this,
            [this](quint64 id, BackendPlaybackState state) {
        if (!accepts(id) || state_ == PlaybackState::Loading || state_ == PlaybackState::Error) {
            return;
        }
        switch (state) {
        case BackendPlaybackState::Stopped: setState(PlaybackState::Stopped); break;
        case BackendPlaybackState::Playing: setState(PlaybackState::Playing); break;
        case BackendPlaybackState::Paused: setState(PlaybackState::Paused); break;
        }
    });
    connect(&backend_, &PlaybackBackend::positionChanged, this,
            [this](quint64 id, qint64 position) {
        if (!accepts(id)) return;
        positionMs_ = qMax<qint64>(0, position);
        emit snapshotChanged();
    });
    connect(&backend_, &PlaybackBackend::durationChanged, this,
            [this](quint64 id, qint64 duration) {
        if (!accepts(id) || duration < 0) return;
        const bool changed = !durationMs_.has_value() || *durationMs_ != duration;
        durationMs_ = duration;
        emit snapshotChanged();
        if (changed) emit durationDiscovered(*source_, duration);
    });
    connect(&backend_, &PlaybackBackend::seekableChanged, this,
            [this](quint64 id, bool seekable) {
        if (!accepts(id)) return;
        seekable_ = seekable;
        emit snapshotChanged();
    });
    connect(&backend_, &PlaybackBackend::ended, this, [this](quint64 id) {
        if (!accepts(id)) return;
        if (durationMs_) positionMs_ = *durationMs_;
        setState(PlaybackState::Stopped);
    });
    connect(&backend_, &PlaybackBackend::failed, this,
            [this](quint64 id, const PlaybackError& error) {
        if (!accepts(id)) return;
        error_ = error;
        seekable_ = false;
        setState(PlaybackState::Error);
    });
}

PlayerController::~PlayerController()
{
    shutdown();
}

void PlayerController::load(const MediaSourceReference& source)
{
    if (shutdown_) return;
    ++requestId_;
    backend_.stop();
    source_ = source;
    positionMs_ = 0;
    durationMs_ = source.knownDurationMs && *source.knownDurationMs >= 0
        ? source.knownDurationMs : std::nullopt;
    seekable_ = false;
    error_.reset();

    const QFileInfo file(source.absolutePath);
    if (!source.available || !file.exists()) {
        error_ = PlaybackError{PlaybackErrorKind::MissingFile,
                               QStringLiteral("音频文件不存在，请重新定位后再试。"), {}};
        setState(PlaybackState::Error);
        return;
    }
    if (!file.isFile() || !file.isReadable()) {
        error_ = PlaybackError{PlaybackErrorKind::PermissionDenied,
                               QStringLiteral("无法读取音频文件，请检查文件权限。"), {}};
        setState(PlaybackState::Error);
        return;
    }
    setState(PlaybackState::Loading);
    backend_.load(file.absoluteFilePath(), requestId_, playbackRate_);
}

void PlayerController::unload()
{
    if (shutdown_) return;
    ++requestId_;
    backend_.stop();
    source_.reset();
    positionMs_ = 0;
    durationMs_.reset();
    seekable_ = false;
    error_.reset();
    setState(PlaybackState::Unloaded);
}

void PlayerController::shutdown()
{
    if (shutdown_) return;
    shutdown_ = true;
    ++requestId_;
    backend_.stop();
    disconnect(&backend_, nullptr, this, nullptr);
    source_.reset();
    positionMs_ = 0;
    durationMs_.reset();
    seekable_ = false;
    error_.reset();
    state_ = PlaybackState::Unloaded;
}

bool PlayerController::play()
{
    if (state_ != PlaybackState::Stopped && state_ != PlaybackState::Paused) return false;
    if (durationMs_ && positionMs_ >= *durationMs_) backend_.setPosition(0);
    backend_.play();
    return true;
}

bool PlayerController::pause()
{
    if (state_ != PlaybackState::Playing) return false;
    backend_.pause();
    return true;
}

bool PlayerController::seek(qint64 positionMs)
{
    if (!seekable_ || positionMs < 0 || state_ == PlaybackState::Loading
        || state_ == PlaybackState::Error || state_ == PlaybackState::Unloaded) return false;
    if (durationMs_) positionMs = qMin(positionMs, *durationMs_);
    backend_.setPosition(positionMs);
    return true;
}

bool PlayerController::setPlaybackRate(qreal rate)
{
    if (shutdown_ || !isSupportedPlaybackRate(rate)) return false;
    playbackRate_ = rate;
    backend_.setPlaybackRate(rate);
    emit snapshotChanged();
    return true;
}

PlaybackState PlayerController::state() const { return state_; }
qint64 PlayerController::positionMs() const { return positionMs_; }
std::optional<qint64> PlayerController::durationMs() const { return durationMs_; }
bool PlayerController::isSeekable() const { return seekable_; }
qreal PlayerController::playbackRate() const { return playbackRate_; }
std::optional<PlaybackError> PlayerController::error() const { return error_; }
std::optional<MediaSourceReference> PlayerController::source() const { return source_; }
quint64 PlayerController::requestId() const { return requestId_; }

void PlayerController::setState(PlaybackState state)
{
    state_ = state;
    emit snapshotChanged();
}

bool PlayerController::accepts(quint64 requestId) const
{
    return source_.has_value() && requestId == requestId_;
}

} // namespace mnce
