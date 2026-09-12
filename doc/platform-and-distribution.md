# 平台、发行与许可证

## 技术栈

- C++20
- Qt 6.11.2 或更高版本 Widgets
- CMake
- SQLite
- whisper.cpp
- FFmpeg
- MIT

项目自有代码和允许静态组合的宽松许可证依赖可以静态链接；Qt 和 FFmpeg 在正式发行中使用动态链接。优先采用 Qt 官方构建、Qt Multimedia 默认 FFmpeg 后端和 Qt 部署工具提供的运行时布局，不在没有实际兼容性问题时自行替换媒体后端。源码构建最低支持 Qt 6.11.2；更高的 Qt 6 版本可以用于开发，但必须通过完整构建和测试。正式发行仍固定并记录实际使用的 Qt 补丁版本；当前 Windows 开发与发行验证基线为动态版 Qt 6.11.2 `msvc2022_64` kit。

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

Fedora 开发机的工具链、Qt 安装及本地构建步骤见 [Fedora 开发环境](fedora-development.md)。

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

## 动态链接与部署

- Windows 和 macOS 使用 Qt 官方部署工具收集 Qt 共享库、平台插件、SQL 驱动、Qt Multimedia 后端及其 FFmpeg 运行库；发布前仍须检查实际产物，不能把部署工具成功视为依赖完整的证明。
- Linux 将项目锁定的 Qt 共享库和插件放在应用私有运行时目录，或在发行版能够提供完全兼容版本时声明系统依赖。Qt 部署工具不会在 Linux/X11 上自动部署 FFmpeg，打包脚本必须显式处理。
- Qt 和 FFmpeg 共享库必须从受控的应用目录或明确的系统位置加载，避免依赖开发机环境和不受控的库搜索路径。
- 便携版仍随包携带所需共享库和插件，不要求用户预先安装 Qt 或 FFmpeg。
- 项目内部静态库以及 MIT、BSD、Public Domain 等允许静态组合的依赖不受本节限制。

## 推理硬件范围

- 纯 CPU 必须可运行。
- MVP 不以独立显卡作为前置条件。
- GPU 加速不作为 Windows/Linux MVP 的必要能力。
- Apple Silicon 的具体 Metal 启用策略待构建验证。

## 许可证结论

### 应用与 Qt

MNCE 自有代码使用 MIT License。Qt 6 使用 LGPLv3 授权路径并动态链接；当前使用的 Qt 模块必须全部具有 LGPLv3 选项。引入仅按 GPL 提供的 Qt 模块前必须重新评估并更新本文件，不能让发行物在未记录的情况下转为 GPL 约束。

安装包随附 Qt 共享库和插件，并提供 MIT、LGPLv3、LGPLv3 所引用的 GPLv3 以及适用的第三方许可文本。发行页面提供实际使用的 Qt 源码、补丁和构建信息或符合许可证要求的获取方式。发行条款不得限制用户替换兼容的 Qt 共享库，也不得禁止为调试 LGPL 库修改而进行的逆向工程。

### Whisper 与 whisper.cpp

OpenAI Whisper 代码和模型权重为 MIT，whisper.cpp 也为 MIT，可与 MIT 应用结合。安装包和第三方许可页保留相应版权及 MIT 许可文本。

只纳入由 whisper.cpp 官方下载流程指向的 OpenAI 原始模型转换版本。

### FFmpeg

- Qt Multimedia 播放优先使用 Qt 6.11.2 官方构建配套的默认 FFmpeg 后端及共享库，并通过 Qt 部署工具收集运行时依赖。
- 项目自行构建的 FFmpeg 必须保持 LGPL 路径，禁止启用 `gpl` 和 `nonfree`，并使用共享库；基础配置包含 `--disable-gpl --disable-nonfree --enable-shared --disable-static`。
- 自行构建时使用满足本地音频解码和重采样需求的最小配置，不为未确认的格式或功能增加组件。
- 固定源码版本、补丁和完整 configure 参数。
- 随发行物提供适用的 LGPL/GPL 文本、第三方声明及对应源码或符合许可证要求的获取方式。
- 发布前检查实际打包的 FFmpeg 库及其依赖，不能仅依据预期 configure 参数判断许可证结果。

### SQLite

SQLite 交付代码属于 Public Domain，可以静态嵌入。

## 发布门禁

- 固定所有依赖的版本、提交和源码校验值。
- 固定 Whisper 模型提交、文件名和 SHA-256。
- 保存 Qt、whisper.cpp 和 FFmpeg 的完整构建参数。
- CI 拒绝项目自行构建或打包的 FFmpeg 含有 `gpl` 或 `nonfree` 组件。
- CI 检查正式产物动态链接 Qt 和 FFmpeg，且未意外包含其静态库代码。
- 生成实际依赖清单或 SBOM。
- 安装包和发布页面提供 MIT、LGPLv3、LGPLv3 所引用的 GPLv3、FFmpeg 适用许可证及第三方声明。
- 在未安装 Qt、FFmpeg 和开发工具的干净系统上验证安装版与便携版运行时依赖完整。
- Windows 和 macOS 代码签名方案在首次正式发布前确定。

## 许可证参考

- [MIT License](https://spdx.org/licenses/MIT.html)
- [Qt 6 Licensing](https://doc.qt.io/qt-6/licensing.html)
- [Qt LGPL Obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [OpenAI Whisper LICENSE](https://github.com/openai/whisper/blob/main/LICENSE)
- [whisper.cpp LICENSE](https://github.com/ggml-org/whisper.cpp/blob/master/LICENSE)
- [whisper.cpp 模型清单](https://github.com/ggml-org/whisper.cpp/blob/master/models/README.md)
- [FFmpeg License](https://ffmpeg.org/doxygen/trunk/md_LICENSE.html)
- [FFmpeg Legal](https://ffmpeg.org/legal.html)
- [SQLite Copyright](https://www.sqlite.org/copyright.html)
