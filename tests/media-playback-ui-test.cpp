#include "persistence/media-repository.h"
#include "persistence/series-repository.h"
#include "services/file-hash-service.h"
#include "services/playback-backend.h"
#include "ui/main-window.h"

#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

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
    void setPosition(qint64 position) override
    {
        positions.push_back(position);
        if (reportLoadedOnSeek) emit loaded(activeId, seekDuration, true);
    }
    void setPlaybackRate(qreal rate) override { rates.push_back(rate); }

    void reportLoaded(quint64 id, qint64 duration, bool seekable)
    { emit loaded(id, duration, seekable); }
    void reportState(quint64 id, mnce::BackendPlaybackState state)
    { emit stateChanged(id, state); }
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
    bool reportLoadedOnSeek = false;
    qint64 seekDuration = 0;
};

class FakeDialogs final : public mnce::LibraryUiDialogs
{
public:
    QString chooseAudioFile(QWidget*, bool) override
    { return audioFiles.isEmpty() ? QString{} : audioFiles.takeFirst(); }
    QString chooseSeriesDirectory(QWidget*, bool) override
    { return directories.isEmpty() ? QString{} : directories.takeFirst(); }
    std::optional<QString> requestSeriesName(QWidget*) override { return std::nullopt; }
    bool confirmSeriesDeletion(QWidget*, const QString&, int) override { return false; }
    void showMessage(QWidget*, mnce::UserMessageKind, const QString&, const QString& text) override
    { messages.push_back(text); }

    QStringList audioFiles;
    QStringList directories;
    QStringList messages;
};

class MediaPlaybackUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void controlsSingleFileAndPersistsDuration();
    void switchesSeriesMediaWithoutAutoPlayAndIgnoresLateEvents();
    void restoresPlaybackRateAcrossWindows();
    void displaysBackendFailureAndRemainsNavigable();
    void relocatesMissingSingleAndReloadsIt();
};

namespace {

QByteArray shortWav()
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + 160);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000)
           << quint32(16000) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(160);
    for (int index = 0; index < 80; ++index) stream << qint16(0);
    return bytes;
}

QString writeWav(const QString& directory, const QString& name, char marker = 0)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    QByteArray bytes = shortWav();
    bytes[bytes.size() - 1] = marker;
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}

mnce::MediaItem singleItem(const QString& path, char hashSuffix = 's')
{
    const auto identity = mnce::FileHashService{}.hashFile(path);
    mnce::MediaItem item;
    item.contentHash = identity.succeeded() ? identity.hash : QByteArray(32, hashSuffix);
    item.fileSize = identity.succeeded() ? identity.fileSize : shortWav().size();
    item.currentPath = path;
    item.displayName = QFileInfo(path).fileName();
    item.targetLanguageId = QStringLiteral("en");
    return item;
}

mnce::SeriesFileSnapshot snapshot(const QString& path)
{
    const auto identity = mnce::FileHashService{}.hashFile(path);
    return {identity.hash, identity.fileSize, QFileInfo(path).absoluteFilePath(),
            QFileInfo(path).fileName()};
}

void clickName(QTableWidget* table, int row)
{
    const QRect rectangle = table->visualItemRect(table->item(row, 0));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, rectangle.center());
}

void waitForInitialRefresh(mnce::MainWindow& window)
{
    auto* refresh = window.findChild<QPushButton*>(QStringLiteral("refreshButton"));
    QTRY_VERIFY(refresh != nullptr && refresh->isEnabled());
}

} // namespace

