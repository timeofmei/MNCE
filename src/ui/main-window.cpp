#include "ui/main-window.h"

#include "domain/media-item.h"
#include "domain/media-series.h"
#include "domain/natural-sort.h"
#include "persistence/media-repository.h"
#include "persistence/series-repository.h"
#include "services/file-hash-service.h"
#include "services/media-import-service.h"
#include "services/media-refresh-service.h"
#include "services/player-controller.h"
#include "services/qt-playback-backend.h"
#include "services/series-service.h"
#include "ui/frameless-window-controller.h"
#include "ui/media-detail-widget.h"
#include "ui/window-title-bar.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <atomic>
#include <utility>

namespace mnce {
namespace {

constexpr auto currentLanguageSetting = "library/currentTargetLanguage";
constexpr auto singleSortFieldSetting = "library/singleFiles/sortField";
constexpr auto singleSortDirectionSetting = "library/singleFiles/sortDirection";
constexpr auto seriesSortFieldSetting = "library/series/sortField";
constexpr auto seriesSortDirectionSetting = "library/series/sortDirection";
constexpr auto seriesMediaSortDirectionSetting = "library/seriesMedia/sortDirection";
constexpr auto playbackRateSetting = "playback/rate";

QString audioFilter()
{
    return QStringLiteral("音频文件 (*.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus)");
}

bool hasSupportedExtension(const QString& path)
{
    return SeriesScanService::isSupportedAudioFile(path);
}

std::unique_ptr<PlaybackBackend> ensurePlaybackBackend(
    std::unique_ptr<PlaybackBackend> backend)
{
    return backend ? std::move(backend) : std::make_unique<QtPlaybackBackend>();
}

QString hashFailureMessage(const FileHashResult& result)
{
    return result.message.isEmpty() ? QStringLiteral("无法读取所选文件") : result.message;
}

QString dateText(const QDateTime& dateTime)
{
    return dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

int compareNames(const QString& left, const QString& right)
{
    const int natural = compareNatural(left, right);
    return natural != 0 ? natural : QString::compare(left, right, Qt::CaseSensitive);
}

template<typename Value, typename Compare>
void sortValues(QVector<Value>& values, SortDirection direction, Compare compare)
{
    std::sort(values.begin(), values.end(), [direction, compare](const Value& left,
                                                                 const Value& right) {
        const int result = compare(left, right);
        return direction == SortDirection::Ascending ? result < 0 : result > 0;
    });
}

int compareIds(qint64 left, qint64 right)
{
    return left == right ? 0 : (left < right ? -1 : 1);
}

class NativeLibraryUiDialogs final : public LibraryUiDialogs
{
public:
    QString chooseAudioFile(QWidget* parent, bool relocating) override
    {
        return QFileDialog::getOpenFileName(
            parent, relocating ? QStringLiteral("重新定位音频文件")
                               : QStringLiteral("添加音频文件"),
            {}, audioFilter());
    }

    QString chooseSeriesDirectory(QWidget* parent, bool relocating) override
    {
        return QFileDialog::getExistingDirectory(
            parent, relocating ? QStringLiteral("重新定位系列文件夹")
                               : QStringLiteral("添加文件夹系列"));
    }

    std::optional<QString> requestSeriesName(QWidget* parent) override
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(parent, QStringLiteral("创建系列"),
                                                   QStringLiteral("系列名称："),
                                                   QLineEdit::Normal, {}, &accepted);
        if (!accepted) {
            return std::nullopt;
        }
        return name;
    }

    bool confirmSeriesDeletion(QWidget* parent, const QString& seriesName,
                               int itemCount) override
    {
        return QMessageBox::warning(
                   parent, QStringLiteral("删除系列"),
                   QStringLiteral("将永久删除系列“%1”及其 %2 条应用内部数据。\n"
                                  "不会删除、移动或修改原始音频，也不会影响其他媒体库或系列。")
                       .arg(seriesName).arg(itemCount),
                   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            == QMessageBox::Yes;
    }

    void showMessage(QWidget* parent, UserMessageKind kind,
                     const QString& title, const QString& text) override
    {
        switch (kind) {
        case UserMessageKind::Information:
            QMessageBox::information(parent, title, text);
            break;
        case UserMessageKind::Warning:
            QMessageBox::warning(parent, title, text);
            break;
        case UserMessageKind::Critical:
            QMessageBox::critical(parent, title, text);
            break;
        }
    }
};

} // namespace

struct MainWindow::Impl
{
    enum class TaskKind { None, Import, SingleRefresh, SingleRelocate,
                          SeriesCreate, SeriesRefresh, SeriesRelocate };

    struct TaskResult
    {
        TaskKind kind = TaskKind::None;
        QString languageId;
        QString path;
        MediaItem original;
        MediaItem preparedMedia;
        FileHashResult hash;
        QVector<MediaRefreshInspection> refreshResults;
        PreparedSeriesOperation seriesOperation;
    };

    explicit Impl(MainWindow* owner, TargetLanguageCatalog languageCatalog,
                  std::shared_ptr<LibraryUiDialogs> uiDialogs,
                  std::unique_ptr<PlaybackBackend> audioBackend)
        : window(owner)
        , catalog(std::move(languageCatalog))
        , repository(catalog)
        , seriesRepository(catalog)
        , dialogs(std::move(uiDialogs))
        , playbackBackend(ensurePlaybackBackend(std::move(audioBackend)))
        , playerController(std::make_unique<PlayerController>(*playbackBackend))
    {
        buildUi();
        QObject::connect(&watcher, &QFutureWatcher<TaskResult>::finished, window,
                         [this] { finishTask(); });
        QObject::connect(playerController.get(), &PlayerController::snapshotChanged, window,
                         [this] { detailWidget->updatePlayback(*playerController); });
        QObject::connect(playerController.get(), &PlayerController::durationDiscovered, window,
                         [this](const MediaSourceReference& source, qint64 duration) {
                             persistDuration(source, duration);
                         });
    }

