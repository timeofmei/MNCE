#pragma once

#include <QMainWindow>

namespace mnce {

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace mnce