void MediaPlaybackUiTest::controlsSingleFileAndPersistsDuration()
{
    QTemporaryDir temporary;
    const QString mediaPath = writeWav(temporary.path(), QStringLiteral("single.wav"));
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        QVERIFY(repository.insert(singleItem(mediaPath)).status == mnce::InsertMediaStatus::Inserted);
    }
    auto dialogs = std::make_shared<FakeDialogs>();
    auto backendOwner = std::make_unique<FakePlaybackBackend>();
    auto* backend = backendOwner.get();
    mnce::MainWindow window(catalog, dialogs, std::move(backendOwner));
    QString error;
    QVERIFY(window.initialize(databasePath, temporary.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    waitForInitialRefresh(window);
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("mediaTable"));
    clickName(table, 0);
    QCOMPARE(backend->loadCalls, 1);
    QCOMPARE(backend->playCalls, 0);
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("playbackStatusLabel"))->text(),
             QStringLiteral("正在加载…"));

    const quint64 id = backend->activeId;
    backend->reportLoaded(id, 60'000, true);
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("mediaDetailDuration"))->text(),
             QStringLiteral("01:00"));
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("playButton")), Qt::LeftButton);
    QCOMPARE(backend->playCalls, 1);
    backend->reportState(id, mnce::BackendPlaybackState::Playing);
    backend->reportLoadedOnSeek = true;
    backend->seekDuration = 60'000;
    auto* slider = window.findChild<QSlider*>(QStringLiteral("playbackPositionSlider"));
    slider->setValue(500);
    QVERIFY(QMetaObject::invokeMethod(slider, "sliderReleased"));
    QCOMPARE(backend->positions.constLast(), 30'000);
    auto* pause = window.findChild<QPushButton*>(QStringLiteral("pauseButton"));
    QVERIFY(pause->isEnabled());
    QTest::mouseClick(pause, Qt::LeftButton);
    QCOMPARE(backend->pauseCalls, 1);
    backend->reportState(id, mnce::BackendPlaybackState::Paused);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("stopButton")) == nullptr);

    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("detailBackButton")), Qt::LeftButton);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("01:00"));
    mnce::MediaRepository repository(catalog);
    QVERIFY(repository.open(databasePath, &error));
    QCOMPARE(repository.allItems().constFirst().durationMs, std::optional<qint64>(60'000));
}

void MediaPlaybackUiTest::switchesSeriesMediaWithoutAutoPlayAndIgnoresLateEvents()
{
    QTemporaryDir temporary;
    const QString first = writeWav(temporary.path(), QStringLiteral("lesson1.wav"));
    const QString second = writeWav(temporary.path(), QStringLiteral("lesson2.wav"), 1);
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::SeriesRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        QVERIFY(repository.create(QStringLiteral("Lessons"), QStringLiteral("en"), temporary.path(),
                                  {snapshot(first), snapshot(second)}).succeeded());
    }
    auto backendOwner = std::make_unique<FakePlaybackBackend>();
    auto* backend = backendOwner.get();
    mnce::MainWindow window(catalog, std::make_shared<FakeDialogs>(), std::move(backendOwner));
    QString error;
    QVERIFY(window.initialize(databasePath, temporary.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    waitForInitialRefresh(window);
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("libraryTabs"));
    tabs->setCurrentIndex(1);
    auto* series = window.findChild<QTableWidget*>(QStringLiteral("seriesTable"));
    clickName(series, 0);
    auto* media = window.findChild<QTableWidget*>(QStringLiteral("seriesMediaTable"));
    QCOMPARE(media->rowCount(), 2);
    clickName(media, 0);
    const quint64 oldId = backend->activeId;
    backend->reportLoaded(oldId, 100, true);
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("playButton")), Qt::LeftButton);
    backend->reportState(oldId, mnce::BackendPlaybackState::Playing);
    const int playCalls = backend->playCalls;
    const int stopCalls = backend->stopCalls;
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("detailBackButton")), Qt::LeftButton);
    QCOMPARE(media->item(0, 1)->text(), QStringLiteral("00:00"));
    clickName(media, 1);
    const quint64 newId = backend->activeId;
    QVERIFY(newId > oldId);
    QVERIFY(backend->stopCalls >= stopCalls + 2);
    QCOMPARE(backend->playCalls, playCalls);
    backend->reportLoaded(oldId, 999, true);
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("mediaDetailDuration"))->text(),
             QStringLiteral("—"));
    backend->reportLoaded(newId, 200, true);
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("mediaDetailDuration"))->text(),
             QStringLiteral("00:00"));
    mnce::SeriesRepository repository(catalog);
    QVERIFY(repository.open(databasePath, &error));
    const auto persisted = repository.mediaForSeries(
        repository.seriesForLanguage(QStringLiteral("en")).constFirst().id);
    QCOMPARE(persisted.size(), 2);
    QCOMPARE(persisted.at(0).durationMs, std::optional<qint64>(100));
    QCOMPARE(persisted.at(1).durationMs, std::optional<qint64>(200));
}