    void buildUi()
    {
        window->setWindowTitle(QStringLiteral("MNCE"));
        window->resize(1040, 720);

        auto* central = new QWidget(window);
        QWidget* content = central;
        if (FramelessWindowController::enabledForCurrentPlatform()) {
            auto* frameLayout = new QVBoxLayout(central);
            frameLayout->setContentsMargins(0, 0, 0, 0);
            frameLayout->setSpacing(0);
            titleBar = new WindowTitleBar(central);
            frameLayout->addWidget(titleBar);
            content = new QWidget(central);
            frameLayout->addWidget(content);
        }

        auto* root = new QVBoxLayout(content);
        auto* toolbar = new QHBoxLayout;
        toolbar->addWidget(new QLabel(QStringLiteral("正在学习："), content));
        languageCombo = new QComboBox(content);
        languageCombo->setObjectName(QStringLiteral("targetLanguageCombo"));
        for (const auto& language : catalog.languages()) {
            languageCombo->addItem(language.displayName, language.id);
        }
        toolbar->addWidget(languageCombo);
        toolbar->addStretch();
        root->addLayout(toolbar);

        pages = new QStackedWidget(content);
        pages->setObjectName(QStringLiteral("mainPages"));
        libraryPage = new QWidget(pages);
        auto* libraryLayout = new QVBoxLayout(libraryPage);
        libraryLayout->setContentsMargins(0, 0, 0, 0);
        tabs = new QTabWidget(libraryPage);
        tabs->setObjectName(QStringLiteral("libraryTabs"));
        buildSingleFilesTab();
        buildSeriesTab();
        libraryLayout->addWidget(tabs);
        pages->addWidget(libraryPage);
        detailWidget = new MediaDetailWidget(pages);
        pages->addWidget(detailWidget);
        root->addWidget(pages);
        window->setCentralWidget(central);
        if (titleBar != nullptr) {
            frameController = new FramelessWindowController(window, titleBar, window);
        }

        QObject::connect(detailWidget, &MediaDetailWidget::backRequested, window,
                         [this] { leaveMediaDetail(); });
        QObject::connect(detailWidget, &MediaDetailWidget::playRequested, window,
                         [this] { (void) playerController->play(); });
        QObject::connect(detailWidget, &MediaDetailWidget::pauseRequested, window,
                         [this] { (void) playerController->pause(); });
        QObject::connect(detailWidget, &MediaDetailWidget::seekRequested, window,
                         [this](qint64 position) { (void) playerController->seek(position); });
        QObject::connect(detailWidget, &MediaDetailWidget::playbackRateChanged, window,
                         [this](qreal rate) {
                             if (playerController->setPlaybackRate(rate)) {
                                 saveSetting(playbackRateSetting, rate);
                             }
                         });
        QObject::connect(detailWidget, &MediaDetailWidget::relocateRequested, window,
                         [this] { relocateCurrentMedia(); });

        QObject::connect(languageCombo, &QComboBox::currentIndexChanged, window,
                         [this](int) {
                             leaveMediaDetail();
                             saveSetting(currentLanguageSetting, currentLanguageId());
                             selectedSeriesId = 0;
                             reloadAll();
                         });
    }

    void buildSingleFilesTab()
    {
        auto* page = new QWidget(tabs);
        auto* layout = new QVBoxLayout(page);
        auto* controls = new QHBoxLayout;
        addButton = new QPushButton(QStringLiteral("添加文件"), page);
        addButton->setObjectName(QStringLiteral("addFileButton"));
        singleRefreshButton = new QPushButton(QStringLiteral("刷新"), page);
        singleRefreshButton->setObjectName(QStringLiteral("refreshButton"));
        controls->addWidget(addButton);
        controls->addWidget(singleRefreshButton);
        controls->addStretch();
        controls->addWidget(new QLabel(QStringLiteral("排序："), page));
        singleSortFieldCombo = createSortFieldCombo(page, QStringLiteral("singleSortFieldCombo"));
        singleSortDirectionCombo = createDirectionCombo(page,
                                                        QStringLiteral("singleSortDirectionCombo"));
        controls->addWidget(singleSortFieldCombo);
        controls->addWidget(singleSortDirectionCombo);
        layout->addLayout(controls);

        table = new QTableWidget(page);
        table->setObjectName(QStringLiteral("mediaTable"));
        table->setColumnCount(6);
        table->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("时长"),
                                          QStringLiteral("目标语言"), QStringLiteral("文件状态"),
                                          QStringLiteral("识别状态"), QStringLiteral("操作")});
        configureTable(table, 5, 220);
        layout->addWidget(table);
        tabs->addTab(page, QStringLiteral("单文件"));

        QObject::connect(addButton, &QPushButton::clicked, window,
                         [this] { chooseImportFile(); });
        QObject::connect(singleRefreshButton, &QPushButton::clicked, window,
                         [this] { startSingleRefresh(false); });
        QObject::connect(singleSortFieldCombo, &QComboBox::currentIndexChanged, window,
                         [this](int) { saveSortPreferences(); reloadSingleTable(); });
        QObject::connect(singleSortDirectionCombo, &QComboBox::currentIndexChanged, window,
                         [this](int) { saveSortPreferences(); reloadSingleTable(); });
        QObject::connect(table, &QTableWidget::cellClicked, window,
                         [this](int row, int column) {
                             if (column != 0) return;
                             const auto* cell = table->item(row, 0);
                             if (cell == nullptr) return;
                             openSingleMedia(cell->data(Qt::UserRole).toLongLong());
                         });
    }

