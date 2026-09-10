#pragma once

#include "domain/target-language.h"

#include <QMainWindow>

#include <memory>

class QCloseEvent;

namespace mnce {

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);
    MainWindow(TargetLanguageCatalog catalog, QWidget* parent = nullptr);
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
