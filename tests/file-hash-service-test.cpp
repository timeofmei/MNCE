#include "services/file-hash-service.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>

class FileHashServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void hashesKnownContent();
    void hashesEmptyFile();
    void readsInChunks();
    void reportsMissingFile();
    void detectsFileChanges();
    void supportsStopRequests();
};

static QString writeFile(QTemporaryDir& directory, const QString& name, const QByteArray& data)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        return {};
    }
    file.close();
    return path;
}

void FileHashServiceTest::hashesKnownContent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("known.mp3"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());

    const mnce::FileHashResult result = mnce::FileHashService{}.hashFile(path);
    QVERIFY2(result.succeeded(), qPrintable(result.message));
    QCOMPARE(result.fileSize, 3);
    QCOMPARE(result.hash.toHex(),
             QByteArrayLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

void FileHashServiceTest::hashesEmptyFile()
{
    QTemporaryDir directory;
    const QString path = writeFile(directory, QStringLiteral("empty.wav"), {});
    QVERIFY(!path.isEmpty());
    const auto result = mnce::FileHashService{}.hashFile(path);
    QVERIFY(result.succeeded());
    QCOMPARE(result.fileSize, 0);
    QCOMPARE(result.hash.toHex(),
             QByteArrayLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void FileHashServiceTest::readsInChunks()
{
    QTemporaryDir directory;
    const QString path = writeFile(directory, QStringLiteral("chunks.flac"), QByteArrayLiteral("abcdefghij"));
    int callbacks = 0;
    const auto result = mnce::FileHashService(3).hashFile(
        path, nullptr, [&callbacks](qint64) { ++callbacks; });
    QVERIFY(result.succeeded());
    QCOMPARE(callbacks, 4);
}

void FileHashServiceTest::reportsMissingFile()
{
    const auto result = mnce::FileHashService{}.hashFile(QStringLiteral("does-not-exist.mp3"));
    QCOMPARE(result.error, mnce::FileHashError::NotAReadableFile);
    QVERIFY(!result.message.isEmpty());
}

void FileHashServiceTest::detectsFileChanges()
{
    QTemporaryDir directory;
    const QString path = writeFile(directory, QStringLiteral("changing.ogg"), QByteArrayLiteral("abcdefghij"));
    bool changed = false;
    const auto result = mnce::FileHashService(2).hashFile(path, nullptr, [&](qint64) {
        if (!changed) {
            changed = true;
            QFile file(path);
            QVERIFY(file.open(QIODevice::Append));
            QCOMPARE(file.write("x"), 1);
        }
    });
    QCOMPARE(result.error, mnce::FileHashError::FileChanged);
}

void FileHashServiceTest::supportsStopRequests()
{
    QTemporaryDir directory;
    const QString path = writeFile(directory, QStringLiteral("cancel.aac"), QByteArrayLiteral("abc"));
    std::atomic_bool stop = true;
    const auto result = mnce::FileHashService{}.hashFile(path, &stop);
    QCOMPARE(result.error, mnce::FileHashError::Cancelled);
}

QTEST_APPLESS_MAIN(FileHashServiceTest)

#include "file-hash-service-test.moc"
