# Fedora 开发环境

## 适用范围

本文说明如何在传统 RPM 版 Fedora 桌面系统上配置 MNCE 的本地开发、构建、测试和运行环境。项目的平台范围和发行约束仍以[平台、发行与许可证](platform-and-distribution.md)为准；本文不涉及 RPM 制作、依赖部署或发行验证。

以下包名和系统 Qt 路径已在 Fedora 44 x86-64 上核对。Fedora Atomic Desktop 使用不同的软件安装模型，不适用本文的 `dnf install` 步骤。

## 开发基线

- x86-64 Fedora 桌面系统
- GCC C++ 工具链，支持 C++20
- CMake 3.30.5 或更高版本
- Ninja
- Qt 6.11.2 或更高版本，包含 Core、Concurrent、Network、Sql、Widgets 和 Multimedia
- Wayland 或 X11 图形会话
- 可用的桌面音频输出；Fedora 默认的 PipeWire 环境即可

项目的 Qt 版本范围以[平台、发行与许可证](platform-and-distribution.md#技术栈)为准。CMake 会拒绝低于最低要求的 Qt；使用更高的 Qt 6 版本时应运行完整构建和测试。

## 安装开发环境

### 安装通用构建工具

```bash
sudo dnf install \
    gcc-c++ \
    cmake \
    ninja-build
```

### 安装 Qt

下面两种 Qt 安装方案选择一种，不要同时执行。

#### 方案一：使用 Fedora 系统 Qt

当 Fedora 软件源提供满足项目最低要求的 Qt 时，可以直接使用系统包：

```bash
sudo dnf install \
    qt6-qtbase-devel \
    qt6-qtmultimedia-devel \
    qt6-qtwayland
```

`qt6-qtbase-devel` 提供项目使用的 Qt Core、Concurrent、Network、Sql 和 Widgets 开发文件，`qt6-qtmultimedia-devel` 提供 Qt Multimedia 开发文件。`qt6-qtwayland` 是 Wayland 桌面会话所需的运行时平台插件，不需要对应的开发包。

`dnf` 会安装当前软件源提供的 Qt 6 版本，CMake 配置阶段会验证它是否满足项目最低要求。不要使用 `dnf versionlock` 冻结全部 `qt6-*` 包：Fedora 桌面和其他系统应用也会使用这些包，长期锁定可能阻碍正常的兼容性及安全更新。

#### 方案二：使用独立 Qt 安装

如果当前 Fedora 软件源的 Qt 低于项目最低要求，或者需要固定补丁版本以获得可复现的开发环境，应使用 Qt 官方安装器安装 Linux `gcc_64` 构建以及 Qt Multimedia 模块。安装方法见 Qt 官方的 [Get and Install Qt](https://doc.qt.io/qt-6/get-and-install-qt.html) 和 [Qt Online Installer](https://doc.qt.io/qt-6/qt-online-installation.html)。

假设 Qt 安装在默认用户目录，可在当前终端设置：

```bash
export QT_ROOT="$HOME/Qt/6.11.2/gcc_64"
export CMAKE_PREFIX_PATH="$QT_ROOT"
export PATH="$QT_ROOT/bin:$PATH"
```

切换 Qt 安装或前缀后应使用新的构建目录，避免复用包含旧 Qt 路径的 CMake 缓存。

### 验证开发环境

完成其中一种 Qt 安装方案后检查工具链：

```bash
g++ --version
cmake --version
ninja --version
qtpaths6 --qt-version
```

`qtpaths6 --qt-version` 输出的版本必须满足项目最低要求。CMake 必须不低于 `3.30.5`。

## 配置、构建与测试

Debug：

```bash
cmake --preset fedora-debug
cmake --build --preset fedora-debug
ctest --preset fedora-debug
```

Release：

```bash
cmake --preset fedora-release
cmake --build --preset fedora-release
ctest --preset fedora-release
```

GUI 测试必须从当前用户的有效图形桌面会话中运行。正常情况下不设置 `QT_QPA_PLATFORM`，由 Qt 根据 Wayland 或 X11 会话自动选择平台插件。

## 启动应用

在项目根目录运行对应构建产物：

```bash
./build/fedora-debug/src/mnce
```

应用应以普通用户身份启动，不使用 `sudo`。Fedora 默认保留桌面环境提供的原生标题栏；M5 的 Windows 专用自定义窗口框架不会在 Fedora 启用。

首次运行至少检查：

- 主窗口能够显示、调整大小、最小化、最大化和关闭
- 原生标题栏、文件选择器和消息框能够正常显示及获取焦点
- 目标语言选择、媒体库、系列和媒体详情导航可用
- 本地音频能够加载、播放、暂停、跳转和调整倍速
- 关闭应用时没有残留可见窗口或播放

## 开发工具集成

使用支持 CMake 的 IDE 时，选择系统 GCC、Ninja、`fedora-debug` 或 `fedora-release` 预设，以及与命令行构建相同的 Qt 前缀。IDE 的构建目录应与命令行构建目录分开。不要在 Fedora 上选择 `windows-msvc-*` 预设。

## 常见问题

### CMake 找不到 Qt 或报告版本不匹配

先确认 `qtpaths6 --qt-version` 满足项目最低要求，再检查 `CMAKE_PREFIX_PATH` 是否指向预期的 Qt 安装。修正环境后使用新的构建目录重新配置，不降低项目的 Qt 最低版本要求。

### Wayland 平台插件无法加载

确认已安装 `qt6-qtwayland`，并从有效的桌面终端启动应用或 GUI 测试。只有在排查插件加载问题时临时使用：

```bash
QT_DEBUG_PLUGINS=1 ./build/fedora-debug/src/mnce
```

### 音频没有输出

先确认桌面系统本身能够播放声音，再检查 PipeWire 是否识别输出设备：

```bash
wpctl status
```

应用不要求独立显卡，也不应依赖网络、Whisper 模型或麦克风来运行现有自动测试。
