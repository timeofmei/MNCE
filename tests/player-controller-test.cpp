#include "services/player-controller.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class FakePlaybackBackend final : public mnce::PlaybackBackend
{
    Q_OBJECT

public:
    using PlaybackBackend::PlaybackBackend;

    void load(const QString& path, quint64 id, qreal rate) override
    {
        loadedPath = path;
        activeId = id;
        loadedRate = rate;
        ++loadCalls;
    }
    void play() override { ++playCalls; }
    void pause() override { ++pauseCalls; }
    void stop() override { ++stopCalls; }
    void setPosition(qint64 position) override { positions.push_back(position); }
    void setPlaybackRate(qreal rate) override { rates.push_back(rate); }

    void reportLoaded(quint64 id, qint64 duration, bool seekable)
    { emit loaded(id, duration, seekable); }
    void reportState(quint64 id, mnce::BackendPlaybackState state)
    { emit stateChanged(id, state); }
    void reportPosition(quint64 id, qint64 position)
    { emit positionChanged(id, position); }
    void reportEnded(quint64 id) { emit ended(id); }
    void reportFailure(quint64 id, const mnce::PlaybackError& error)
    { emit failed(id, error); }

    QString loadedPath;
    quint64 activeId = 0;
    qreal loadedRate = 0;
    int loadCalls = 0;
    int playCalls = 0;
    int pauseCalls = 0;
    int stopCalls = 0;
    QVector<qint64> positions;
    QVector<qreal> rates;
};

class PlayerControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadAndControlLifecycle();
    void handlesErrorsAndUnseekableMedia();
    void ignoresLateCallbacksAfterSwitch();
    void rejectsMissingFilesWithoutLoadingBackend();
};

static QString makeFile(QTemporaryDir& directory, const QString& name)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write("audio") != 5) return {};
    return path;
}

void PlayerControllerTest::loadAndControlLifecycle()
{
    QTemporaryDir directory;
    const QString path = makeFile(directory, QStringLiteral("one.wav"));
    FakePlaybackBackend backend;
    mnce::PlayerController controller(backend);
    QVERIFY(controller.setPlaybackRate(1.25));
    QVERIFY(!controller.setPlaybackRate(1.1));
    controller.load({mnce::MediaSourceKind::SingleFile, 7, path, true, std::nullopt});
    QCOMPARE(controller.state(), mnce::PlaybackState::Loading);
    QCOMPARE(backend.loadCalls, 1);
    QCOMPARE(backend.playCalls, 0);
    QCOMPARE(backend.loadedRate, 1.25);
    QVERIFY(!controller.play());

    const quint64 id = controller.requestId();
    backend.reportLoaded(id, 90'000, true);
    QCOMPARE(controller.state(), mnce::PlaybackState::Stopped);
    QCOMPARE(controller.durationMs(), std::optional<qint64>(90'000));
    QVERIFY(controller.isSeekable());
    QVERIFY(controller.play());
    QCOMPARE(backend.playCalls, 1);
    backend.reportState(id, mnce::BackendPlaybackState::Playing);
    QCOMPARE(controller.state(), mnce::PlaybackState::Playing);
    backend.reportPosition(id, 12'000);
    QCOMPARE(controller.positionMs(), 12'000);
    QVERIFY(controller.seek(100'000));
    QCOMPARE(backend.positions.constLast(), 90'000);
    backend.reportLoaded(id, 90'000, true);
    QCOMPARE(controller.state(), mnce::PlaybackState::Playing);
    QVERIFY(controller.pause());
    backend.reportState(id, mnce::BackendPlaybackState::Paused);
    QCOMPARE(controller.state(), mnce::PlaybackState::Paused);
    backend.reportState(id, mnce::BackendPlaybackState::Stopped);
    QCOMPARE(controller.state(), mnce::PlaybackState::Stopped);
    backend.reportEnded(id);
    QCOMPARE(controller.positionMs(), 90'000);
}

void PlayerControllerTest::handlesErrorsAndUnseekableMedia()
{
    QTemporaryDir directory;
    const QString path = makeFile(directory, QStringLiteral("one.mp3"));
    FakePlaybackBackend backend;
    mnce::PlayerController controller(backend);
    controller.load({mnce::MediaSourceKind::SeriesItem, 9, path, true, 123});
    const quint64 id = controller.requestId();
    backend.reportLoaded(id, 123, false);
    QVERIFY(!controller.seek(10));
    backend.reportFailure(id, {mnce::PlaybackErrorKind::UnsupportedFormat,
                               QStringLiteral("不支持此音频格式。"), QStringLiteral("codec")});
    QCOMPARE(controller.state(), mnce::PlaybackState::Error);
    QVERIFY(controller.error().has_value());
    QCOMPARE(controller.error()->kind, mnce::PlaybackErrorKind::UnsupportedFormat);
    QVERIFY(!controller.play());
}

void PlayerControllerTest::ignoresLateCallbacksAfterSwitch()
{
    QTemporaryDir directory;
    const QString first = makeFile(directory, QStringLiteral("first.wav"));
    const QString second = makeFile(directory, QStringLiteral("second.wav"));
    FakePlaybackBackend backend;
    mnce::PlayerController controller(backend);
    controller.load({mnce::MediaSourceKind::SingleFile, 1, first, true, std::nullopt});
    const quint64 oldId = controller.requestId();
    controller.load({mnce::MediaSourceKind::SeriesItem, 2, second, true, std::nullopt});
    const quint64 currentId = controller.requestId();
    QVERIFY(currentId > oldId);
    backend.reportLoaded(oldId, 999, true);
    backend.reportFailure(oldId, {mnce::PlaybackErrorKind::DecodeFailed,
                                  QStringLiteral("old"), {}});
    QCOMPARE(controller.state(), mnce::PlaybackState::Loading);
    QVERIFY(!controller.durationMs().has_value());
    QCOMPARE(controller.source()->kind, mnce::MediaSourceKind::SeriesItem);
    QCOMPARE(controller.source()->mediaId, 2);
    backend.reportLoaded(currentId, 456, true);
    QCOMPARE(controller.durationMs(), std::optional<qint64>(456));
}

void PlayerControllerTest::rejectsMissingFilesWithoutLoadingBackend()
{
    QTemporaryDir directory;
    FakePlaybackBackend backend;
    mnce::PlayerController controller(backend);
    controller.load({mnce::MediaSourceKind::SingleFile, 4,
                     directory.filePath(QStringLiteral("missing.wav")), true, std::nullopt});
    QCOMPARE(controller.state(), mnce::PlaybackState::Error);
    QCOMPARE(controller.error()->kind, mnce::PlaybackErrorKind::MissingFile);
    QCOMPARE(backend.loadCalls, 0);
    QVERIFY(!controller.seek(0));
}

QTEST_GUILESS_MAIN(PlayerControllerTest)

#include "player-controller-test.moc"
