# MNCE 文档

本目录记录 MNCE MVP 的产品决策、技术边界和待讨论事项。

MNCE 意为 **Master New Concept English**。产品定位是一款轻量化的个人语言学习桌面工具，以本地音频、本地 Whisper 转写和三种练习模式为核心。

## 文档索引

- [M1：可编译的桌面应用骨架](M1.md)：首个工程里程碑、交付内容和验收标准
- [产品范围](product-scope.md)：产品定位、MVP 范围和明确不做的功能
- [媒体库与语音识别](media-and-transcription.md)：文件管理、内容身份、模型和识别流程
- [学习模式](learning-modes.md)：影子跟读、逐句听写和全文背诵
- [页面与导航](ui-structure.md)：MVP 的初步信息架构
- [分句服务](sentence-segmentation.md)：DeepSeek、自定义 OpenAI-compatible 接口及本地回退
- [平台、发行与许可证](platform-and-distribution.md)：目标平台、安装包、技术栈和合规边界
- [待讨论事项](open-questions.md)：尚未最终确定的产品和工程问题

## 当前状态

本文档描述的是 MVP 讨论结果，不是完整需求规格。已明确的决策按当前基线执行；标为“待定”的内容需要在实现前或对应开发阶段继续讨论。
