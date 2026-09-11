#include "domain/playback.h"

#include <QtTest>

class PlaybackDomainTest final : public QObject
{
    Q_OBJECT

private slots:
    void formatsTime();
    void validatesPlaybackRates();
};

void PlaybackDomainTest::formatsTime()
{
    QCOMPARE(mnce::formatPlaybackTime(0), QStringLiteral("00:00"));
    QCOMPARE(mnce::formatPlaybackTime(61'999), QStringLiteral("01:01"));
    QCOMPARE(mnce::formatPlaybackTime(3'661'000), QStringLiteral("1:01:01"));
    QCOMPARE(mnce::formatPlaybackTime(-1), QStringLiteral("00:00"));
}

void PlaybackDomainTest::validatesPlaybackRates()
{
    const QVector<qreal> expected{0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    QCOMPARE(mnce::supportedPlaybackRates(), expected);
    for (const qreal rate : expected) QVERIFY(mnce::isSupportedPlaybackRate(rate));
    QVERIFY(!mnce::isSupportedPlaybackRate(0.0));
    QVERIFY(!mnce::isSupportedPlaybackRate(1.1));
    QCOMPARE(mnce::playbackRateFromSetting(QVariant(1.25)), 1.25);
    QCOMPARE(mnce::playbackRateFromSetting(QVariant(QStringLiteral("invalid"))), 1.0);
    QCOMPARE(mnce::playbackRateFromSetting(QVariant(3.0)), 1.0);
    QCOMPARE(mnce::playbackRateFromSetting(QVariant()), 1.0);
}

QTEST_GUILESS_MAIN(PlaybackDomainTest)

#include "playback-domain-test.moc"
