# 平台、发行与许可证

## 技术栈

- C++20
- Qt 6.11.2 Widgets
- CMake
- SQLite
- whisper.cpp
- FFmpeg
- GPLv3

项目目标是尽量静态链接、固定依赖版本并提供可复现构建。所有平台的 Qt 版本锁定为 6.11.2；Windows 使用 `msvc2022_64` kit 作为开发基线。

## 目标平台

| 平台 | MVP 最低兼容基线 | 架构 |
| --- | --- | --- |
| Windows | Windows 11 | x86-64 |
| Ubuntu | Ubuntu 22.04 | x86-64 |
| Debian | Debian 12 | x86-64 |
| Fedora | Fedora 42 | x86-64 |
| macOS | macOS 13 | Apple Silicon / arm64 |

不规定最低 CPU 型号和内存容量。发行说明应明确：识别速度和可处理模型大小取决于用户设备，不保证所有设备具有相同等待时间。

Fedora 42 已结束上游维护，但保留为 RPM 二进制兼容基线。发行测试还应覆盖发布时仍受 Fedora 维护的版本。

## 项目标识

- 组织名称：`timeofmei`
- 组织域名：`timeofmei.com`
- Qt application name：`MNCE`
- 应用 ID：`com.timeofmei.mnce`
- 发行包基础名：`mnce`
- SQLite 数据库文件名：`mnce.sqlite3`

应用必须在创建依赖数据位置的对象前设置上述 Qt 组织和应用信息。SQLite 数据库默认放在 Qt `QStandardPaths::AppLocalDataLocation` 返回的目录中，不写入程序安装目录或当前工作目录。

## 发行包

- Windows：安装版 `.msi` 和便携版 `.zip`
- Ubuntu/Debian：`.deb`
- Fedora：`.rpm`
- macOS：`.dmg`
- Linux 不提供 AppImage

安装包包含应用和运行时依赖，但不包含 Whisper 模型。尽量采用用户级安装，避免管理员权限。

卸载应用时默认不自动删除模型和用户数据，但必须提供明确的数据清理选项。每个发行文件提供校验值。

## 推理硬件范围

- 纯 CPU 必须可运行。
- MVP 不以独立显卡作为前置条件。
- GPU 加速不作为 Windows/Linux MVP 的必要能力。
- Apple Silicon 的具体 Metal 启用策略待构建验证。

## 许可证结论

### 应用与 Qt

应用使用 GPLv3。Qt 6 静态链接选择 Qt 的 GPLv3 授权路径。发布二进制时提供应用源码、实际使用的 Qt 源码和补丁，以及完整构建和安装方式。

### Whisper 与 whisper.cpp

OpenAI Whisper 代码和模型权重为 MIT，whisper.cpp 也为 MIT，可与 GPLv3 应用结合。安装包和第三方许可页保留相应版权及 MIT 许可文本。

只纳入由 whisper.cpp 官方下载流程指向的 OpenAI 原始模型转换版本。

### FFmpeg

- 发布构建禁止启用 `nonfree`。
- 使用满足本地音频解码和重采样需求的最小配置。
- 固定源码版本、补丁和完整 configure 参数。
- 随发行物提供适用的 LGPL/GPL 文本及对应源码。

### SQLite

SQLite 交付代码属于 Public Domain，可以静态嵌入。

## 发布门禁

- 固定所有依赖的版本、提交和源码校验值。
- 固定 Whisper 模型提交、文件名和 SHA-256。
- 保存 Qt、whisper.cpp 和 FFmpeg 的完整构建参数。
- CI 拒绝包含 FFmpeg `nonfree` 组件的构建。
- 生成实际依赖清单或 SBOM。
- 安装包和发布页面提供 GPLv3、MIT、FFmpeg 适用许可证及第三方声明。
- Windows 和 macOS 代码签名方案在首次正式发布前确定。

## 许可证参考

- [Qt 6 Licensing](https://doc.qt.io/qt-6/licensing.html)
- [OpenAI Whisper LICENSE](https://github.com/openai/whisper/blob/main/LICENSE)
- [whisper.cpp LICENSE](https://github.com/ggml-org/whisper.cpp/blob/master/LICENSE)
- [whisper.cpp 模型清单](https://github.com/ggml-org/whisper.cpp/blob/master/models/README.md)
- [FFmpeg License](https://ffmpeg.org/doxygen/trunk/md_LICENSE.html)
- [FFmpeg Legal](https://ffmpeg.org/legal.html)
- [SQLite Copyright](https://www.sqlite.org/copyright.html)
