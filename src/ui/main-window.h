#pragma once

#include "domain/target-language.h"

#include <QMainWindow>

#include <memory>
#include <optional>

class QCloseEvent;
class QWidget;

namespace mnce {

enum class UserMessageKind { Information, Warning, Critical };

class LibraryUiDialogs
{
public:
    virtual ~LibraryUiDialogs() = default;

    [[nodiscard]] virtual QString chooseAudioFile(QWidget* parent, bool relocating) = 0;
    [[nodiscard]] virtual QString chooseSeriesDirectory(QWidget* parent, bool relocating) = 0;
    [[nodiscard]] virtual std::optional<QString> requestSeriesName(QWidget* parent) = 0;
    [[nodiscard]] virtual bool confirmSeriesDeletion(QWidget* parent,
                                                     const QString& seriesName,
                                                     int itemCount) = 0;
    virtual void showMessage(QWidget* parent, UserMessageKind kind,
                             const QString& title, const QString& text) = 0;
};

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);
    MainWindow(TargetLanguageCatalog catalog, QWidget* parent = nullptr);
    MainWindow(TargetLanguageCatalog catalog,
               std::shared_ptr<LibraryUiDialogs> dialogs,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool initialize(const QString& databasePath,
                                  const QString& settingsFilePath,
                                  QString* error);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mnce
