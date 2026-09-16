# BFM-Zh v1.2.1-zh.3 发布文案（复制到GitHub Release）

## 标题
```
BFM-Zh v1.2.1-zh.3 中文增强版 · 转区启动 + 视图重构 + 字体清晰化
```

## 标签
```
v1.2.1-zh.3
```

## 简介（Release Body）

> BFM（Banner File Manager）中文汉化增强版，适用于 Winlator / Bannerlator / Bionic 等 Wine 容器。

### 🆕 本版新增
- **以指定区域运行**：右键 exe → 转区启动（日文 / 简中 / 繁中 / 英文），解决 galgame、汉化补丁乱码，参考 Locale Emulator 思路，Wine 下通过环境变量实现
- **大图标视图重构**：自定义绘制，多行文件名不截断，超长文件名完整显示
- **菜单 / 标题字体提升**：菜单栏、右键菜单、标题栏字体统一 11pt，解决 Bionic 下字体过小模糊
- **修复 SHA1 菜单显示字面量** `%ls` 的问题

### ✅ 已有功能
- **启动增强**：加速运行（清内存，显示释放量）/ 激进加速 / 以参数启动（DX11 / D3D9 / OpenGL / 自定义）/ 自适应窗口与全屏
- **文件管理**：双面板分屏、鼠标拖拽（拖到文件夹复制、拖到外部程序打开）、提取图标、MD5 / SHA1 / SHA256、文件夹大小、复制到 / 移动到
- **原创功能**：图标检查器（多尺寸预览 + ICO/BMP 保存）、文件关联管理器（不干预 Wine 全局）
- **界面**：多语言切换（简中 / 繁中 / 英文 / 葡语 / 俄语）、收藏夹、预览面板、内存显示开关、设置持久化
- **挂载**：ISO / BIN / CUE 镜像（内置 libcdio）

### 📦 安装教程
1. 下载 `bfm-zh-x64.zip`
2. 解压后放入容器可访问目录（如 D 盘）
3. 容器内运行 `安装.bat`：自动备份原版 → 覆盖 → 自动重启 WFM
4. 还原：运行 `还原.bat` 恢复原版
5. 手动方式：将 `wfm.exe` 和 `libcdio.dll` 覆盖到 `C:\windows\`

### ⚠️ 说明
- 转区功能依赖容器是否生成对应 locale，若容器不支持会自动回退默认区域，不会崩溃
- 本分支遵循上游 GPL-3.0-or-later 协议，保留上游版权声明

---
**上游**：[The412Banner/banner-file-manager](https://github.com/The412Banner/banner-file-manager) | **本分支**：[hao728/bfm-zh](https://github.com/hao728/bfm-zh)
