#include "services/file-hash-service.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace mnce {

FileHashService::FileHashService(qint64 blockSize)
    : blockSize_(blockSize)
{
}

FileHashResult FileHashService::hashFile(
    const QString& path,
    const std::atomic_bool* stopRequested,
    const std::function<void(qint64)>& blockRead) const
{
    const QFileInfo before(path);
    if (!before.exists() || !before.isFile() || !before.isReadable()) {
        return {{}, 0, FileHashError::NotAReadableFile,
                QStringLiteral("文件不存在、不可读或不是普通文件：%1").arg(path)};
    }

    const qint64 initialSize = before.size();
    const QDateTime initialModified = before.lastModified();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, 0, FileHashError::NotAReadableFile,
                QStringLiteral("无法打开文件：%1").arg(file.errorString())};
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    QByteArray buffer(blockSize_, Qt::Uninitialized);
    qint64 totalRead = 0;
    while (true) {
        if (stopRequested != nullptr && stopRequested->load()) {
            return {{}, 0, FileHashError::Cancelled, QStringLiteral("操作已停止")};
        }
        const qint64 bytesRead = file.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            return {{}, 0, FileHashError::ReadFailed,
                    QStringLiteral("读取文件失败：%1").arg(file.errorString())};
        }
        if (bytesRead == 0) {
            break;
        }
        hasher.addData(QByteArrayView(buffer.constData(), bytesRead));
        totalRead += bytesRead;
        if (blockRead) {
            blockRead(totalRead);
        }
    }
    file.close();

    const QFileInfo after(path);
    if (!after.exists() || !after.isFile() || initialSize != after.size()
        || initialModified != after.lastModified() || totalRead != initialSize) {
        return {{}, 0, FileHashError::FileChanged,
                QStringLiteral("文件在处理期间发生了变化，请重试")};
    }

    return {hasher.result(), initialSize, FileHashError::None, {}};
}

} // namespace mnce