    void buildSeriesTab()
    {
        auto* page = new QWidget(tabs);
        auto* layout = new QVBoxLayout(page);
        auto* controls = new QHBoxLayout;
        addFolderButton = new QPushButton(QStringLiteral("添加文件夹"), page);
        addFolderButton->setObjectName(QStringLiteral("addFolderButton"));
        controls->addWidget(addFolderButton);
        controls->addStretch();
        controls->addWidget(new QLabel(QStringLiteral("排序："), page));
        seriesSortFieldCombo = createSortFieldCombo(page, QStringLiteral("seriesSortFieldCombo"));
        seriesSortDirectionCombo = createDirectionCombo(page,
                                                        QStringLiteral("seriesSortDirectionCombo"));
        controls->addWidget(seriesSortFieldCombo);
        controls->addWidget(seriesSortDirectionCombo);
        layout->addLayout(controls);

        seriesTable = new QTableWidget(page);
        seriesTable->setObjectName(QStringLiteral("seriesTable"));
        seriesTable->setColumnCount(5);
        seriesTable->setHorizontalHeaderLabels({QStringLiteral("系列名称"), QStringLiteral("文件夹状态"),
                                                QStringLiteral("条目数量"), QStringLiteral("添加时间"),
                                                QStringLiteral("操作")});
        configureTable(seriesTable, 4, 100);
        layout->addWidget(seriesTable, 2);

        seriesDetail = new QGroupBox(QStringLiteral("系列详情"), page);
        seriesDetail->setObjectName(QStringLiteral("seriesDetail"));
        auto* detailLayout = new QVBoxLayout(seriesDetail);
        auto* detailToolbar = new QHBoxLayout;
        seriesDetailName = new QLabel(seriesDetail);
        seriesDetailName->setObjectName(QStringLiteral("seriesDetailName"));
        detailToolbar->addWidget(seriesDetailName);
        detailToolbar->addStretch();
        seriesRefreshButton = new QPushButton(QStringLiteral("刷新系列"), seriesDetail);
        seriesRefreshButton->setObjectName(QStringLiteral("seriesRefreshButton"));
        seriesRelocateButton = new QPushButton(QStringLiteral("重新定位"), seriesDetail);
        seriesRelocateButton->setObjectName(QStringLiteral("seriesRelocateButton"));
        detailToolbar->addWidget(seriesRefreshButton);
        detailToolbar->addWidget(seriesRelocateButton);
        detailToolbar->addWidget(new QLabel(QStringLiteral("文件名排序："), seriesDetail));
        seriesMediaSortDirectionCombo = createDirectionCombo(
            seriesDetail, QStringLiteral("seriesMediaSortDirectionCombo"));
        detailToolbar->addWidget(seriesMediaSortDirectionCombo);
        detailLayout->addLayout(detailToolbar);
        seriesDirectoryLabel = new QLabel(seriesDetail);
        seriesDirectoryLabel->setObjectName(QStringLiteral("seriesDirectoryLabel"));
        seriesDirectoryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        detailLayout->addWidget(seriesDirectoryLabel);
        seriesMediaTable = new QTableWidget(seriesDetail);
        seriesMediaTable->setObjectName(QStringLiteral("seriesMediaTable"));
        seriesMediaTable->setColumnCount(4);
        seriesMediaTable->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("时长"),
                                                     QStringLiteral("文件状态"),
                                                     QStringLiteral("识别状态")});
        configureTable(seriesMediaTable);
        detailLayout->addWidget(seriesMediaTable);
        seriesDetail->hide();
        layout->addWidget(seriesDetail, 3);
        tabs->addTab(page, QStringLiteral("文件夹系列"));

        QObject::connect(addFolderButton, &QPushButton::clicked, window,
                         [this] { chooseSeriesCreation(); });
        QObject::connect(seriesRefreshButton, &QPushButton::clicked, window,
                         [this] { startSelectedSeriesRefresh(); });
        QObject::connect(seriesRelocateButton, &QPushButton::clicked, window,
                         [this] { chooseSeriesRelocation(); });
        QObject::connect(seriesSortFieldCombo, &QComboBox::currentIndexChanged, window,
                         [this](int) { saveSortPreferences(); reloadSeriesTable(); });
        QObject::connect(seriesSortDirectionCombo, &QComboBox::currentIndexChanged, window,
                         [this](int) { saveSortPreferences(); reloadSeriesTable(); });
        QObject::connect(seriesMediaSortDirectionCombo, &QComboBox::currentIndexChanged, window,
                         [this](int) { saveSortPreferences(); reloadSeriesDetail(); });
        QObject::connect(seriesTable, &QTableWidget::cellClicked, window,
                         [this](int row, int column) {
                             if (column != 0) {
                                 return;
                             }
                             const auto* item = seriesTable->item(row, 0);
                             if (item != nullptr) {
                                 openSeries(item->data(Qt::UserRole).toLongLong());
                             }
                         });
        QObject::connect(seriesMediaTable, &QTableWidget::cellClicked, window,
                         [this](int row, int column) {
                             if (column != 0) return;
                             const auto* cell = seriesMediaTable->item(row, 0);
                             if (cell == nullptr) return;
                             openSeriesMedia(cell->data(Qt::UserRole).toLongLong());
                         });
    }

    static QComboBox* createSortFieldCombo(QWidget* parent, const QString& objectName)
    {
        auto* combo = new QComboBox(parent);
        combo->setObjectName(objectName);
        combo->addItem(QStringLiteral("名称"), QStringLiteral("name"));
        combo->addItem(QStringLiteral("添加时间"), QStringLiteral("created_at"));
        return combo;
    }

    static QComboBox* createDirectionCombo(QWidget* parent, const QString& objectName)
    {
        auto* combo = new QComboBox(parent);
        combo->setObjectName(objectName);
        combo->addItem(QStringLiteral("正序"), QStringLiteral("ascending"));
        combo->addItem(QStringLiteral("倒序"), QStringLiteral("descending"));
        return combo;
    }

    static void configureTable(QTableWidget* target, int fixedColumn = -1,
                               int fixedWidth = 0)
    {
        target->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        if (fixedColumn >= 0) {
            target->horizontalHeader()->setSectionResizeMode(fixedColumn, QHeaderView::Fixed);
            target->setColumnWidth(fixedColumn, fixedWidth);
        }
        target->verticalHeader()->setDefaultSectionSize(44);
        target->setSelectionMode(QAbstractItemView::NoSelection);
        target->setFocusPolicy(Qt::NoFocus);
        target->setEditTriggers(QAbstractItemView::NoEditTriggers);
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

    LibrarySortField sortField(const QComboBox* combo) const
    {
        return combo->currentData().toString() == QStringLiteral("name")
            ? LibrarySortField::Name : LibrarySortField::CreatedAt;
    }

    SortDirection direction(const QComboBox* combo) const
    {
        return combo->currentData().toString() == QStringLiteral("ascending")
            ? SortDirection::Ascending : SortDirection::Descending;
    }

    void saveSetting(const char* key, const QVariant& value)
    {
        if (settings) {
            settings->setValue(QString::fromLatin1(key), value);
            settings->sync();
        }
    }

    void saveSortPreferences()
    {
        if (!settings) {
            return;
        }
        saveSetting(singleSortFieldSetting, singleSortFieldCombo->currentData());
        saveSetting(singleSortDirectionSetting, singleSortDirectionCombo->currentData());
        saveSetting(seriesSortFieldSetting, seriesSortFieldCombo->currentData());
        saveSetting(seriesSortDirectionSetting, seriesSortDirectionCombo->currentData());
        saveSetting(seriesMediaSortDirectionSetting, seriesMediaSortDirectionCombo->currentData());
    }

    void restoreCombo(QComboBox* combo, const char* key, const QString& fallback)
    {
        const QString value = settings->value(QString::fromLatin1(key), fallback).toString();
        int index = combo->findData(value);
        if (index < 0) {
            index = combo->findData(fallback);
        }
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
    }

    void restoreSortPreferences()
    {
        restoreCombo(singleSortFieldCombo, singleSortFieldSetting, QStringLiteral("created_at"));
        restoreCombo(singleSortDirectionCombo, singleSortDirectionSetting, QStringLiteral("descending"));
        restoreCombo(seriesSortFieldCombo, seriesSortFieldSetting, QStringLiteral("created_at"));
        restoreCombo(seriesSortDirectionCombo, seriesSortDirectionSetting, QStringLiteral("descending"));
        restoreCombo(seriesMediaSortDirectionCombo, seriesMediaSortDirectionSetting,
                     QStringLiteral("ascending"));
    }

    void reloadAll()
    {
        reloadSingleTable();
        reloadSeriesTable();
        reloadSeriesDetail();
    }

    void reloadSingleTable(qint64 selectedId = 0)
    {
        if (!repository.isOpen()) {
            return;
        }
        QString error;
        auto items = repository.itemsForLanguage(currentLanguageId(), &error);
        if (!error.isEmpty()) {
            showMessage(UserMessageKind::Critical, QStringLiteral("读取失败"), error);
            return;
        }
        const auto field = sortField(singleSortFieldCombo);
        sortValues(items, direction(singleSortDirectionCombo), [field](const auto& left,
                                                                      const auto& right) {
            if (field == LibrarySortField::Name) {
                const int name = compareNames(left.displayName, right.displayName);
                return name != 0 ? name : compareIds(left.id, right.id);
            }
            if (left.createdAt != right.createdAt) {
                return left.createdAt < right.createdAt ? -1 : 1;
            }
            return compareIds(left.id, right.id);
        });

        table->setRowCount(0);
        for (const auto& item : items) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto* name = new QTableWidgetItem(item.displayName);
            name->setData(Qt::UserRole, item.id);
            name->setToolTip(item.currentPath);
            name->setForeground(window->palette().link());
            table->setItem(row, 0, name);
            table->setItem(row, 1, new QTableWidgetItem(
                item.durationMs ? formatPlaybackTime(*item.durationMs) : QStringLiteral("—")));
            table->setItem(row, 2, new QTableWidgetItem(languageName(item.targetLanguageId)));
            table->setItem(row, 3, new QTableWidgetItem(
                item.fileState == FileState::Available ? QStringLiteral("可用")
                                                       : QStringLiteral("文件缺失")));
            table->setItem(row, 4, new QTableWidgetItem(QStringLiteral("未识别")));
            auto* actions = new QWidget(table);
            actions->setMinimumWidth(210);
            auto* actionLayout = new QHBoxLayout(actions);
            actionLayout->setContentsMargins(4, 2, 4, 2);
            if (item.fileState == FileState::Missing) {
                auto* relocate = new QPushButton(QStringLiteral("重新定位"), actions);
                actionLayout->addWidget(relocate);
                QObject::connect(relocate, &QPushButton::clicked, window,
                                 [this, item] { chooseSingleRelocation(item); });
            }
            auto* remove = new QPushButton(QStringLiteral("删除记录"), actions);
            actionLayout->addWidget(remove);
            QObject::connect(remove, &QPushButton::clicked, window,
                             [this, id = item.id] { removeItem(id); });
            table->setCellWidget(row, 5, actions);
            if (item.id == selectedId) {
                table->scrollToItem(name);
            }
        }
    }

    void reloadSeriesTable()
    {
        if (!seriesRepository.isOpen()) {
            return;
        }
        QString error;
        auto series = seriesRepository.seriesForLanguage(currentLanguageId(), &error);
        if (!error.isEmpty()) {
            showMessage(UserMessageKind::Critical, QStringLiteral("读取失败"), error);
            return;
        }
        const auto field = sortField(seriesSortFieldCombo);
        sortValues(series, direction(seriesSortDirectionCombo), [field](const auto& left,
                                                                        const auto& right) {
            if (field == LibrarySortField::Name) {
                const int name = compareNames(left.name, right.name);
                return name != 0 ? name : compareIds(left.id, right.id);
            }
            if (left.createdAt != right.createdAt) {
                return left.createdAt < right.createdAt ? -1 : 1;
            }
            return compareIds(left.id, right.id);
        });

        seriesTable->setRowCount(0);
        for (const auto& value : series) {
            const auto media = seriesRepository.mediaForSeries(value.id, &error);
            if (!error.isEmpty()) {
                showMessage(UserMessageKind::Critical, QStringLiteral("读取失败"), error);
                return;
            }
            const int row = seriesTable->rowCount();
            seriesTable->insertRow(row);
            auto* name = new QTableWidgetItem(value.name);
            name->setData(Qt::UserRole, value.id);
            name->setToolTip(QStringLiteral("点击进入系列\n%1").arg(value.currentDirectory));
            name->setForeground(window->palette().link());
            seriesTable->setItem(row, 0, name);
            seriesTable->setItem(row, 1, new QTableWidgetItem(
                value.directoryState == DirectoryState::Available ? QStringLiteral("可用")
                                                                  : QStringLiteral("文件夹缺失")));
            seriesTable->setItem(row, 2, new QTableWidgetItem(QString::number(media.size())));
            seriesTable->setItem(row, 3, new QTableWidgetItem(dateText(value.createdAt)));
            auto* actions = new QWidget(seriesTable);
            actions->setMinimumWidth(90);
            auto* actionLayout = new QHBoxLayout(actions);
            actionLayout->setContentsMargins(4, 2, 4, 2);
            auto* remove = new QPushButton(QStringLiteral("删除"), actions);
            remove->setObjectName(QStringLiteral("deleteSeriesButton"));
            actionLayout->addWidget(remove);
            QObject::connect(remove, &QPushButton::clicked, window,
                             [this, value, count = media.size()] {
                                 removeSeries(value, count);
                             });
            seriesTable->setCellWidget(row, 4, actions);
        }
    }

    void reloadSeriesDetail()
    {
        if (selectedSeriesId == 0 || !seriesRepository.isOpen()) {
            seriesDetail->hide();
            return;
        }
        MediaSeries series;
        QString error;
        if (!seriesRepository.find(selectedSeriesId, &series, &error)
            || series.targetLanguageId != currentLanguageId()) {
            selectedSeriesId = 0;
            seriesDetail->hide();
            return;
        }
        auto media = seriesRepository.mediaForSeries(selectedSeriesId, &error);
        if (!error.isEmpty()) {
            showMessage(UserMessageKind::Critical, QStringLiteral("读取失败"), error);
            return;
        }
        sortValues(media, direction(seriesMediaSortDirectionCombo), [](const auto& left,
                                                                       const auto& right) {
            const int name = compareNames(left.displayName, right.displayName);
            return name != 0 ? name : compareIds(left.id, right.id);
        });
        seriesDetail->setTitle(series.directoryState == DirectoryState::Available
                                   ? QStringLiteral("系列详情")
                                   : QStringLiteral("系列详情（文件夹缺失）"));
        seriesDetailName->setText(QStringLiteral("系列：%1").arg(series.name));
        seriesDirectoryLabel->setText(QStringLiteral("文件夹：%1").arg(series.currentDirectory));
        seriesMediaTable->setRowCount(0);
        for (const auto& item : media) {
            const int row = seriesMediaTable->rowCount();
            seriesMediaTable->insertRow(row);
            auto* name = new QTableWidgetItem(item.displayName);
            name->setData(Qt::UserRole, item.id);
            name->setToolTip(item.currentPath);
            name->setForeground(window->palette().link());
            seriesMediaTable->setItem(row, 0, name);
            seriesMediaTable->setItem(row, 1, new QTableWidgetItem(
                item.durationMs ? formatPlaybackTime(*item.durationMs) : QStringLiteral("—")));
            seriesMediaTable->setItem(row, 2, new QTableWidgetItem(
                item.fileState == FileState::Available ? QStringLiteral("可用")
                                                       : QStringLiteral("文件缺失")));
            seriesMediaTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("未识别")));
        }
        seriesDetail->show();
    }

    void chooseImportFile()
    {
        const QString path = dialogs->chooseAudioFile(window, false);
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable() || !hasSupportedExtension(path)) {
            showMessage(UserMessageKind::Warning, QStringLiteral("无法添加"),
                        QStringLiteral("请选择可读的受支持音频文件。"));
            return;
        }
        startFileTask(TaskKind::Import, path, currentLanguageId(), {});
    }

    void chooseSingleRelocation(const MediaItem& item)
    {
        const QString path = dialogs->chooseAudioFile(window, true);
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable() || info.size() != item.fileSize) {
            showMessage(UserMessageKind::Warning, QStringLiteral("重新定位失败"),
                        QStringLiteral("所选文件不是原记录对应的同一内容。"));
            return;
        }
        startFileTask(TaskKind::SingleRelocate, path, item.targetLanguageId, item);
    }

    void chooseSeriesCreation()
    {
        const QString directory = dialogs->chooseSeriesDirectory(window, false);
        if (directory.isEmpty()) {
            return;
        }
        const auto name = dialogs->requestSeriesName(window);
        if (!name.has_value()) {
            return;
        }
        startSeriesTask(TaskKind::SeriesCreate,
                        {*name, currentLanguageId(), directory});
    }

    void chooseSeriesRelocation()
    {
        MediaSeries series;
        QString error;
        if (!seriesRepository.find(selectedSeriesId, &series, &error)) {
            showMessage(UserMessageKind::Critical, QStringLiteral("重新定位失败"), error);
            return;
        }
        const QString directory = dialogs->chooseSeriesDirectory(window, true);
        if (directory.isEmpty()) {
            return;
        }
        startPreparedSeriesTask(TaskKind::SeriesRelocate,
                                [this, series, directory] {
                                    return seriesService.prepareRelocation(
                                        series, directory, &stopRequested);
                                });
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

    void startSingleRefresh(bool allLanguages)
    {
        if (watcher.isRunning()) {
            return;
        }
        QString error;
        const auto items = allLanguages ? repository.allItems(&error)
                                        : repository.itemsForLanguage(currentLanguageId(), &error);
        if (!error.isEmpty()) {
            showMessage(UserMessageKind::Critical, QStringLiteral("刷新失败"), error);
            return;
        }
        stopRequested.store(false);
        setTaskControls(false);
        const MediaRefreshService service = refreshService;
        watcher.setFuture(QtConcurrent::run([service, items, stop = &stopRequested] {
            TaskResult result;
            result.kind = TaskKind::SingleRefresh;
            result.refreshResults = service.inspect(items, stop);
            return result;
        }));
    }

    void startSeriesTask(TaskKind kind, const CreateSeriesRequest& request)
    {
        startPreparedSeriesTask(kind, [this, request] {
            return seriesService.prepareCreate(request, &stopRequested);
        });
    }

    template<typename Prepare>
    void startPreparedSeriesTask(TaskKind kind, Prepare prepare)
    {
        if (watcher.isRunning()) {
            return;
        }
        stopRequested.store(false);
        setTaskControls(false);
        watcher.setFuture(QtConcurrent::run([kind, prepare = std::move(prepare)]() mutable {
            TaskResult result;
            result.kind = kind;
            result.seriesOperation = prepare();
            return result;
        }));
    }

    void openSeries(qint64 seriesId)
    {
        selectedSeriesId = seriesId;
        reloadSeriesDetail();
    }

    void openSingleMedia(qint64 mediaId)
    {
        QString error;
        const auto items = repository.itemsForLanguage(currentLanguageId(), &error);
        if (!error.isEmpty()) {
            showMessage(UserMessageKind::Critical, QStringLiteral("读取失败"), error);
            return;
        }
        const auto found = std::find_if(items.cbegin(), items.cend(), [mediaId](const auto& item) {
            return item.id == mediaId;
        });
        if (found == items.cend()) return;
        detailSingleItem = *found;
        detailSeriesItem.reset();
        showMediaDetail({MediaSourceKind::SingleFile, found->id, found->currentPath,
                         found->fileState == FileState::Available, found->durationMs},
                        found->displayName, found->targetLanguageId,
                        found->fileState == FileState::Available);
    }

    void openSeriesMedia(qint64 mediaId)
    {
        QString error;
        const auto items = seriesRepository.mediaForSeries(selectedSeriesId, &error);
        if (!error.isEmpty()) {
            showMessage(UserMessageKind::Critical, QStringLiteral("读取失败"), error);
            return;
        }
        const auto found = std::find_if(items.cbegin(), items.cend(), [mediaId](const auto& item) {
            return item.id == mediaId;
        });
        if (found == items.cend()) return;
        MediaSeries series;
        if (!seriesRepository.find(found->seriesId, &series, &error)) {
            showMessage(UserMessageKind::Critical, QStringLiteral("读取失败"), error);
            return;
        }
        selectedSeriesId = found->seriesId;
        detailSingleItem.reset();
        detailSeriesItem = *found;
        showMediaDetail({MediaSourceKind::SeriesItem, found->id, found->currentPath,
                         found->fileState == FileState::Available, found->durationMs},
                        found->displayName, series.targetLanguageId,
                        found->fileState == FileState::Available);
    }

    void showMediaDetail(const MediaSourceReference& source, const QString& displayName,
                         const QString& languageId, bool available)
    {
        currentDetail = MediaDetailData{source, displayName, languageName(languageId),
                                        available ? QStringLiteral("可用")
                                                  : QStringLiteral("文件缺失"),
                                        QStringLiteral("未识别")};
        detailWidget->setMedia(*currentDetail);
        pages->setCurrentWidget(detailWidget);
        window->setWindowTitle(QStringLiteral("%1 — MNCE").arg(displayName));
        playerController->load(source);
        detailWidget->updatePlayback(*playerController);
    }

    void leaveMediaDetail()
    {
        if (playerController) playerController->unload();
        currentDetail.reset();
        detailSingleItem.reset();
        detailSeriesItem.reset();
        if (pages) pages->setCurrentWidget(libraryPage);
        window->setWindowTitle(QStringLiteral("MNCE"));
    }

    void relocateCurrentMedia()
    {
        if (detailSingleItem) {
            chooseSingleRelocation(*detailSingleItem);
        } else if (detailSeriesItem && selectedSeriesId != 0) {
            chooseSeriesRelocation();
        }
    }

    void persistDuration(const MediaSourceReference& source, qint64 duration)
    {
        QString error;
        const bool saved = source.kind == MediaSourceKind::SingleFile
            ? repository.updateDuration(source.mediaId, duration, &error)
            : seriesRepository.updateMediaDuration(source.mediaId, duration, &error);
        if (!saved) {
            showMessage(UserMessageKind::Critical, QStringLiteral("保存时长失败"), error);
            return;
        }
        if (source.kind == MediaSourceKind::SingleFile) {
            reloadSingleTable(source.mediaId);
            if (detailSingleItem && detailSingleItem->id == source.mediaId) {
                detailSingleItem->durationMs = duration;
            }
        } else {
            reloadSeriesDetail();
            if (detailSeriesItem && detailSeriesItem->id == source.mediaId) {
                detailSeriesItem->durationMs = duration;
            }
        }
        if (currentDetail && currentDetail->source.kind == source.kind
            && currentDetail->source.mediaId == source.mediaId) {
            currentDetail->source.knownDurationMs = duration;
            detailWidget->setMedia(*currentDetail);
            detailWidget->updatePlayback(*playerController);
        }
    }

    void startSelectedSeriesRefresh()
    {
        if (watcher.isRunning() || selectedSeriesId == 0) {
            return;
        }
        MediaSeries series;
        QString error;
        if (!seriesRepository.find(selectedSeriesId, &series, &error)) {
            showMessage(UserMessageKind::Critical, QStringLiteral("刷新失败"), error);
            return;
        }
        startPreparedSeriesTask(TaskKind::SeriesRefresh, [this, series] {
            return seriesService.prepareRefresh(series, &stopRequested);
        });
    }

    void setTaskControls(bool enabled)
    {
        addButton->setEnabled(enabled);
        singleRefreshButton->setEnabled(enabled);
        addFolderButton->setEnabled(enabled);
        seriesRefreshButton->setEnabled(enabled);
        seriesRelocateButton->setEnabled(enabled);
        seriesTable->setEnabled(enabled);
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
        case TaskKind::SingleRelocate:
            finishSingleRelocation(result);
            break;
        case TaskKind::SingleRefresh:
            finishSingleRefresh(result);
            break;
        case TaskKind::SeriesCreate:
        case TaskKind::SeriesRefresh:
        case TaskKind::SeriesRelocate:
            finishSeriesTask(result);
            break;
        case TaskKind::None:
            break;
        }
    }

    void finishImport(const TaskResult& task)
    {
        if (!task.hash.succeeded()) {
            showMessage(UserMessageKind::Warning, QStringLiteral("添加失败"),
                        hashFailureMessage(task.hash));
            return;
        }
        const auto inserted = repository.insert(task.preparedMedia);
        if (inserted.status == InsertMediaStatus::Inserted) {
            if (currentLanguageId() == task.languageId) {
                reloadSingleTable(inserted.mediaId);
                showMessage(UserMessageKind::Information, QStringLiteral("添加完成"),
                            QStringLiteral("文件已加入媒体库。"));
            } else {
                showMessage(UserMessageKind::Information, QStringLiteral("添加完成"),
                            QStringLiteral("文件已加入“%1”内容空间。")
                                .arg(languageName(task.languageId)));
            }
        } else if (inserted.status == InsertMediaStatus::Duplicate) {
            if (currentLanguageId() == task.languageId) {
                reloadSingleTable(inserted.mediaId);
            }
            showMessage(UserMessageKind::Information, QStringLiteral("内容已存在"),
                        QStringLiteral("该内容已在当前目标语言的媒体库中。"));
        } else {
            showMessage(UserMessageKind::Critical, QStringLiteral("添加失败"), inserted.error);
        }
    }

    void finishSingleRelocation(const TaskResult& task)
    {
        if (!MediaRefreshService::matchesOriginal(task.original, task.hash)) {
            showMessage(UserMessageKind::Warning, QStringLiteral("重新定位失败"),
                        QStringLiteral("所选文件不是原记录对应的同一内容。"));
            return;
        }
        QString error;
        const QFileInfo info(task.path);
        if (!repository.relocate(task.original.id, info.absoluteFilePath(), info.fileName(), &error)) {
            showMessage(UserMessageKind::Critical, QStringLiteral("重新定位失败"), error);
            return;
        }
        reloadSingleTable(task.original.id);
        if (detailSingleItem && detailSingleItem->id == task.original.id) {
            openSingleMedia(task.original.id);
        }
        showMessage(UserMessageKind::Information, QStringLiteral("重新定位完成"),
                    QStringLiteral("媒体文件路径已更新。"));
    }

    void finishSingleRefresh(const TaskResult& task)
    {
        QString error;
        if (!refreshService.apply(repository, task.refreshResults, &error)) {
            showMessage(UserMessageKind::Critical, QStringLiteral("刷新失败"), error);
        }
        reloadSingleTable();
    }

    void finishSeriesTask(const TaskResult& task)
    {
        const qint64 detailMediaId = detailSeriesItem ? detailSeriesItem->id : 0;
        const auto applied = seriesService.apply(seriesRepository, task.seriesOperation);
        if (!applied.succeeded()) {
            showMessage(applied.status == SeriesOperationStatus::ScanFailed
                            ? UserMessageKind::Warning : UserMessageKind::Critical,
                        task.kind == TaskKind::SeriesCreate ? QStringLiteral("创建系列失败")
                                                           : QStringLiteral("系列操作失败"),
                        applied.error);
            return;
        }
        if (task.kind == TaskKind::SeriesCreate
            && currentLanguageId() == task.seriesOperation.targetLanguageId) {
            selectedSeriesId = applied.seriesId;
        }
        reloadSeriesTable();
        reloadSeriesDetail();
        if (detailMediaId != 0) {
            openSeriesMedia(detailMediaId);
        }
    }

    void removeItem(qint64 id)
    {
        QString error;
        if (!repository.remove(id, &error)) {
            showMessage(UserMessageKind::Critical, QStringLiteral("删除失败"), error);
            return;
        }
        reloadSingleTable();
    }

    void removeSeries(const MediaSeries& series, int itemCount)
    {
        if (!dialogs->confirmSeriesDeletion(window, series.name, itemCount)) {
            return;
        }
        const auto result = seriesService.remove(seriesRepository, series.id);
        if (!result.succeeded()) {
            showMessage(UserMessageKind::Critical, QStringLiteral("删除失败"), result.error);
            return;
        }
        if (selectedSeriesId == series.id) {
            selectedSeriesId = 0;
        }
        reloadSeriesTable();
        reloadSeriesDetail();
    }

    void showMessage(UserMessageKind kind, const QString& title, const QString& text)
    {
        dialogs->showMessage(window, kind, title, text);
    }

    void stopAndWait()
    {
        playerController->shutdown();
        closing = true;
        stopRequested.store(true);
        if (watcher.isRunning()) {
            watcher.waitForFinished();
        }
    }

    MainWindow* window;
    TargetLanguageCatalog catalog;
    MediaRepository repository;
    SeriesRepository seriesRepository;
    std::shared_ptr<LibraryUiDialogs> dialogs;
    FileHashService hashService;
    MediaImportService importService;
    MediaRefreshService refreshService;
    SeriesService seriesService;
    std::unique_ptr<QSettings> settings;
    std::unique_ptr<PlaybackBackend> playbackBackend;
    std::unique_ptr<PlayerController> playerController;
    WindowTitleBar* titleBar = nullptr;
    FramelessWindowController* frameController = nullptr;
    QComboBox* languageCombo = nullptr;
    QStackedWidget* pages = nullptr;
    QWidget* libraryPage = nullptr;
    MediaDetailWidget* detailWidget = nullptr;
    QTabWidget* tabs = nullptr;
    QPushButton* addButton = nullptr;
    QPushButton* singleRefreshButton = nullptr;
    QComboBox* singleSortFieldCombo = nullptr;
    QComboBox* singleSortDirectionCombo = nullptr;
    QTableWidget* table = nullptr;
    QPushButton* addFolderButton = nullptr;
    QComboBox* seriesSortFieldCombo = nullptr;
    QComboBox* seriesSortDirectionCombo = nullptr;
    QTableWidget* seriesTable = nullptr;
    QGroupBox* seriesDetail = nullptr;
    QLabel* seriesDetailName = nullptr;
    QLabel* seriesDirectoryLabel = nullptr;
    QPushButton* seriesRefreshButton = nullptr;
    QPushButton* seriesRelocateButton = nullptr;
    QComboBox* seriesMediaSortDirectionCombo = nullptr;
    QTableWidget* seriesMediaTable = nullptr;
    QFutureWatcher<TaskResult> watcher;
    std::atomic_bool stopRequested = false;
    qint64 selectedSeriesId = 0;
    std::optional<MediaDetailData> currentDetail;
    std::optional<MediaItem> detailSingleItem;
    std::optional<SeriesMediaItem> detailSeriesItem;
    bool closing = false;
};

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(TargetLanguageCatalog::builtIn(),
                 std::make_shared<NativeLibraryUiDialogs>(),
                 std::make_unique<QtPlaybackBackend>(), parent)
{
}

