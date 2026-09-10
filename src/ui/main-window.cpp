#include "ui/main-window.h"

#include "domain/media-item.h"
#include "persistence/media-repository.h"
#include "services/file-hash-service.h"
#include "services/media-import-service.h"
#include "services/media-refresh-service.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <atomic>
#include <utility>

namespace mnce {
namespace {

constexpr auto currentLanguageSetting = "library/currentTargetLanguage";

QString audioFilter()
{
    return QStringLiteral("音频文件 (*.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus)");
}

bool hasSupportedExtension(const QString& path)
{
    static const QStringList extensions = {
        QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("wav"), QStringLiteral("flac"), QStringLiteral("ogg"),
        QStringLiteral("opus"),
    };
    return extensions.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

QString hashFailureMessage(const FileHashResult& result)
{
    return result.message.isEmpty() ? QStringLiteral("无法读取所选文件") : result.message;
}

} // namespace

struct MainWindow::Impl
{
    enum class TaskKind { None, Import, Refresh, Relocate };

    struct TaskResult
    {
        TaskKind kind = TaskKind::None;
        QString languageId;
        QString path;
        MediaItem original;
        MediaItem preparedMedia;
        FileHashResult hash;
        QVector<MediaRefreshInspection> refreshResults;
    };

    explicit Impl(MainWindow* owner, TargetLanguageCatalog languageCatalog)
        : window(owner)
        , catalog(std::move(languageCatalog))
        , repository(catalog)
    {
        buildUi();
        QObject::connect(&watcher, &QFutureWatcher<TaskResult>::finished, window,
                         [this] { finishTask(); });
    }

    void buildUi()
    {
        window->setWindowTitle(QStringLiteral("MNCE 媒体库"));
        window->resize(960, 640);

        auto* central = new QWidget(window);
        auto* root = new QVBoxLayout(central);
        auto* toolbar = new QHBoxLayout;
        toolbar->addWidget(new QLabel(QStringLiteral("正在学习："), central));

        languageCombo = new QComboBox(central);
        languageCombo->setObjectName(QStringLiteral("targetLanguageCombo"));
        for (const auto& language : catalog.languages()) {
            languageCombo->addItem(language.displayName, language.id);
        }
        toolbar->addWidget(languageCombo);
        toolbar->addStretch();

        addButton = new QPushButton(QStringLiteral("添加文件"), central);
        addButton->setObjectName(QStringLiteral("addFileButton"));
        refreshButton = new QPushButton(QStringLiteral("刷新"), central);
        refreshButton->setObjectName(QStringLiteral("refreshButton"));
        toolbar->addWidget(addButton);
        toolbar->addWidget(refreshButton);
        root->addLayout(toolbar);

        table = new QTableWidget(central);
        table->setObjectName(QStringLiteral("mediaTable"));
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("目标语言"),
                                          QStringLiteral("文件状态"), QStringLiteral("识别状态"),
                                          QStringLiteral("操作")});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        table->setColumnWidth(4, 220);
        table->verticalHeader()->setDefaultSectionSize(44);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setFocusPolicy(Qt::NoFocus);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        root->addWidget(table);
        window->setCentralWidget(central);

        QObject::connect(languageCombo, &QComboBox::currentIndexChanged, window,
                         [this](int) {
                             if (settings) {
                                 settings->setValue(QString::fromLatin1(currentLanguageSetting),
                                                    currentLanguageId());
                                 settings->sync();
                             }
                             reloadTable();
                         });
        QObject::connect(addButton, &QPushButton::clicked, window,
                         [this] { chooseImportFile(); });
        QObject::connect(refreshButton, &QPushButton::clicked, window,
                         [this] { startRefresh(false); });
    }

    QString currentLanguageId() const
    {
        return languageCombo->currentData().toString();
    }

    QString languageName(const QString& id) const
    {
        for (const auto& language : catalog.languages()) {
            if (language.id == id) {
                return language.displayName;
            }
        }
        return id;
    }

