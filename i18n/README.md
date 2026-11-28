# openhouse 简体中文外挂汉化 (`i18n/`)

本目录实现了对 `openhouse` 全部界面（安装器 `openhouse-installer`、主控与 VR 面板 `openhouse-overlay`、接收器管理器 `doorman-dongle-manager`、状态查看器 `openhouse-viewer`、开发者视图 `lhviz` 及系统启动弹窗）的**低侵入外挂式汉化**。

## 架构特点（为什么几乎不会与原作者更新冲突）
- **ImGui 底层透明拦截**：仅在极少变动的 `third_party/imgui` 渲染/测宽/格式化层与字体初始化处打薄钩子，不修改控件 ID，也不大改 `apps/*/ui.cpp` 的业务代码。
- **自动合并系统中文字体**：在 `ImFontAtlas::AddFontDefault` 时统一缓存并合并系统 CJK 字体（Windows 优先微软雅黑 `msyh.ttc`，Linux 优先 `NotoSansCJK` / 文泉驿），所有字号共用一份字体内存。
- **内置词典 + 外部热覆盖**：
  - 编译时自动将 `i18n/zh_CN.json` 内置进二进制，安装或单文件运行时开箱即中文。
  - 运行时也会自动读取外部 `zh_CN.json`（按优先级：`OPENHOUSE_I18N_FILE` > 程序同目录 > 上级目录 > `%APPDATA%\lh\zh_CN.json` 或 `~/.config/lh/zh_CN.json`），修改外部 JSON 重启软件即可生效，无需重新编译。

## 词典格式 (`i18n/zh_CN.json`)
- `exact`：静态文本精确匹配（O(1) 哈希查找）。
- `format`：`printf` 格式化模板匹配（加载时自动校验 `%` 占位符类型与顺序完全一致，防止崩溃）。
- `patterns`：动态拼接文本模板，使用 `{1}`、`{2}` 捕获片段，使用 `{@1}` 对捕获片段进行递归翻译。

## 同步原作者更新与补翻流程
> 约定：`master` 为本项目默认显示的简体中文汉化主分支，`upstream` 分支用于干净同步原作者 `YusufOzmen01/openhouse` 的上游更新（在 GitHub 网页端使用 **Sync fork** 时，请先切换到 `upstream` 分支再同步，切勿在 `master` 分支直接丢弃提交）。

1. 同步上游更新并合入汉化主分支：
   ```bash
   git switch upstream
   git pull upstream master
   git push origin upstream
   git switch master
   git merge upstream
   ```
2. 校验词典并扫描是否有新增未翻译文本：
   ```bash
   python i18n/extract_strings.py
   ```
3. 运行期自动抓漏翻（可选）：启动软件前设置环境变量 `OPENHOUSE_I18N_DUMP=1`，退出后会在 `%APPDATA%\lh\i18n_missing.json`（Linux 为 `~/.config/lh/i18n_missing.json`）生成本次界面中未命中的英文词条。
4. 补完 `i18n/zh_CN.json` 后提交并推送到 `master` 分支，GitHub Actions 会自动编译打包 Windows 与 Linux 成品压缩包。
