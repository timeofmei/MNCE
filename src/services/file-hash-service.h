#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>

namespace mnce {

enum class FileHashError {
    None,
    NotAReadableFile,
    ReadFailed,
    FileChanged,
    Cancelled,
};

struct FileHashResult
{
    QByteArray hash;
    qint64 fileSize = 0;
    FileHashError error = FileHashError::None;
    QString message;

    [[nodiscard]] bool succeeded() const { return error == FileHashError::None; }
};

class FileHashService
{
public:
    explicit FileHashService(qint64 blockSize = 1024 * 1024);

    [[nodiscard]] FileHashResult hashFile(
        const QString& path,
        const std::atomic_bool* stopRequested = nullptr,
        const std::function<void(qint64)>& blockRead = {}) const;

private:
    qint64 blockSize_;
};

} // namespace mnce
