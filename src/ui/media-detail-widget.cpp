#include "ui/media-detail-widget.h"

#include "services/player-controller.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QtMath>

namespace mnce {
namespace {

QString playbackStateText(PlaybackState state)
{
    switch (state) {
    case PlaybackState::Unloaded: return QStringLiteral("未加载");
    case PlaybackState::Loading: return QStringLiteral("正在加载…");
    case PlaybackState::Stopped: return QStringLiteral("已停止");
    case PlaybackState::Playing: return QStringLiteral("正在播放");
    case PlaybackState::Paused: return QStringLiteral("已暂停");
    case PlaybackState::Error: return QStringLiteral("播放不可用");
    }
    return {};
}

QString durationText(const std::optional<qint64>& duration)
{
    return duration ? formatPlaybackTime(*duration) : QStringLiteral("—");
}

} // namespace

MediaDetailWidget::MediaDetailWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mediaDetailPage"));
    auto* root = new QVBoxLayout(this);
    auto* heading = new QHBoxLayout;
    backButton_ = new QPushButton(QStringLiteral("返回媒体库"), this);
    backButton_->setObjectName(QStringLiteral("detailBackButton"));
    heading->addWidget(backButton_);
    nameLabel_ = new QLabel(this);
    nameLabel_->setObjectName(QStringLiteral("mediaDetailName"));
    QFont nameFont = nameLabel_->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 2);
    nameLabel_->setFont(nameFont);
    heading->addWidget(nameLabel_);
    heading->addStretch();
    relocateButton_ = new QPushButton(this);
    relocateButton_->setObjectName(QStringLiteral("detailRelocateButton"));
    heading->addWidget(relocateButton_);
    root->addLayout(heading);

    auto* information = new QFormLayout;
    pathLabel_ = new QLabel(this);
    pathLabel_->setObjectName(QStringLiteral("mediaDetailPath"));
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel_->setWordWrap(true);
    languageLabel_ = new QLabel(this);
    languageLabel_->setObjectName(QStringLiteral("mediaDetailLanguage"));
    fileStateLabel_ = new QLabel(this);
    fileStateLabel_->setObjectName(QStringLiteral("mediaDetailFileState"));
    transcriptionStateLabel_ = new QLabel(this);
    transcriptionStateLabel_->setObjectName(QStringLiteral("mediaDetailTranscriptionState"));
    durationLabel_ = new QLabel(this);
    durationLabel_->setObjectName(QStringLiteral("mediaDetailDuration"));
    information->addRow(QStringLiteral("路径："), pathLabel_);
    information->addRow(QStringLiteral("目标语言："), languageLabel_);
    information->addRow(QStringLiteral("文件状态："), fileStateLabel_);
    information->addRow(QStringLiteral("识别状态："), transcriptionStateLabel_);
    information->addRow(QStringLiteral("时长："), durationLabel_);
    root->addLayout(information);

    auto* playback = new QVBoxLayout;
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("playbackStatusLabel"));
    playback->addWidget(statusLabel_);
    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("playbackErrorLabel"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QStringLiteral("color: #b3261e;"));
    playback->addWidget(errorLabel_);

    auto* progress = new QHBoxLayout;
    positionLabel_ = new QLabel(QStringLiteral("00:00"), this);
    positionLabel_->setObjectName(QStringLiteral("playbackPositionLabel"));
    positionSlider_ = new QSlider(Qt::Horizontal, this);
    positionSlider_->setObjectName(QStringLiteral("playbackPositionSlider"));
    positionSlider_->setRange(0, 1000);
    playbackDurationLabel_ = new QLabel(QStringLiteral("—"), this);
    playbackDurationLabel_->setObjectName(QStringLiteral("playbackTotalDurationLabel"));
    progress->addWidget(positionLabel_);
    progress->addWidget(positionSlider_, 1);
    progress->addWidget(playbackDurationLabel_);
    playback->addLayout(progress);

    auto* controls = new QHBoxLayout;
    playButton_ = new QPushButton(QStringLiteral("播放"), this);
    playButton_->setObjectName(QStringLiteral("playButton"));
    pauseButton_ = new QPushButton(QStringLiteral("暂停"), this);
    pauseButton_->setObjectName(QStringLiteral("pauseButton"));
    rateCombo_ = new QComboBox(this);
    rateCombo_->setObjectName(QStringLiteral("playbackRateCombo"));
    for (const qreal rate : supportedPlaybackRates()) {
        const int decimals = qFuzzyCompare(rate, 0.75) || qFuzzyCompare(rate, 1.25) ? 2 : 1;
        rateCombo_->addItem(QStringLiteral("%1×").arg(rate, 0, 'f', decimals), rate);
    }
    controls->addWidget(playButton_);
    controls->addWidget(pauseButton_);
    controls->addStretch();
    controls->addWidget(new QLabel(QStringLiteral("倍速："), this));
    controls->addWidget(rateCombo_);
    playback->addLayout(controls);
    root->addLayout(playback);
    root->addStretch();

    connect(backButton_, &QPushButton::clicked, this, &MediaDetailWidget::backRequested);
    connect(playButton_, &QPushButton::clicked, this, &MediaDetailWidget::playRequested);
    connect(pauseButton_, &QPushButton::clicked, this, &MediaDetailWidget::pauseRequested);
    connect(relocateButton_, &QPushButton::clicked, this, &MediaDetailWidget::relocateRequested);
    connect(positionSlider_, &QSlider::sliderReleased, this, [this] {
        if (!durationMs_) return;
        emit seekRequested(qRound64(static_cast<qreal>(*durationMs_)
                                    * positionSlider_->value() / positionSlider_->maximum()));
    });
    connect(rateCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        emit playbackRateChanged(rateCombo_->currentData().toDouble());
    });
}

