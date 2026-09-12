# MNCE

MNCE（Master New Concept English）是一款以本地音频、本地 Whisper 转写和语言练习为核心的桌面应用。当前工程已完成桌面应用骨架、单文件媒体库、文件夹系列、本地音频播放和 Windows 自定义标题栏等 M1 至 M5 功能。

产品与工程决策见 [`doc/README.md`](doc/README.md)。

## Windows

- Windows 11 x86-64
- Visual Studio Community 2026 的 MSVC x64 工具链
- Windows SDK 10.0.26100
- Qt 6.11.2 `msvc2022_64`
- CMake 3.30.5
- Ninja 1.12.1

在 Visual Studio Developer PowerShell 中设置 Qt 安装目录：

```powershell
$env:QT_ROOT = 'C:\Qt\6.11.2\msvc2022_64'
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"
```

### 配置、构建与测试

Debug：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Release：

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

### 启动应用

在保持 Qt 开发环境可用的同一个终端中运行：

```powershell
.\build\windows-msvc-debug\src\mnce.exe
```

如需创建可脱离开发终端运行的临时目录，可以显式调用 `windeployqt`：

```powershell
cmake --build --preset windows-msvc-release
New-Item -ItemType Directory -Force .\deploy | Out-Null
Copy-Item .\build\windows-msvc-release\src\mnce.exe .\deploy\
& "$env:QT_ROOT\bin\windeployqt.exe" --release --no-translations .\deploy\mnce.exe
```

`windeployqt` 不属于普通编译步骤；`deploy` 目录也不会纳入版本控制。

## Fedora

Fedora 的系统依赖、Qt 配置、Debug/Release 构建、测试、运行及常见问题见 [`doc/fedora-development.md`](doc/fedora-development.md)。