    void reloadTable(qint64 selectedId = 0)
    {
        if (!repository.isOpen()) {
            return;
        }
        QString error;
        const auto items = repository.itemsForLanguage(currentLanguageId(), &error);
        if (!error.isEmpty()) {
            QMessageBox::critical(window, QStringLiteral("读取失败"), error);
            return;
        }
        table->setRowCount(0);
        for (const auto& item : items) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto* name = new QTableWidgetItem(item.displayName);
            name->setData(Qt::UserRole, item.id);
            name->setToolTip(item.currentPath);
            table->setItem(row, 0, name);
            table->setItem(row, 1, new QTableWidgetItem(languageName(item.targetLanguageId)));
            table->setItem(row, 2, new QTableWidgetItem(
                item.fileState == FileState::Available ? QStringLiteral("可用")
                                                       : QStringLiteral("文件缺失")));
            table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("未识别")));

            auto* actions = new QWidget(table);
            actions->setMinimumWidth(210);
            auto* actionLayout = new QHBoxLayout(actions);
            actionLayout->setContentsMargins(4, 2, 4, 2);
            if (item.fileState == FileState::Missing) {
                auto* relocate = new QPushButton(QStringLiteral("重新定位"), actions);
                actionLayout->addWidget(relocate);
                QObject::connect(relocate, &QPushButton::clicked, window,
                                 [this, item] { chooseRelocation(item); });
            }
            auto* remove = new QPushButton(QStringLiteral("删除记录"), actions);
            actionLayout->addWidget(remove);
            QObject::connect(remove, &QPushButton::clicked, window,
                             [this, id = item.id] { removeItem(id); });
            table->setCellWidget(row, 4, actions);
            if (item.id == selectedId) {
                table->scrollToItem(name);
            }
        }
    }

    void chooseImportFile()
    {
        const QString path = QFileDialog::getOpenFileName(window, QStringLiteral("添加音频文件"),
                                                          {}, audioFilter());
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable() || !hasSupportedExtension(path)) {
            QMessageBox::warning(window, QStringLiteral("无法添加"),
                                 QStringLiteral("请选择可读的受支持音频文件。"));
            return;
        }
        startFileTask(TaskKind::Import, path, currentLanguageId(), {});
    }

    void chooseRelocation(const MediaItem& item)
    {
        const QString path = QFileDialog::getOpenFileName(window, QStringLiteral("重新定位音频文件"),
                                                          {}, audioFilter());
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable() || info.size() != item.fileSize) {
            QMessageBox::warning(window, QStringLiteral("重新定位失败"),
                                 QStringLiteral("所选文件不是原记录对应的同一内容。"));
            return;
        }
        startFileTask(TaskKind::Relocate, path, item.targetLanguageId, item);
    }

    void startFileTask(TaskKind kind, const QString& path, const QString& languageId,
                       const MediaItem& original)
    {
        if (watcher.isRunning()) {
            return;
        }
        stopRequested.store(false);
        setTaskControls(false);
        const FileHashService service = hashService;
        const MediaImportService importer = importService;
        watcher.setFuture(QtConcurrent::run([service, importer, path, languageId, kind, original,
                                             stop = &stopRequested] {
            TaskResult result;
            result.kind = kind;
            result.path = path;
            result.languageId = languageId;
            result.original = original;
            if (kind == TaskKind::Import) {
                const auto prepared = importer.prepare({path, languageId}, stop);
                result.hash = prepared.hashResult;
                result.preparedMedia = prepared.media;
            } else {
                result.hash = service.hashFile(path, stop);
            }
            return result;
        }));
    }

    void startRefresh(bool allLanguages)
    {
        if (watcher.isRunning()) {
            return;
        }
        QString error;
        const auto items = allLanguages ? repository.allItems(&error)
                                        : repository.itemsForLanguage(currentLanguageId(), &error);
        if (!error.isEmpty()) {
            QMessageBox::critical(window, QStringLiteral("刷新失败"), error);
            return;
        }
        stopRequested.store(false);
        setTaskControls(false);
        const MediaRefreshService service = refreshService;
        watcher.setFuture(QtConcurrent::run([service, items, stop = &stopRequested] {
            TaskResult result;
            result.kind = TaskKind::Refresh;
            result.refreshResults = service.inspect(items, stop);
            return result;
        }));
    }

    void setTaskControls(bool enabled)
    {
        addButton->setEnabled(enabled);
        refreshButton->setEnabled(enabled);
    }

    void finishTask()
    {
        setTaskControls(true);
        if (closing) {
            return;
        }
        const TaskResult result = watcher.result();
        switch (result.kind) {
        case TaskKind::Import:
            finishImport(result);
            break;
        case TaskKind::Relocate:
            finishRelocation(result);
            break;
        case TaskKind::Refresh:
            finishRefresh(result);
            break;
        case TaskKind::None:
            break;
        }
    }

    void finishImport(const TaskResult& task)
    {
        if (!task.hash.succeeded()) {
            QMessageBox::warning(window, QStringLiteral("添加失败"), hashFailureMessage(task.hash));
            return;
        }
        const auto inserted = repository.insert(task.preparedMedia);
        if (inserted.status == InsertMediaStatus::Inserted) {
            if (currentLanguageId() == task.languageId) {
                reloadTable(inserted.mediaId);
                QMessageBox::information(window, QStringLiteral("添加完成"),
                                         QStringLiteral("文件已加入媒体库。"));
            } else {
                QMessageBox::information(
                    window, QStringLiteral("添加完成"),
                    QStringLiteral("文件已加入“%1”内容空间。").arg(languageName(task.languageId)));
            }
        } else if (inserted.status == InsertMediaStatus::Duplicate) {
            if (currentLanguageId() == task.languageId) {
                reloadTable(inserted.mediaId);
            }
            QMessageBox::information(window, QStringLiteral("内容已存在"),
                                     QStringLiteral("该内容已在当前目标语言的媒体库中。"));
        } else {
            QMessageBox::critical(window, QStringLiteral("添加失败"), inserted.error);
        }
    }

    void finishRelocation(const TaskResult& task)
    {
        if (!MediaRefreshService::matchesOriginal(task.original, task.hash)) {
            QMessageBox::warning(window, QStringLiteral("重新定位失败"),
                                 QStringLiteral("所选文件不是原记录对应的同一内容。"));
            return;
        }
        QString error;
        const QFileInfo info(task.path);
        if (!repository.relocate(task.original.id, info.absoluteFilePath(), info.fileName(), &error)) {
            QMessageBox::critical(window, QStringLiteral("重新定位失败"), error);
            return;
        }
        reloadTable(task.original.id);
        QMessageBox::information(window, QStringLiteral("重新定位完成"),
                                 QStringLiteral("媒体文件路径已更新。"));
    }

    void finishRefresh(const TaskResult& task)
    {
        QString error;
        if (!refreshService.apply(repository, task.refreshResults, &error)) {
            QMessageBox::critical(window, QStringLiteral("刷新失败"), error);
        }
        reloadTable();
    }

    void removeItem(qint64 id)
    {
        QString error;
        if (!repository.remove(id, &error)) {
            QMessageBox::critical(window, QStringLiteral("删除失败"), error);
            return;
        }
        reloadTable();
    }

    void stopAndWait()
    {
        closing = true;
        stopRequested.store(true);
        if (watcher.isRunning()) {
            watcher.waitForFinished();
        }
    }

    MainWindow* window;
    TargetLanguageCatalog catalog;
    MediaRepository repository;
    FileHashService hashService;
    MediaImportService importService;
    MediaRefreshService refreshService;
    std::unique_ptr<QSettings> settings;
    QComboBox* languageCombo = nullptr;
    QPushButton* addButton = nullptr;
    QPushButton* refreshButton = nullptr;
    QTableWidget* table = nullptr;
    QFutureWatcher<TaskResult> watcher;
    std::atomic_bool stopRequested = false;
    bool closing = false;
};

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(TargetLanguageCatalog::builtIn(), parent)
{
}

MainWindow::MainWindow(TargetLanguageCatalog catalog, QWidget* parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>(this, std::move(catalog)))
{
}

MainWindow::~MainWindow()
{
    impl_->stopAndWait();
}

bool MainWindow::initialize(const QString& databasePath,
                            const QString& settingsFilePath,
                            QString* error)
{
    if (!impl_->repository.open(databasePath, error)) {
        return false;
    }
    if (settingsFilePath.isEmpty()) {
        impl_->settings = std::make_unique<QSettings>();
    } else {
        impl_->settings = std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat);
    }
    QString languageId = impl_->settings->value(
        QString::fromLatin1(currentLanguageSetting), impl_->catalog.defaultLanguageId()).toString();
    if (!impl_->catalog.contains(languageId)) {
        languageId = impl_->catalog.defaultLanguageId();
    }
    const int index = impl_->languageCombo->findData(languageId);
    impl_->languageCombo->setCurrentIndex(index);
    impl_->reloadTable();
    impl_->startRefresh(true);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    impl_->stopAndWait();
    QMainWindow::closeEvent(event);
}

} // namespace mnce