void MediaPlaybackUiTest::restoresPlaybackRateAcrossWindows()
{
    QTemporaryDir temporary;
    const QString mediaPath = writeWav(temporary.path(), QStringLiteral("rate.wav"));
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    const QString settingsPath = temporary.filePath(QStringLiteral("settings.ini"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        QVERIFY(repository.insert(singleItem(mediaPath)).status == mnce::InsertMediaStatus::Inserted);
    }
    {
        auto backend = std::make_unique<FakePlaybackBackend>();
        mnce::MainWindow window(catalog, std::make_shared<FakeDialogs>(), std::move(backend));
        QString error;
        QVERIFY(window.initialize(databasePath, settingsPath, &error));
        auto* rate = window.findChild<QComboBox*>(QStringLiteral("playbackRateCombo"));
        rate->setCurrentIndex(rate->findData(1.5));
        QCOMPARE(rate->currentData().toDouble(), 1.5);
    }
    {
        auto backendOwner = std::make_unique<FakePlaybackBackend>();
        auto* backend = backendOwner.get();
        mnce::MainWindow window(catalog, std::make_shared<FakeDialogs>(), std::move(backendOwner));
        QString error;
        QVERIFY(window.initialize(databasePath, settingsPath, &error));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("playbackRateCombo"))
                     ->currentData().toDouble(), 1.5);
        window.show();
        waitForInitialRefresh(window);
        clickName(window.findChild<QTableWidget*>(QStringLiteral("mediaTable")), 0);
        QCOMPARE(backend->loadedRate, 1.5);
    }
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("playback/rate"), QStringLiteral("invalid"));
    }
    {
        auto backend = std::make_unique<FakePlaybackBackend>();
        mnce::MainWindow window(catalog, std::make_shared<FakeDialogs>(), std::move(backend));
        QString error;
        QVERIFY(window.initialize(databasePath, settingsPath, &error));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("playbackRateCombo"))
                     ->currentData().toDouble(), 1.0);
    }
}

void MediaPlaybackUiTest::displaysBackendFailureAndRemainsNavigable()
{
    QTemporaryDir temporary;
    const QString mediaPath = writeWav(temporary.path(), QStringLiteral("bad.wav"));
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        QVERIFY(repository.insert(singleItem(mediaPath)).status == mnce::InsertMediaStatus::Inserted);
    }
    auto backendOwner = std::make_unique<FakePlaybackBackend>();
    auto* backend = backendOwner.get();
    mnce::MainWindow window(catalog, std::make_shared<FakeDialogs>(), std::move(backendOwner));
    QString error;
    QVERIFY(window.initialize(databasePath, temporary.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    waitForInitialRefresh(window);
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("mediaTable"));
    clickName(table, 0);
    backend->reportFailure(backend->activeId,
                           {mnce::PlaybackErrorKind::UnsupportedFormat,
                            QStringLiteral("不支持此音频格式。"), QStringLiteral("decoder")});
    auto* errorLabel = window.findChild<QLabel*>(QStringLiteral("playbackErrorLabel"));
    QCOMPARE(errorLabel->text(), QStringLiteral("不支持此音频格式。"));
    QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("playButton"))->isEnabled());
    QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("detailBackButton")), Qt::LeftButton);
    clickName(table, 0);
    QCOMPARE(backend->loadCalls, 2);
}

void MediaPlaybackUiTest::relocatesMissingSingleAndReloadsIt()
{
    QTemporaryDir temporary;
    const QString replacementDirectory = temporary.filePath(QStringLiteral("replacement"));
    QVERIFY(QDir().mkpath(replacementDirectory));
    const QString replacement = writeWav(replacementDirectory, QStringLiteral("missing.wav"));
    const QString missing = temporary.filePath(QStringLiteral("missing.wav"));
    const QString databasePath = temporary.filePath(QStringLiteral("db.sqlite3"));
    const auto catalog = mnce::TargetLanguageCatalog::builtIn();
    {
        mnce::MediaRepository repository(catalog);
        QString error;
        QVERIFY(repository.open(databasePath, &error));
        auto item = singleItem(replacement);
        item.currentPath = missing;
        item.fileState = mnce::FileState::Missing;
        QVERIFY(repository.insert(item).status == mnce::InsertMediaStatus::Inserted);
    }
    auto dialogs = std::make_shared<FakeDialogs>();
    dialogs->audioFiles << replacement;
    auto backendOwner = std::make_unique<FakePlaybackBackend>();
    auto* backend = backendOwner.get();
    mnce::MainWindow window(catalog, dialogs, std::move(backendOwner));
    QString error;
    QVERIFY(window.initialize(databasePath, temporary.filePath(QStringLiteral("settings.ini")), &error));
    window.show();
    waitForInitialRefresh(window);
    clickName(window.findChild<QTableWidget*>(QStringLiteral("mediaTable")), 0);
    QCOMPARE(backend->loadCalls, 0);
    auto* relocate = window.findChild<QPushButton*>(QStringLiteral("detailRelocateButton"));
    QVERIFY(relocate->isVisible());
    QTest::mouseClick(relocate, Qt::LeftButton);
    QTRY_COMPARE(backend->loadCalls, 1);
    QCOMPARE(QFileInfo(backend->loadedPath).absoluteFilePath(), QFileInfo(replacement).absoluteFilePath());
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("mediaDetailFileState"))->text(),
             QStringLiteral("可用"));
}

QTEST_MAIN(MediaPlaybackUiTest)

#include "media-playback-ui-test.moc"
