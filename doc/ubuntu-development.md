# Ubuntu 开发环境

## 适用范围

本文说明如何在 x86-64 Ubuntu Desktop 上配置 MNCE 的本地开发、构建、测试和运行环境。项目的平台范围和发行约束仍以[平台、发行与许可证](platform-and-distribution.md)为准；本文不涉及 DEB 制作、依赖部署或发行验证。

以下包名、系统 Qt 状态和独立 Qt 工作流已在 Ubuntu 26.04.1 LTS GNOME Wayland 上核对。Ubuntu 22.04 仍是项目的最低兼容基线，但其发行版工具和 Qt 版本可能低于源码构建要求，需要单独安装满足最低版本的开发工具。

## 开发基线

- x86-64 Ubuntu Desktop
- GCC C++ 工具链，支持 C++20
- CMake 3.30.5 或更高版本
- Ninja
- Qt 6.11.2 或更高版本，包含 Core、Concurrent、Network、Sql、Widgets、Multimedia 和 Test
- Wayland 或 X11 图形会话
- 可用的桌面音频输出；Ubuntu 默认的 PipeWire 环境即可

项目的 Qt 版本范围以[平台、发行与许可证](platform-and-distribution.md#技术栈)为准。CMake 会拒绝低于最低要求的 Qt；使用更高的 Qt 6 版本时应运行完整构建和测试。

## 安装开发环境

### 安装通用构建工具

```bash
sudo apt update
sudo apt install \
    build-essential \
    cmake \
    ninja-build
```

安装后必须检查实际版本。若发行版提供的 CMake 低于项目要求，应从受控来源安装较新版本，而不是降低项目的 `cmake_minimum_required()`。

### 安装 Qt

下面两种 Qt 安装方案选择一种作为当前构建的 Qt 前缀。切换方案后不要复用指向另一套 Qt 的 CMake 缓存。

#### 方案一：使用 Ubuntu 系统 Qt

仅当当前 Ubuntu 软件源提供满足项目最低版本的 Qt 时，才可以使用系统包：

```bash
sudo apt install \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-wayland \
    libqt6sql6-sqlite
```

`qt6-base-dev` 提供项目使用的 Qt Core、Concurrent、Network、Sql、Widgets 和 Test 开发文件，`qt6-multimedia-dev` 提供 Qt Multimedia 开发文件，`qt6-wayland` 提供 Wayland 平台支持，`libqt6sql6-sqlite` 提供 SQLite 驱动。

Ubuntu 26.04.1 LTS 上核对到的系统 Qt 为 6.10.2，低于当前源码构建下限，因此该环境不能直接使用系统 Qt 构建 MNCE，应选择独立 Qt 安装。

#### 方案二：使用独立 Qt 安装

当系统 Qt 低于项目最低要求，或者需要固定补丁版本以获得可复现的开发环境时，应使用 Qt 官方安装器安装 Linux `gcc_64` 构建以及 Qt Multimedia 模块。安装方法见 Qt 官方的 [Get and Install Qt](https://doc.qt.io/qt-6/get-and-install-qt.html) 和 [Qt Online Installer](https://doc.qt.io/qt-6/qt-online-installation.html)。

假设 Qt 安装在默认用户目录，可在当前终端设置：

```bash
export QT_ROOT="$HOME/Qt/6.11.2/gcc_64"
export CMAKE_PREFIX_PATH="$QT_ROOT"
export PATH="$QT_ROOT/bin:$PATH"
```

这些变量必须在运行 CMake、CTest 和应用的终端或 IDE 环境中保持一致。切换 Qt 安装或前缀后应使用新的构建目录，避免复用包含旧 Qt 路径的 CMake 缓存。

### 验证开发环境

完成 Qt 安装和环境设置后检查工具链：

```bash
g++ --version
cmake --version
ninja --version
qtpaths6 --qt-version
```

`qtpaths6 --qt-version` 必须输出当前选定的 Qt，且版本不低于 6.11.2。CMake 必须不低于 3.30.5。如果系统和独立 Qt 同时存在，还应检查：

```bash
command -v qtpaths6
qtpaths6 --query QT_INSTALL_PREFIX
```

## 配置、构建与测试

Debug：

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Release：

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

两个 preset 分别使用 `build/debug` 和 `build/release`。GUI 测试必须从当前用户的有效图形桌面会话中运行。正常情况下不设置 `QT_QPA_PLATFORM`，由 Qt 根据 Wayland 或 X11 会话自动选择平台插件。

## 启动应用

在项目根目录运行对应构建产物：

```bash
./build/debug/src/mnce
```

应用应以普通用户身份从有效桌面会话启动，不使用 `sudo`。Linux 主窗口使用 MNCE 自定义标题栏；文件选择器、消息框和其他对话框继续使用桌面环境提供的原生窗口框架。

首次运行至少检查：

- 主窗口只显示一套 MNCE 自定义标题栏
- 主窗口能够拖动、调整大小、最小化、最大化、还原、贴靠和关闭
- 最大化窗口不显示边缘缩放光标，标题栏按钮和内容控件不误触窗口操作
- 原生文件选择器和消息框能够正常显示、聚焦并归属于主窗口
- 目标语言选择、媒体库、系列和媒体详情导航可用
- 本地音频能够加载、播放、暂停、跳转和调整倍速
- 关闭应用时没有残留可见窗口或播放

Linux 自定义标题栏的完整验收边界见 [M6：Linux 自定义标题栏](M6.md)。

## 开发工具集成

使用支持 CMake 的 IDE 时，选择系统 GCC、Ninja、`linux-debug` 或 `linux-release` 预设，以及与命令行构建相同的 Qt 前缀。IDE 的构建目录应与命令行构建目录分开。不要在 Ubuntu 上选择 `windows-msvc-*` 预设。

如果 IDE 自带 CMake，应确认其版本满足项目要求。IDE 继承的 `CMAKE_PREFIX_PATH` 和 `PATH` 也必须指向预期 Qt，不能只在另一个终端中设置。

## 常见问题

### CMake 找到系统 Qt 6.10.2

确认已在当前终端激活独立 Qt，并检查：

```bash
qtpaths6 --qt-version
qtpaths6 --query QT_INSTALL_PREFIX
```

如果现有构建目录已经缓存系统 Qt，切换前缀后应使用新的构建目录或清理该构建目录的 CMake 缓存，不要降低项目的 Qt 最低版本要求。

### Wayland 平台插件无法加载

系统 Qt 路径需要安装 `qt6-wayland`；独立 Qt 路径需要确认所选 Qt 安装包含 Wayland 平台插件。应从有效的 GNOME Wayland 会话启动应用或 GUI 测试。只有在排查插件加载问题时临时使用：

```bash
QT_DEBUG_PLUGINS=1 ./build/debug/src/mnce
```

### SSH 中无法运行 GUI 测试或应用

普通 SSH 登录不会自动继承桌面会话的 D-Bus、显示服务器和 Wayland 环境。优先在 GNOME 桌面终端中运行 GUI 测试；远程验证时必须使用当前登录用户的真实图形会话环境，不能用 `offscreen` 平台替代 Wayland 窗口验收。

### 音频没有输出

先确认桌面系统本身能够播放声音，再检查 PipeWire 是否识别输出设备：

```bash
wpctl status
```

应用不要求独立显卡，也不应依赖网络、Whisper 模型或麦克风来运行现有自动测试。
