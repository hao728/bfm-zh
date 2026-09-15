# BFM-Zh 中文增强版文件管理器

**BFM-Zh** 是 [Banner File Manager](https://github.com/The412Banner/banner-file-manager)（BFM）的中文汉化增强版，在 Winlator / Bannerlator / Bionic 等 Wine 容器中运行，替代容器自带的文件管理器。

本项目在保留原版全部功能的基础上，做了大量中文化、界面优化与原创功能扩展。

## ✨ 特色功能

### 🇨🇳 中文化
- 全界面简体中文，注释代码全中文化
- 内置 5 种语言，菜单栏「语言」可随时切换（简中 / 繁中 / 英文 / 葡语 / 俄语）
- 全部文本基于 Locale 框架，非硬编码替换，可后续迭代

### 🚀 启动增强
- **加速运行（清内存）**：启动前清理内存，均衡模式释放内存并显示释放量
- **激进加速**：更高内存压力压缩，适合接近设备内存上限的游戏
- **以参数启动**：一键 `-force-d3d11` / `-force-d3d9` / `-force-opengl` / 自定义参数
- **以指定区域运行**：转区启动（日文 / 简中 / 繁中 / 英文），解决 galgame、汉化补丁乱码问题
- **自适应窗口 / 全屏**：统一引擎虚拟桌面，支持自定义分辨率

### 🖥️ 界面与视图
- 大图标视图：自定义绘制，**多行文件名不截断**，支持超长文件名
- 列表 / 小图标 / 详情视图完整支持，列排序可点击
- 跟随容器明暗主题，控件全部适配
- 字体统一 11pt 清晰化，菜单栏 / 右键菜单 / 标题栏同步提升
- 磁盘容量条显示剩余 / 总容量，百分比居中
- 状态栏内存显示可开关（查看菜单）

### 📁 文件管理
- 双面板分屏：两个独立面板，支持对比、跨面板复制粘贴
- **鼠标拖拽**：文件拖到文件夹复制 / 移动，拖到外部程序打开（OLE 拖放）
- **图标检查器**（原创）：exe/dll 多尺寸图标预览（16-256px），保存 ICO / BMP
- **文件关联管理器**（原创）：为文件类型指定打开程序，仅存 BFM 注册表，不干预 Wine 全局
- 提取图标 / 计算 MD5 / SHA1 / SHA256 / 查看文本 / 计算文件夹大小
- 收藏夹：收藏常用路径，快速访问
- 预览面板：图片 / 文本快速预览
- 复制进度条带取消，大目录加载不卡顿（虚拟列表 + 缓存）

### 🔌 其他
- ISO / BIN / CUE 镜像挂载（内置 libcdio）
- 注册表右键菜单扩展支持
- 设置持久化：视图样式、排序、隐藏文件、双面板状态重启后保留

## 📦 安装

> 适用于 Winlator / Bannerlator / Bionic 等 Wine 容器（x86-64）。

**方式一：一键脚本（推荐）**
1. 下载 Release 中的 `bfm-zh-x64.zip`
2. 解压后，将整个文件夹放入容器可访问的目录（如 D 盘）
3. 在容器中运行 `安装.bat`：自动备份原版 → 覆盖安装 → 重启 WFM
4. 还原：运行 `还原.bat` 恢复原版

**方式二：手动覆盖**
1. 解压 `bfm-zh-x64.zip`
2. 将 `wfm.exe` 和 `libcdio.dll` 复制到容器 `C:\windows\` 覆盖原文件
3. 重启 WFM（关闭容器重进）

## 🛠 构建

```sh
CC=x86_64-w64-mingw32-gcc
RC=x86_64-w64-mingw32-windres
INCLUDES="-I./include -I./include/libcdio"
CFLAGS="-O2 -std=c99 -DUNICODE -D_UNICODE -DCOBJMACROS -DWINVER=0x0600 -Wall"
SRCS="main theme config favorites diff content_view toolbar navbar treeview sizebar statusbar file_node file_actions input_dialog"

mkdir -p obj
for f in $SRCS; do $CC $CFLAGS $INCLUDES -c "src/$f.c" -o "obj/$f.o"; done
$RC $INCLUDES -I./res -i res/resource.rc -o obj/resource.o
$CC -o wfm.exe obj/*.o -s -lcomctl32 -lgdi32 -lole32 -loleaut32 -luuid -luxtheme -lshlwapi -lcrypt32 ./libcdio.dll -Wl,--subsystem,windows
```

GitHub Actions 已配置自动构建，每次提交自动出包。

## 📄 开源协议

本项目基于 [GPL-3.0-or-later](LICENSE) 发布。

- 上游：[The412Banner/banner-file-manager](https://github.com/The412Banner/banner-file-manager)（GPL-3.0-or-later）
- 原始 WFM：[brunodev85/wfm](https://github.com/brunodev85/wfm)（MIT，版权声明见 [LICENSE.WFM](LICENSE.WFM)）
- 内置 libcdio（GPL-3.0-or-later）

本仓库为上游 BFM 的汉化与功能增强分支，所有新增代码遵循 GPL-3.0-or-later，使用请保留上游版权声明。

## 👤 维护

本分支由 [hao728](https://github.com/hao728) 维护。