void MediaDetailWidget::setMedia(const MediaDetailData& media)
{
    nameLabel_->setText(media.displayName);
    pathLabel_->setText(media.source.absolutePath);
    languageLabel_->setText(media.languageName);
    fileStateLabel_->setText(media.fileStateText);
    transcriptionStateLabel_->setText(media.transcriptionStateText);
    durationMs_ = media.source.knownDurationMs;
    durationLabel_->setText(durationText(durationMs_));
    playbackDurationLabel_->setText(durationText(durationMs_));
    relocateButton_->setText(media.source.kind == MediaSourceKind::SingleFile
                                 ? QStringLiteral("重新定位文件")
                                 : QStringLiteral("重新定位系列"));
    relocateButton_->setVisible(!media.source.available);
}

void MediaDetailWidget::updatePlayback(const PlayerController& controller)
{
    const PlaybackState state = controller.state();
    durationMs_ = controller.durationMs();
    statusLabel_->setText(playbackStateText(state));
    durationLabel_->setText(durationText(durationMs_));
    playbackDurationLabel_->setText(durationText(durationMs_));
    positionLabel_->setText(formatPlaybackTime(controller.positionMs()));
    errorLabel_->setText(controller.error() ? controller.error()->userMessage : QString());
    errorLabel_->setVisible(controller.error().has_value());

    const bool ready = state == PlaybackState::Stopped || state == PlaybackState::Paused
        || state == PlaybackState::Playing;
    playButton_->setEnabled(state == PlaybackState::Stopped || state == PlaybackState::Paused);
    pauseButton_->setEnabled(state == PlaybackState::Playing);
    positionSlider_->setEnabled(ready && controller.isSeekable() && durationMs_.has_value());
    if (!positionSlider_->isSliderDown()) {
        const int value = durationMs_ && *durationMs_ > 0
            ? qRound(static_cast<qreal>(controller.positionMs())
                     * positionSlider_->maximum() / *durationMs_)
            : 0;
        const QSignalBlocker blocker(positionSlider_);
        positionSlider_->setValue(qBound(0, value, positionSlider_->maximum()));
    }
    const bool canRelocate = controller.source().has_value()
        && (!controller.source()->available
            || (controller.error()
                && (controller.error()->kind == PlaybackErrorKind::MissingFile
                    || controller.error()->kind == PlaybackErrorKind::PermissionDenied)));
    relocateButton_->setVisible(canRelocate);
}

void MediaDetailWidget::setPlaybackRate(qreal rate)
{
    const int index = rateCombo_->findData(rate);
    const QSignalBlocker blocker(rateCombo_);
    rateCombo_->setCurrentIndex(index >= 0 ? index : rateCombo_->findData(1.0));
}

} // namespace mnce
