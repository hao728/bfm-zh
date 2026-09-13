# Changelog

All notable changes to **BFM-Zh (Banner File Manager 中文美化增强版)** are documented in this file.

Base project: [The412Banner/banner-file-manager](https://github.com/The412Banner/banner-file-manager) v1.2.1
License: GPL-3.0-or-later

---

## [Unreleased]

### Added
- 文本逐行 diff 比较（LCS 算法，双面板右键"比较文件"）
- 双面板同步浏览（导航时另一面板自动跟随同名子目录）
- 右键"加速运行（清内存）"——回收后台工作集后启动程序
- 右键"设置外部启动器"——配置自定义启动器路径
- 收藏夹（左侧树 ★收藏，注册表持久化）
- 多标签页（Ctrl+T 新建、Ctrl+W 关闭、中键关闭）
- 工具菜单快速启动（记事本/CMD/注册表/任务管理器）
- 地址栏自动补全（SHAutoComplete）

### Changed
- UI 字体优先使用微软雅黑（Microsoft YaHei），Wine 下更清晰
- 字号调整为 12px，抗锯齿渲染
- "设置外部启动器"位于右键菜单（单文件）

### Fixed
- 地址栏补全在 GCC 下的编译冲突（shlwapi.h 顺序）
- 双面板文件夹比较按文件名高亮差异

---

## [1.2.1-zh.1] - 2026-09-09

### Added
- 完整中文界面（中/英/葡/俄 四语言）
- 三模式主题（亮色/暗色/跟随系统）
- 注册表配置持久化
- 标签页容器

### Base
- 同步上游 The412Banner/banner-file-manager v1.2.1
