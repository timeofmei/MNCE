#pragma once

#include "domain/playback.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

namespace mnce {

class PlayerController;

struct MediaDetailData
{
    MediaSourceReference source;
    QString displayName;
    QString languageName;
    QString fileStateText;
    QString transcriptionStateText;
};

class MediaDetailWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MediaDetailWidget(QWidget* parent = nullptr);

    void setMedia(const MediaDetailData& media);
    void updatePlayback(const PlayerController& controller);
    void setPlaybackRate(qreal rate);

signals:
    void backRequested();
    void playRequested();
    void pauseRequested();
    void seekRequested(qint64 positionMs);
    void playbackRateChanged(qreal rate);
    void relocateRequested();

private:
    std::optional<qint64> durationMs_;
    QPushButton* backButton_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* languageLabel_ = nullptr;
    QLabel* fileStateLabel_ = nullptr;
    QLabel* transcriptionStateLabel_ = nullptr;
    QLabel* durationLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QLabel* positionLabel_ = nullptr;
    QLabel* playbackDurationLabel_ = nullptr;
    QPushButton* relocateButton_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QSlider* positionSlider_ = nullptr;
    QComboBox* rateCombo_ = nullptr;
};

} // namespace mnce
