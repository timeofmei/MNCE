# MNCE 文档

本目录记录 MNCE MVP 的产品决策、技术边界和待讨论事项。

MNCE 意为 **Master New Concept English**。产品定位是一款轻量化的个人语言学习桌面工具，以本地音频、本地 Whisper 转写和三种练习模式为核心。

## 文档索引

- [M1：可编译的桌面应用骨架](M1.md)：首个工程里程碑、交付内容和验收标准
- [M2：单文件媒体库基础](M2.md)：本地数据库、媒体导入、内容去重、缺失检测与重新定位
- [M3：文件夹系列](M3.md)：文件夹扫描、系列排序、刷新、缺失检测与重新定位
- [M4：本地音频播放基础](M4.md)：统一媒体详情、时长、播放控制和倍速偏好
- [M5：Windows 自定义标题栏](M5.md)：自定义主窗口框架、Windows 系统贴靠和基础窗口控制
- [产品范围](product-scope.md)：产品定位、MVP 范围和明确不做的功能
- [目标语言与内容空间](target-languages.md)：可扩充语言目录、全局语言上下文和跨语言隔离规则
- [媒体库与语音识别](media-and-transcription.md)：文件管理、内容身份、模型和识别流程
- [音频播放](audio-playback.md)：播放器入口、控制、时长、倍速偏好和技术边界
- [学习模式](learning-modes.md)：影子跟读、逐句听写和全文背诵
- [页面与导航](ui-structure.md)：MVP 的初步信息架构
- [分句服务](sentence-segmentation.md)：DeepSeek、自定义 OpenAI-compatible 接口及本地回退
- [测试策略](testing-strategy.md)：自动测试分层、GUI 端到端测试和人工界面验证规则
- [平台、发行与许可证](platform-and-distribution.md)：目标平台、安装包、技术栈和合规边界
- [待讨论事项](open-questions.md)：尚未最终确定的产品和工程问题

## 当前状态

本文档描述的是 MVP 讨论结果，不是完整需求规格。已明确的决策按当前基线执行；标为“待定”的内容需要在实现前或对应开发阶段继续讨论。
