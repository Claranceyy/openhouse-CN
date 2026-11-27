<div align="center">

<img src="tools/horrible-logo-to-be-replaced.svg" alt="openhouse logo" width="128">

# openhouse 简体中文汉化版

开源 SteamVR Lighthouse 驱动替代项目 **[YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse)** 的简体中文外挂汉化分支。

<a href="https://github.com/Claranceyy/openhouse-CN/releases/latest"><img src="https://img.shields.io/badge/Releases-下载汉化版-0078D6?style=for-the-badge&logo=github" alt="Download Releases"></a>
&nbsp;
<a href="https://github.com/Claranceyy/openhouse-CN/actions"><img src="https://img.shields.io/badge/Actions-云端构建产物-2EA44F?style=for-the-badge&logo=githubactions&logoColor=white" alt="GitHub Actions"></a>
&nbsp;
<a href="https://github.com/YusufOzmen01/openhouse"><img src="https://img.shields.io/badge/Upstream-原版英文仓库-555555?style=for-the-badge&logo=github" alt="Upstream Repository"></a>

[所有版本发布](https://github.com/Claranceyy/openhouse-CN/releases) · [原版项目文档](https://github.com/YusufOzmen01/openhouse#readme) · [原项目 Discord](https://discord.gg/7dBHBj2bjG) · [汉化维护说明](i18n/README.md)

</div>

---

## 项目简介

**[openhouse](https://github.com/YusufOzmen01/openhouse)** 是 SteamVR 原厂 Lighthouse 定位驱动的开源替代方案，支持 Windows 与 Linux（兼容 SteamVR、Monado / WiVRn 及独立运行），带来统一房间建图、更稳健的防漂移/遮挡恢复等诸多改进，同时内置：

- **lhbs**：开源 Lighthouse 1.0 基站固件，突破原厂 2 基站上限，支持同时使用**最多 4 个 1.0 基站**。
- **Doorman**：自研开源 2.4 GHz 跳频无线协议，单枚通用 nRF52840 接收器即可同时连接**多达 8～10 台**追踪器与手柄。

<div align="center">
<img src="images/overlay-overview.png" width="800">
</div>

---

## 本汉化版特点

- **全界面中文化**：完整覆盖安装器（`openhouse-installer`）、VR / 桌面主控面板（`openhouse-overlay`）、Doorman 接收器管理器（`doorman-dongle-manager`）、状态查看器（`openhouse-viewer`）、开发者视图（`lhviz`），以及 Windows / Linux 启动弹窗与 VR 头显内通知。
- **低侵入外挂架构**：通过底层 ImGui 透明拦截与自动合并系统中文字体实现，几乎不改动上游业务源码，便于持续跟进原作者更新。
- **外置词典热修改**：压缩包根目录附带 [zh_CN.json](i18n/zh_CN.json)，无需重新编译，直接修改文件并重启程序即可实时调整译文（详见 [i18n/README.md](i18n/README.md)）。
- **云端透明编译**：所有 Windows / Linux 安装包均直接由 GitHub Actions 公开流水线从本仓库源码编译产出。

---

## 下载与安装

> [!IMPORTANT]
> 安装或卸载驱动前，请先**完全关闭 SteamVR**（Linux 下请关闭 Monado / WiVRn）。

1. 前往本仓库 [Releases 页面](https://github.com/Claranceyy/openhouse-CN/releases)（或最新 [Actions](https://github.com/Claranceyy/openhouse-CN/actions) 底部 Artifacts）下载 `openhouse-windows-x64.zip`（Linux 下载 `.tar.gz`）并**完整解压**。
2. 运行根目录下的 `openhouse-installer.exe`，点击 **安装驱动** 即可。
3. 如需刷写 1.0 基站 `lhbs` 固件或 `Doorman` 接收器/设备固件，在安装器主页进入 **安装或更新固件** 按中文引导操作即可。

> [!NOTE]
> **关于 Windows 安全拦截提示**：本汉化版由个人 Fork 通过 GitHub Actions 免费编译，**不包含商业或 SignPath 代码签名证书**。首次运行若弹出 Windows SmartScreen（“Windows 已保护你的电脑”），点击 **更多信息 → 仍要运行** 即可（也可在解压前右键压缩包 → 属性 → 勾选「解除锁定」）。如需带 SignPath 官方签名的原版英文包，请前往 [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse/releases) 下载。

---

## 问题反馈

- **翻译错漏、术语优化或中文排版问题**：欢迎在本仓库提交 [Issue](https://github.com/Claranceyy/openhouse-CN/issues) 或直接 PR 修改 [i18n/zh_CN.json](i18n/zh_CN.json)。
- **驱动追踪、固件刷写、硬件兼容等本体问题**：请先在原版环境复现，并前往上游仓库 [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse/issues) 或原项目 [Discord 社区](https://discord.gg/7dBHBj2bjG) 反馈。

---

## 致谢

- 衷心感谢原项目作者 **[Yusuf Çınar Özmen (`YusufOzmen01`)](https://github.com/YusufOzmen01)** 及所有为 **[openhouse](https://github.com/YusufOzmen01/openhouse)** 做出贡献的开发者与社区测试者！如果 `openhouse` 帮到了你，欢迎前往[原项目主页](https://github.com/YusufOzmen01/openhouse)给原作者点亮 Star 或支持他的开发工作。
- 感谢开源项目 **[collabora/libsurvive](https://github.com/collabora/libsurvive)**，为上游 `openhouse` 的 Watchman 协议、光学解码与 Lighthouse 2.0 实现提供了大量宝贵参考。
- 感谢 **[Dear ImGui](https://github.com/ocornut/imgui)** 等上游所依赖的众多优秀开源项目。

---

## 说明与协议

- **AI 辅助声明**：与原项目一致，本汉化层框架与中文词典亦在 AI 辅助下完成并经过实机测试校验。
- **开源协议**：本项目严格遵循上游的 **MIT License**，详见 [LICENSE](LICENSE)。
- **免责声明**：本项目与 Valve Corporation 或 HTC Corporation 无任何关联、认可或赞助关系。Valve、SteamVR 与 Lighthouse 为 Valve Corporation 的商标，HTC 与 VIVE 为 HTC Corporation 的商标。
