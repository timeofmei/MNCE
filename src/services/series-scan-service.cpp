#include "services/series-scan-service.h"

#include "domain/natural-sort.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace mnce {
namespace {

int compareSnapshots(const SeriesFileSnapshot& left, const SeriesFileSnapshot& right)
{
    const int natural = compareNatural(left.displayName, right.displayName);
    if (natural != 0) {
        return natural;
    }
    const int foldedPath = QString::compare(left.absolutePath.toCaseFolded(),
                                            right.absolutePath.toCaseFolded(),
                                            Qt::CaseSensitive);
    if (foldedPath != 0) {
        return foldedPath;
    }
    return QString::compare(left.absolutePath, right.absolutePath, Qt::CaseSensitive);
}

} // namespace

SeriesScanService::SeriesScanService(FileHashService hashService)
    : hashService_(std::move(hashService))
{
}

bool SeriesScanService::isSupportedAudioFile(const QString& fileName)
{
    static const QSet<QString> extensions = {
        QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("wav"), QStringLiteral("flac"), QStringLiteral("ogg"),
        QStringLiteral("opus"),
    };
    return extensions.contains(QFileInfo(fileName).suffix().toCaseFolded());
}

void SeriesScanService::sortNaturally(QVector<SeriesFileSnapshot>& files,
                                      SortDirection direction)
{
    std::sort(files.begin(), files.end(), [direction](const auto& left, const auto& right) {
        const int comparison = compareSnapshots(left, right);
        return direction == SortDirection::Ascending ? comparison < 0 : comparison > 0;
    });
}

SeriesScanResult SeriesScanService::scan(
    const QString& directory, const std::atomic_bool* stopRequested) const
{
    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.exists() || !directoryInfo.isDir() || !directoryInfo.isReadable()) {
        return {{}, SeriesScanError::DirectoryUnavailable, directory,
                QStringLiteral("文件夹不存在或不可访问：%1").arg(directory)};
    }

    const QDir source(directoryInfo.absoluteFilePath());
    const auto entries = source.entryInfoList(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                                              QDir::NoSort);
    QVector<SeriesFileSnapshot> candidates;
    for (const auto& entry : entries) {
        if (stopRequested != nullptr && stopRequested->load()) {
            return {{}, SeriesScanError::Cancelled, {}, QStringLiteral("操作已停止")};
        }
        if (entry.isSymLink() || !isSupportedAudioFile(entry.fileName())) {
            continue;
        }
        SeriesFileSnapshot file;
        file.absolutePath = QDir::cleanPath(entry.absoluteFilePath());
        file.displayName = entry.fileName();
        candidates.push_back(std::move(file));
    }
    sortNaturally(candidates);

    QVector<SeriesFileSnapshot> unique;
    QHash<QByteArray, qint64> sizesByHash;
    for (auto& candidate : candidates) {
        const auto hash = hashService_.hashFile(candidate.absolutePath, stopRequested);
        if (!hash.succeeded()) {
            const auto error = hash.error == FileHashError::Cancelled
                ? SeriesScanError::Cancelled : SeriesScanError::FileFailed;
            return {{}, error, candidate.absolutePath, hash.message};
        }
        candidate.contentHash = hash.hash;
        candidate.fileSize = hash.fileSize;
        const auto existing = sizesByHash.constFind(candidate.contentHash);
        if (existing != sizesByHash.cend()) {
            if (*existing != candidate.fileSize) {
                return {{}, SeriesScanError::HashSizeConflict, candidate.absolutePath,
                        QStringLiteral("相同内容哈希对应了不同文件大小，无法自动合并")};
            }
            continue;
        }
        sizesByHash.insert(candidate.contentHash, candidate.fileSize);
        unique.push_back(std::move(candidate));
    }
    return {std::move(unique), SeriesScanError::None, {}, {}};
}

} // namespace mnce