MainWindow::MainWindow(TargetLanguageCatalog catalog, QWidget* parent)
    : MainWindow(std::move(catalog), std::make_shared<NativeLibraryUiDialogs>(),
                 std::make_unique<QtPlaybackBackend>(), parent)
{
}

MainWindow::MainWindow(TargetLanguageCatalog catalog,
                       std::shared_ptr<LibraryUiDialogs> dialogs,
                       QWidget* parent)
    : MainWindow(std::move(catalog), std::move(dialogs),
                 std::make_unique<QtPlaybackBackend>(), parent)
{
}

MainWindow::MainWindow(TargetLanguageCatalog catalog,
                       std::shared_ptr<LibraryUiDialogs> dialogs,
                       std::unique_ptr<PlaybackBackend> playbackBackend,
                       QWidget* parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>(this, std::move(catalog), std::move(dialogs),
                                  std::move(playbackBackend)))
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
    if (!impl_->seriesRepository.open(databasePath, error)) {
        impl_->repository.close();
        return false;
    }
    if (settingsFilePath.isEmpty()) {
        impl_->settings = std::make_unique<QSettings>();
    } else {
        impl_->settings = std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat);
    }
    impl_->restoreSortPreferences();
    const qreal playbackRate = playbackRateFromSetting(
        impl_->settings->value(QString::fromLatin1(playbackRateSetting), 1.0));
    impl_->detailWidget->setPlaybackRate(playbackRate);
    (void) impl_->playerController->setPlaybackRate(playbackRate);
    QString languageId = impl_->settings->value(
        QString::fromLatin1(currentLanguageSetting), impl_->catalog.defaultLanguageId()).toString();
    if (!impl_->catalog.contains(languageId)) {
        languageId = impl_->catalog.defaultLanguageId();
    }
    const int index = impl_->languageCombo->findData(languageId);
    impl_->languageCombo->setCurrentIndex(index);
    impl_->reloadAll();
    impl_->startSingleRefresh(true);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    impl_->stopAndWait();
    QMainWindow::closeEvent(event);
}

} // namespace mnce
