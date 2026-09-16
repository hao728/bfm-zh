#include "main.h"
#include <olectl.h>   // OleLoadPicturePath for image preview
#include <ocidl.h>    // IPicture interface

static const wchar_t mainWndClass[] = L"WFM-MainWnd";

extern struct FileNode* currPathFileNode;
extern HWND hwndNavbar;
extern HWND hwndSizebar;
extern HWND hwndStatusbar;
extern void onMenuItemProcessManagerClick(void);
extern HWND hwndToolbar;
extern HWND hwndTreeview;

HINSTANCE globalHInstance = NULL;
HWND hwndMain = NULL;
HWND hwndTabs = NULL;
HWND hwndPreview = NULL;
bool previewOn = true;  // 默认开启预览窗格，提升实用性
struct LC_STR lc_str = {0};
void resizeControls(void);  // forward: tab visibility changes trigger relayout
void previewUpdate(void);   // forward: refresh preview pane for current selection

// --- Tab bar ---------------------------------------------------------------------------
#define MAX_TABS 16
struct TabState {
    wchar_t path[MAX_PATH];
};
static struct TabState tabStates[MAX_TABS];
static int tabCount = 0;
static int activeTab = 0;

static void tabsSaveCurrent(void) {
    if (activeTab < 0 || activeTab >= tabCount) return;
    if (currPathFileNode) {
        getFileNodePath(currPathFileNode, tabStates[activeTab].path);
    }
}

static void tabsRestore(int idx) {
    if (idx < 0 || idx >= tabCount) return;
    activeTab = idx;
    if (tabStates[idx].path[0]) {
        navigateToPath(tabStates[idx].path);
    }
}

// 仅打开一个标签页时隐藏标签栏（冗余杂乱）；

// 创建第二个标签页后再次显示它。

static void tabsUpdateVisibility(void) {
    if (!hwndTabs) return;
    ShowWindow(hwndTabs, tabCount > 1 ? SW_SHOW : SW_HIDE);
}

static void tabsAdd(const wchar_t* path) {
    if (tabCount >= MAX_TABS) return;
    if (path) wcscpy_s(tabStates[tabCount].path, MAX_PATH, path);
    else tabStates[tabCount].path[0] = L'\0';
    wchar_t label[64] = {0};
    if (path && path[0]) {
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        wcscpy_s(label, 64, name);
    } else {
        wcscpy_s(label, 64, L"此电脑");
    }
    TCITEMW ti = {0};
    ti.mask = TCIF_TEXT;
    ti.pszText = label;
    TabCtrl_InsertItem(hwndTabs, tabCount, &ti);
    tabCount++;
    TabCtrl_SetCurSel(hwndTabs, tabCount - 1);
    activeTab = tabCount - 1;
    tabsUpdateVisibility();
}

static void tabsClose(int idx) {
    if (idx < 0 || idx >= tabCount || tabCount <= 1) return;
    tabsSaveCurrent();
    TabCtrl_DeleteItem(hwndTabs, idx);
    for (int i = idx; i < tabCount - 1; i++) {
        tabStates[i] = tabStates[i + 1];
    }
    tabCount--;
    if (activeTab >= tabCount) activeTab = tabCount - 1;
    TabCtrl_SetCurSel(hwndTabs, activeTab);
    tabsUpdateVisibility();
    tabsRestore(activeTab);
}

// 每次导航后调用：将当前路径持久化到活动标签页，并且

// 刷新其标签，使标签栏反映用户所在位置。

static void tabsSyncCurrent(const wchar_t* path) {
    if (!hwndTabs || activeTab < 0 || activeTab >= tabCount) return;
    if (path) wcscpy_s(tabStates[activeTab].path, MAX_PATH, path);
    wchar_t label[64] = {0};
    if (path && path[0]) {
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        if (!name[0]) {
            // 驱动器根如 "C:\"——显示 "C:"

            wcsncpy_s(label, 64, path, 2);
        } else {
            wcscpy_s(label, 64, name);
        }
    } else {
        wcscpy_s(label, 64, L"此电脑");
    }
    TCITEMW ti = {0};
    ti.mask = TCIF_TEXT;
    ti.pszText = label;
    TabCtrl_SetItem(hwndTabs, activeTab, &ti);
}

// 公共标签 API，供键盘快捷键（Ctrl+T / Ctrl+W）和菜单使用。

void tabNew(void) {
    if (!hwndTabs || tabCount >= MAX_TABS) return;
    tabsSaveCurrent();
    wchar_t cur[MAX_PATH] = {0};
    if (currPathFileNode) getFileNodePath(currPathFileNode, cur);
    tabsAdd(cur[0] ? cur : NULL);
    if (cur[0]) navigateToPath(cur);
    resizeControls();
}

void tabCloseActive(void) {
    if (!hwndTabs || tabCount <= 1) return;
    int sel = TabCtrl_GetCurSel(hwndTabs);
    if (sel < 0) sel = activeTab;
    tabsClose(sel);
    resizeControls();
}

// --- Dual-pane layout ------------------------------------------------------------------
#define PANE_FRAME 2
static RECT paneCell[2] = {0};
static HMENU hViewMenu = NULL;
static HBRUSH paneLabelActiveBrush = NULL;
static HBRUSH paneLabelInactiveBrush = NULL;
static bool paneLabelInactiveDark = false;

void cvInvalidatePaneFrames(void) {
    for (int i = 0; i < 2; i++) InvalidateRect(hwndMain, &paneCell[i], TRUE);
}

// 主题函数位于 theme.c（亮色/暗色/自定义、强调色、悬停/交替行）。

// 配置持久化在 config.c 中（基于注册表的 cfgGet/cfgSet）。


// --- Dark non-client scrollbars -------------------------------------------------------
// Wine 将窗口自身的（非客户区）滚动条绘为经典浅色 3D 外观，无论

// 无论容器主题是什么，因此每个列表视图/树视图最终都有一个白色

// 沿右边缘和底部绘制。没有消息可以重新着色它们

// （WM_CTLCOLORSCROLLBAR 仅适用于独立滚动条控件），因此我们重绘它们

// 我们自己在窗口 DC 上绘制——与列标题使用相同的自绘方法，

// 状态栏和导航栏按钮。亮色模式留给系统处理。

#define SB_TROUGH_DARK RGB(38, 38, 38)
#define SB_THUMB_DARK  RGB(95, 95, 95)
#define SB_ARROW_DARK  RGB(205, 205, 205)

enum { SB_ARROW_UP, SB_ARROW_DOWN, SB_ARROW_LEFT, SB_ARROW_RIGHT };

static void fillRect(HDC hdc, const RECT* rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, (RECT*)rc, brush);
    DeleteObject(brush);
}

static void drawScrollArrow(HDC hdc, const RECT* rc, int dir, COLORREF color) {
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int w = rc->right - rc->left, h = rc->bottom - rc->top;
    int s = ((w < h ? w : h) / 5);
    if (s < 2) s = 2;

    POINT p[3];
    switch (dir) {
        case SB_ARROW_UP:    p[0] = (POINT){cx, cy - s}; p[1] = (POINT){cx - s, cy + s}; p[2] = (POINT){cx + s, cy + s}; break;
        case SB_ARROW_DOWN:  p[0] = (POINT){cx, cy + s}; p[1] = (POINT){cx - s, cy - s}; p[2] = (POINT){cx + s, cy - s}; break;
        case SB_ARROW_LEFT:  p[0] = (POINT){cx - s, cy}; p[1] = (POINT){cx + s, cy - s}; p[2] = (POINT){cx + s, cy + s}; break;
        default:             p[0] = (POINT){cx + s, cy}; p[1] = (POINT){cx - s, cy - s}; p[2] = (POINT){cx - s, cy + s}; break;
    }

    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Polygon(hdc, p, 3);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// 将一个滚动条绘为深色。通过 `out` 返回其矩形（窗口坐标），以便调用者可以

// 填充水平滚动条和垂直滚动条交汇处的角块。

static void paintOneScrollbar(HWND hwnd, HDC hdc, POINT org, LONG objid, bool vertical, RECT* out) {
    SCROLLBARINFO sbi = {0};
    sbi.cbSize = sizeof(SCROLLBARINFO);
    if (!GetScrollBarInfo(hwnd, objid, &sbi)) return;
    if (sbi.rgstate[0] & STATE_SYSTEM_INVISIBLE) return;

    RECT rc = sbi.rcScrollBar;              // screen coords -> window coords
    OffsetRect(&rc, -org.x, -org.y);
    if (IsRectEmpty(&rc)) return;

    fillRect(hdc, &rc, SB_TROUGH_DARK);

    int btn = sbi.dxyLineButton;
    int extent = vertical ? (rc.bottom - rc.top) : (rc.right - rc.left);
    if (btn * 2 > extent) btn = extent / 2;

    // 两端的箭头按钮。

    if (btn > 0) {
        RECT a = rc, b = rc;
        if (vertical) { a.bottom = a.top + btn; b.top = b.bottom - btn; }
        else          { a.right = a.left + btn; b.left = b.right - btn; }
        drawScrollArrow(hdc, &a, vertical ? SB_ARROW_UP : SB_ARROW_LEFT, SB_ARROW_DARK);
        drawScrollArrow(hdc, &b, vertical ? SB_ARROW_DOWN : SB_ARROW_RIGHT, SB_ARROW_DARK);
    }

    // 拇指。xyThumbTop/Bottom 是从 rcScrollBar 开始的偏移量；如果 Wine 报告

    // 没有可用的，回退到从滚动范围计算。

    int thumbStart = sbi.xyThumbTop, thumbEnd = sbi.xyThumbBottom;
    if (thumbEnd <= thumbStart) {
        SCROLLINFO si = {0};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_ALL;
        if (GetScrollInfo(hwnd, vertical ? SB_VERT : SB_HORZ, &si) && si.nMax > si.nMin) {
            int range = si.nMax - si.nMin + 1;
            int track = extent - btn * 2;
            int page = si.nPage > 0 ? (int)si.nPage : 1;
            int size = (int)((double)track * page / range);
            if (size < 12) size = 12;
            if (size > track) size = track;
            int span = range - page;
            int offset = span > 0 ? (int)((double)(track - size) * (si.nPos - si.nMin) / span) : 0;
            thumbStart = btn + offset;
            thumbEnd = thumbStart + size;
        }
    }

    if (thumbEnd > thumbStart) {
        RECT t = rc;
        if (vertical) { t.top = rc.top + thumbStart; t.bottom = rc.top + thumbEnd; t.left += 1; t.right -= 1; }
        else          { t.left = rc.left + thumbStart; t.right = rc.left + thumbEnd; t.top += 1; t.bottom -= 1; }
        IntersectRect(&t, &t, &rc);
        if (!IsRectEmpty(&t)) fillRect(hdc, &t, SB_THUMB_DARK);
    }

    *out = rc;
}

void themePaintScrollbars(HWND hwnd) {
    if (!isDarkMode()) return;

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    bool hasV = (style & WS_VSCROLL) != 0;
    bool hasH = (style & WS_HSCROLL) != 0;
    if (!hasV && !hasH) return;

    HDC hdc = GetWindowDC(hwnd);
    if (!hdc) return;

    RECT wr;
    GetWindowRect(hwnd, &wr);
    POINT org = {wr.left, wr.top};

    RECT vrc = {0}, hrc = {0};
    if (hasV) paintOneScrollbar(hwnd, hdc, org, OBJID_VSCROLL, true, &vrc);
    if (hasH) paintOneScrollbar(hwnd, hdc, org, OBJID_HSCROLL, false, &hrc);

    // 两个滚动条交汇处的死角也被 Wine 绘为浅色。

    if (!IsRectEmpty(&vrc) && !IsRectEmpty(&hrc)) {
        RECT corner = {vrc.left, hrc.top, vrc.right, hrc.bottom};
        fillRect(hdc, &corner, SB_TROUGH_DARK);
    }

    ReleaseDC(hwnd, hdc);
}

// 使用滚动条时，Wine 运行自己的模态消息循环（track_scroll_bar）

// 并在每次鼠标移动和自动重复计时时将滚动条重绘为浅色——因此按下

// 槽在按住期间保持白色，因为我们的重绘仅在以下之后运行一次

// 循环返回。该循环确实向窗口分发普通 WM_TIMER 消息，因此我们驱动

// 在拖动跟踪期间启动快速重绘定时器，赢得重绘竞争。

#define THEME_SB_TIMER_ID 0x7BF1
#define THEME_SB_TIMER_MS 10
#define BOOST_TIP_TIMER_ID 0x7BF2
#define BOOST_TIP_TIMER_MS 3000

// 在原始窗口过程之前调用。如果消息已完全处理则返回 true。

bool themeScrollbarsHookBefore(HWND hwnd, UINT msg, WPARAM wParam) {
    if (msg == WM_TIMER && wParam == THEME_SB_TIMER_ID) {
        themePaintScrollbars(hwnd);
        return true;
    }
    if ((msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK) && isDarkMode()) {
        SetTimer(hwnd, THEME_SB_TIMER_ID, THEME_SB_TIMER_MS, NULL);
    }
    return false;
}

// 在原始窗口过程之后调用（模态跟踪循环在那里返回）。

void themeScrollbarsHookAfter(HWND hwnd, UINT msg) {
    if (msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK ||
        msg == WM_NCLBUTTONUP || msg == WM_CAPTURECHANGED) {
        KillTimer(hwnd, THEME_SB_TIMER_ID);
    }
    if (themeScrollbarsNeedRepaint(msg)) themePaintScrollbars(hwnd);
}

// Wine 从控件自身处理内部重绘滚动条（SetScrollInfo 绘制

// 立即），因此仅在 WM_NCPAINT 上重绘不够——子类化会调用

// 在原始过程之后为任何可能移动或调整

// 滚动条。保留白名单机制，避免高频查询消息触发重绘。

bool themeScrollbarsNeedRepaint(UINT msg) {
    switch (msg) {
        case WM_NCPAINT:
        case WM_PAINT:
        case WM_SIZE:
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN:
        case WM_LBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONUP:
        case WM_NCMOUSEMOVE:
        case WM_NCMOUSELEAVE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_STYLECHANGED:
        case WM_SYSCOLORCHANGE:
        case LVM_SETITEMCOUNT:
        case LVM_DELETEALLITEMS:
        case LVM_INSERTITEM:
        case LVM_DELETEITEM:
        case LVM_SETCOLUMNWIDTH:
        case LVM_ENSUREVISIBLE:
        case LVM_SCROLL:
        case LVM_REDRAWITEMS:
        case LVM_UPDATE:
        case TVM_EXPAND:
        case TVM_INSERTITEM:
        case TVM_DELETEITEM:
        case TVM_SELECTITEM:
            return true;
        default:
            return false;
    }
}

// CreateFontW 在字体缺失时从不返回 NULL——它会静默替换——

// 因此请求的名称不能证明字体存在。我们验证两件事：

//   (a) GetTextFace 返回的字体名与我们请求的一致；

//   (b) 该字体确实包含常用中日韩字符（U+6587 文）的字形，

//       这是中文是否会渲染而非方块的真正测试。

static bool fontFaceUsable(const wchar_t* face, bool requireCJK) {
    HFONT hf = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    if (!hf) return false;
    HDC hdc = GetDC(NULL);
    HFONT old = (HFONT)SelectObject(hdc, hf);
    wchar_t actual[LF_FACESIZE] = {0};
    GetTextFaceW(hdc, LF_FACESIZE, actual);
    bool nameOk = (_wcsicmp(actual, face) == 0);
    bool cjkOk = true;
    if (requireCJK) {
        WORD gi = 0;
        wchar_t probe = 0x6587;  // 文
        cjkOk = (GetGlyphIndicesW(hdc, &probe, 1, &gi, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR)
                && gi != 0 && gi != 0xFFFF;
    }
    SelectObject(hdc, old);
    ReleaseDC(NULL, hdc);
    DeleteObject(hf);
    return nameOk && cjkOk;
}

// EnumFontFamiliesEx 回调：记录第一个已安装的 TrueType 字体，

// 能渲染中日韩。这会发现容器实际自带的任何字体

// （Droid Sans Fallback、思源黑体等），无需我们猜测其名称。

static wchar_t g_enumCJKFace[LF_FACESIZE] = {0};
static INT CALLBACK enumCJKFontProc(const LOGFONTW* lf, const TEXTMETRICW* tm,
                                    DWORD fontType, LPARAM lParam) {
    (void)tm; (void)lParam;
    if (!(fontType & TRUETYPE_FONTTYPE)) return 1;  // skip raster/device fonts
    if (g_enumCJKFace[0]) return 0;                 // already found
    if (fontFaceUsable(lf->lfFaceName, true))
        wcscpy_s(g_enumCJKFace, LF_FACESIZE, lf->lfFaceName);
    return g_enumCJKFace[0] ? 0 : 1;
}

// 共享 UI 字体。我们不信任 SystemParametersInfo（其 lfMessageFont 是一个

// 逻辑名称如 "MS Shell Dlg"，Wine 反正会替换）。相反我们

// 选择一个真实的、已安装的、支持中日韩的 TrueType 字体：首先是首选名称，

// 然后是系统枚举的任何内容。DPI 感知高度使其在以下设备上保持清晰

// 高密度手机面板。

static HFONT uiFont = NULL;
static int g_fontSizePt = 11;  // 用户可配置的UI字体大小（pt），默认11

// 从注册表加载字体大小设置
static void loadFontSize(void) {
    HKEY hkey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD val = 0, sz = sizeof(val);
        if (RegQueryValueExW(hkey, L"FontSize", NULL, NULL, (BYTE*)&val, &sz) == ERROR_SUCCESS) {
            if (val >= 8 && val <= 20) g_fontSizePt = (int)val;
        }
        RegCloseKey(hkey);
    }
}

// 保存字体大小到注册表
static void saveFontSize(int pt) {
    HKEY hkey;
    if (RegCreateKeyW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", &hkey) == ERROR_SUCCESS) {
        DWORD val = (DWORD)pt;
        RegSetValueExW(hkey, L"FontSize", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
        RegCloseKey(hkey);
    }
}

// 应用新字体大小：销毁旧字体，重新创建，更新所有控件
static void boostSystemFonts(void);  // 前向声明（定义在下方）

static void applyFontSize(int pt) {
    g_fontSizePt = pt;
    saveFontSize(pt);
    // 销毁旧字体，getUIFont会重新创建
    if (uiFont) { DeleteObject(uiFont); uiFont = NULL; }
    HFONT newFont = getUIFont();
    // 更新main.c可直接访问的控件
    HWND ctrls[] = { hwndMain, hwndTabs, hwndPreview, hwndStatusbar, hwndTreeview,
                     hwndToolbar, hwndNavbar };
    for (int i = 0; i < (int)(sizeof(ctrls)/sizeof(ctrls[0])); i++) {
        if (ctrls[i]) SendMessage(ctrls[i], WM_SETFONT, (WPARAM)newFont, TRUE);
    }
    // 面板列表/路径标签（通过content_view.c的函数访问static变量）
    cvApplyFont(newFont);
    // 地址栏/搜索框（通过navbar.c的函数访问static变量）
    navbarApplyFont(newFont);
    // 同步提升系统菜单/标题字体
    boostSystemFonts();
    // 强制重绘
    InvalidateRect(hwndMain, NULL, TRUE);
    DrawMenuBar(hwndMain);
}

HFONT getUIFont(void) {
    if (!uiFont) {
        HDC screen = GetDC(NULL);
        int dpiY = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(NULL, screen);
        if (dpiY <= 0) dpiY = 96;
        // 使用用户配置的字体大小（默认11pt），钳制防止异常

        int height = -MulDiv(g_fontSizePt, dpiY, 72);
        if (height > -10) height = -10;
        if (height < -28) height = -28;

        wchar_t chosen[LF_FACESIZE] = {0};

        // 1. 首选字体，需验证存在且包含中日韩字形。

        static const wchar_t* preferred[] = {
            L"Microsoft YaHei", L"微软雅黑", L"Noto Sans CJK SC",
            L"Source Han Sans SC", L"WenQuanYi Micro Hei",
            L"Droid Sans Fallback", NULL
        };
        for (int i = 0; preferred[i] && !chosen[0]; i++) {
            if (fontFaceUsable(preferred[i], true))
                wcscpy_s(chosen, LF_FACESIZE, preferred[i]);
        }

        // 2. 枚举所有已安装字体，取第一个支持中日韩的字体。

        if (!chosen[0]) {
            LOGFONTW lf = {0};
            lf.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"");
            HDC hdc = GetDC(NULL);
            EnumFontFamiliesExW(hdc, &lf, enumCJKFontProc, 0, 0);
            ReleaseDC(NULL, hdc);
            if (g_enumCJKFace[0])
                wcscpy_s(chosen, LF_FACESIZE, g_enumCJKFace);
        }

        // 3. 创建选中的中日韩字体；如果没有，用 Tahoma（Wine 始终自带）。

        // OUT_TT_PRECIS 强制映射器选择 TrueType 字体（光栅 .fon

        // 字体缩放效果差）；CLEARTYPE_QUALITY 在以下设备上提供亚像素渲染

        // LCD 面板，在不支持的地方回退到灰度抗锯齿。

        if (chosen[0])
            uiFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, chosen);
        if (!uiFont && fontFaceUsable(L"Tahoma", false))
            uiFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
        // 4. 最后手段。

        if (!uiFont)
            uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    return uiFont;
}

// 提升系统菜单/标题字体，解决Bionic/Wine下菜单栏、右键菜单、标题栏字体过小模糊的问题。
// 原因：主区域控件使用 getUIFont()（11pt）而菜单/标题栏使用系统默认字体（Bionic下偏小）。
// 影响范围：仅修改 NONCLIENTMETRICS 的菜单与标题字体高度，字体名保持系统默认。
// 回滚：删除本函数调用即可恢复系统默认菜单字体。
static void boostSystemFonts(void) {
    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) return;
    HDC screen = GetDC(NULL);
    int dpiY = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    if (dpiY <= 0) dpiY = 96;
    int h = -MulDiv(g_fontSizePt, dpiY, 72);  // 与 getUIFont 一致的用户配置字体大小
    if (h > -10) h = -10;
    if (h < -28) h = -28;
    ncm.lfMenuFont.lfHeight = h;
    ncm.lfMenuFont.lfWeight = FW_NORMAL;
    ncm.lfCaptionFont.lfHeight = h;
    ncm.lfCaptionFont.lfWeight = FW_NORMAL;
    SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof(ncm), &ncm, SPIF_SENDCHANGE);
}

// ---------- Memory-boost progress tip ----------
// 一个小型居中的置顶弹窗，让用户看到内存清理阶段

// 正在运行。一旦启动的游戏运行后，仅状态栏文字很容易被忽略

// 窗口覆盖主窗口。

static HWND hBoostTip = NULL;
static void hideBoostTip(void);

// Boost tip 子类化窗口过程：支持点击关闭
static LRESULT CALLBACK boostTipSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (msg == WM_LBUTTONDOWN) {
        hideBoostTip();
        return 0;
    }
    if (msg == WM_TIMER) {
        hideBoostTip();
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void showBoostTip(const wchar_t* text) {
    hideBoostTip();
    RECT rc;
    GetClientRect(hwndMain, &rc);
    int w = 420, h = 90;
    int x = (rc.right - w) / 2;
    int y = (rc.bottom - h) / 2;
    // 使用 STATIC 类确保文字正常显示，通过子类化添加点击关闭功能
    hBoostTip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", text,
        WS_POPUP | WS_VISIBLE | SS_CENTER | WS_BORDER, x, y, w, h,
        hwndMain, NULL, globalHInstance, NULL);
    if (hBoostTip) {
        HFONT f = getUIFont();
        if (f) SendMessageW(hBoostTip, WM_SETFONT, (WPARAM)f, TRUE);
        // 子类化：添加点击关闭功能
        SetWindowSubclass(hBoostTip, boostTipSubclassProc, 1, 0);
        // 设置5秒自动关闭定时器，确保游戏启动失败时也能自动隐藏
        SetTimer(hBoostTip, 1, 5000, NULL);
    }
}

static void hideBoostTip(void) {
    if (hBoostTip) {
        DestroyWindow(hBoostTip);
        hBoostTip = NULL;
    }
}

void GetWindowRectInParent(HWND hwnd, RECT* rect) {
    GetWindowRect(hwnd, rect);
    MapWindowPoints(HWND_DESKTOP, GetParent(hwnd), (LPPOINT)rect, 2);
}

INT_PTR CALLBACK AboutDialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NOTIFY: {
            // SysLink 单击：在默认浏览器中打开仓库 URL

            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->idFrom == IDC_APP_REPO && pnmh->code == NM_CLICK) {
                PNMLINK pNMLink = (PNMLINK)lParam;
                ShellExecuteW(hwndDlg, L"open", pNMLink->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
            }
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDOK:
                case IDCANCEL: {
                  EndDialog(hwndDlg, (INT_PTR) LOWORD(wParam));
                  return (INT_PTR) TRUE;
                }
            }
            break;
        }
        case WM_INITDIALOG: {
            RECT rect, rect1;
            GetWindowRect(GetParent(hwndDlg), &rect);
            GetClientRect(hwndDlg, &rect1);
            SetWindowPos(hwndDlg, NULL, (rect.right + rect.left) / 2 - (rect1.right - rect1.left) / 2, (rect.bottom + rect.top) / 2 - (rect1.bottom - rect1.top) / 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE);

            SetWindowText(hwndDlg, lc_str.about);
            SetWindowText(GetDlgItem(hwndDlg, IDOK), lc_str.ok);
            SetWindowText(GetDlgItem(hwndDlg, IDC_APP_NAME), lc_str.app_name);
            SetWindowText(GetDlgItem(hwndDlg, IDC_APP_VERSION), lc_str.app_version);
            SetWindowText(GetDlgItem(hwndDlg, IDC_APP_DEV_NAME), lc_str.app_dev_name);
            SetWindowText(GetDlgItem(hwndDlg, IDC_APP_MODIFIER), lc_str.modifier);
            return (INT_PTR)TRUE;
        }
    }

    return (INT_PTR)FALSE;
}

// 在 content_view.c 中定义；语言切换后刷新列标题和状态栏

extern void cvRefreshLanguage(void);
extern void onMenuItemLoadISOImageClick(void);
extern void onMenuItemUnloadISOImageClick(void);
extern void hideExtractProgress(void);
extern wchar_t g_sevenZipLastOpName[];
extern HICON getExeIconEnhanced(const wchar_t* exePath);

// 前向声明：createMainMenu 在后面定义，但被 mainMenuCommand 调用

static void createMainMenu();

// 切换主题后刷新所有界面元素（必须在 createMainMenu 声明之后定义）
static void applyThemeAndRefresh(void) {
    // 重建菜单（勾选当前主题）
    createMainMenu();
    // 刷新内容区（主题颜色变化）
    cvRefreshLanguage();
    // 更新 TreeView 背景和文字颜色（静态设置，切换主题后需重新赋值）
    if (hwndTreeview) {
        TreeView_SetBkColor(hwndTreeview, themeFieldBg());
        TreeView_SetTextColor(hwndTreeview, themeFieldText());
        RedrawWindow(hwndTreeview, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    // 强制工具栏重绘（WM_ERASEBKGND 重画主题色背景）
    if (hwndToolbar) {
        RedrawWindow(hwndToolbar, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    // 重绘标题栏、工具栏、状态栏、导航栏
    InvalidateRect(hwndMain, NULL, TRUE);
    DrawMenuBar(hwndMain);
    RedrawWindow(hwndMain, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void mainMenuCommand(WPARAM wParam) {
    switch (LOWORD(wParam)) {
        case ID_EDIT_CUT:
            onMenuItemCutClick();
            break;
        case ID_EDIT_COPY:
            onMenuItemCopyClick();
            break;
        case ID_EDIT_PASTE:
            onMenuItemPasteClick();
            break;
        case ID_EDIT_PASTE_SHORTCUT:
            onMenuItemPasteShortcutClick();
            break;
        case ID_EDIT_SELECT_ALL:
            onMenuItemSelectAllClick();
            break;                  
        case ID_HELP_ABOUT:
            DialogBox(globalHInstance, MAKEINTRESOURCE(IDD_ABOUT), hwndMain, &AboutDialogProc);
            break;
        case ID_FILE_EXIT:
            DestroyWindow(hwndMain);
            break;
        case ID_LANG_EN:
            loadLCStrings(L"en-US"); createMainMenu(); cvRefreshLanguage(); break;
        case ID_LANG_ZH:
            loadLCStrings(L"zh-CN"); createMainMenu(); cvRefreshLanguage(); break;
        case ID_LANG_PT:
            loadLCStrings(L"pt-BR"); createMainMenu(); cvRefreshLanguage(); break;
        case ID_LANG_RU:
            loadLCStrings(L"ru-RU"); createMainMenu(); cvRefreshLanguage(); break;
        case ID_MOUNT_ISO:
            onMenuItemLoadISOImageClick(); break;
        case ID_UNMOUNT_ISO:
            onMenuItemUnloadISOImageClick(); break;
        case ID_VIEW_LARGEICONS:
            setViewStyle(STYLE_LARGE_ICON);
            break;
        case ID_VIEW_SMALLICONS:
            setViewStyle(STYLE_SMALL_ICON);
            break;
        case ID_VIEW_LIST:
            setViewStyle(STYLE_LIST);
            break;
        case ID_VIEW_DETAILS:
            setViewStyle(STYLE_DETAILS);
            break;
        case ID_VIEW_SPLIT:
            cvToggleSplit();
            break;
        case ID_VIEW_PREVIEW:
            previewOn = !previewOn;
            if (hViewMenu) CheckMenuItem(hViewMenu, ID_VIEW_PREVIEW, MF_BYCOMMAND | (previewOn ? MF_CHECKED : MF_UNCHECKED));
            resizeControls();
            if (previewOn) previewUpdate();
            break;
        case ID_VIEW_HIDDEN:
            showHiddenFiles = !showHiddenFiles;
            if (hViewMenu) CheckMenuItem(hViewMenu, ID_VIEW_HIDDEN, MF_BYCOMMAND | (showHiddenFiles ? MF_CHECKED : MF_UNCHECKED));
            // 保存显示隐藏文件设置到注册表
            {
                HKEY hkeyHidden;
                if (RegCreateKeyW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", &hkeyHidden) == ERROR_SUCCESS) {
                    DWORD val = showHiddenFiles ? 1 : 0;
                    RegSetValueExW(hkeyHidden, L"ShowHidden", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
                    RegCloseKey(hkeyHidden);
                }
            }
            navigateRefresh();
            break;
        case ID_VIEW_MEMORY:
            cvToggleMemoryDisplay();
            if (hViewMenu) CheckMenuItem(hViewMenu, ID_VIEW_MEMORY, MF_BYCOMMAND | (cvMemoryVisible() ? MF_CHECKED : MF_UNCHECKED));
            break;
        case ID_VIEW_FONT_SMALL:  applyFontSize(9);  createMainMenu(); break;
        case ID_VIEW_FONT_MEDIUM: applyFontSize(11); createMainMenu(); break;
        case ID_VIEW_FONT_LARGE:  applyFontSize(13); createMainMenu(); break;
        case ID_VIEW_FONT_XLARGE: applyFontSize(15); createMainMenu(); break;
        case ID_SORT_NAME: cvSetSort(0); break;
        case ID_SORT_TYPE: cvSetSort(1); break;
        case ID_SORT_SIZE: cvSetSort(2); break;
        case ID_SORT_DATE: cvSetSort(3); break;
        case ID_NAV_BACK: navGoBack(); break;
        case ID_NAV_FORWARD: navGoForward(); break;
        case ID_NAV_RECENT: recentMenu(); break;
        case ID_VIEW_GAME_MODE: onMenuItemGameModeClick(); break;
        case ID_VIEW_COMPARE: onMenuItemComparePanesClick(); break;
        case ID_VIEW_THEME_LIGHT: themeSetMode(THEME_LIGHT); applyThemeAndRefresh(); break;
        case ID_VIEW_THEME_DARK: themeSetMode(THEME_DARK); applyThemeAndRefresh(); break;
        case ID_VIEW_THEME_CUSTOM: themeSetMode(THEME_CUSTOM); applyThemeAndRefresh(); break;
        case ID_TAB_NEW: tabNew(); break;
        case ID_TAB_CLOSE: tabCloseActive(); break;
        case ID_TOOL_NOTEPAD: ShellExecuteW(NULL, L"open", L"notepad.exe", NULL, NULL, SW_SHOW); break;
        case ID_TOOL_CMD: ShellExecuteW(NULL, L"open", L"cmd.exe", NULL, NULL, SW_SHOW); break;
        case ID_TOOL_REGEDIT: ShellExecuteW(NULL, L"open", L"regedit.exe", NULL, NULL, SW_SHOW); break;
        case ID_TOOL_TASKMGR: onMenuItemProcessManagerClick(); break;
        case ID_TOOL_LAUNCHER: onMenuItemLauncherChooseClick(); break;
    }
}

// --- Preview pane -----------------------------------------------------------------------
static const wchar_t previewWndClass[] = L"WFM-PreviewPane";
static const wchar_t zoomWndClass[] = L"WFM-PreviewZoom";
static HWND hwndZoom = NULL;
static wchar_t previewPath[MAX_PATH] = {0};
static IPicture* previewPic = NULL;
static HICON previewIcon = NULL;
static HBITMAP previewCustomBmp = NULL;  // 自定义图标位图（与主视图图标识别联动）
static wchar_t previewTypeName[64] = {0};
static wchar_t previewSizeStr[32] = {0};
static wchar_t previewDateStr[64] = {0};
static wchar_t previewText[2048] = {0};  // first lines of text files

static bool isImageExt(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    const wchar_t* ext = dot + 1;
    return !_wcsicmp(ext, L"jpg") || !_wcsicmp(ext, L"jpeg") ||
           !_wcsicmp(ext, L"png") || !_wcsicmp(ext, L"gif") ||
           !_wcsicmp(ext, L"bmp") || !_wcsicmp(ext, L"ico") ||
           !_wcsicmp(ext, L"tif") || !_wcsicmp(ext, L"tiff");
}

static bool isTextExt(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    const wchar_t* ext = dot + 1;
    return !_wcsicmp(ext, L"txt") || !_wcsicmp(ext, L"log") ||
           !_wcsicmp(ext, L"ini") || !_wcsicmp(ext, L"bat") ||
           !_wcsicmp(ext, L"cmd") || !_wcsicmp(ext, L"reg") ||
           !_wcsicmp(ext, L"md") || !_wcsicmp(ext, L"json") ||
           !_wcsicmp(ext, L"xml") || !_wcsicmp(ext, L"csv") ||
           !_wcsicmp(ext, L"conf") || !_wcsicmp(ext, L"cfg") ||
           !_wcsicmp(ext, L"sh") || !_wcsicmp(ext, L"py") ||
           !_wcsicmp(ext, L"c") || !_wcsicmp(ext, L"h") ||
           !_wcsicmp(ext, L"cpp") || !_wcsicmp(ext, L"js") ||
           !_wcsicmp(ext, L"html") || !_wcsicmp(ext, L"css");
}

static void loadPreviewText(const wchar_t* path) {
    previewText[0] = L'\0';
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    char buf[8192];
    DWORD read = 0;
    if (!ReadFile(hFile, buf, sizeof(buf) - 1, &read, NULL) || read == 0) {
        CloseHandle(hFile);
        return;
    }
    buf[read] = 0;
    CloseHandle(hFile);
    // 如果存在 UTF-8 BOM 则跳过。

    int skip = (read >= 3 && (unsigned char)buf[0] == 0xEF &&
                (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
    // 首先尝试 UTF-8；回退到 ANSI。

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                  buf + skip, (int)(read - skip), NULL, 0);
    if (len <= 0) {
        len = MultiByteToWideChar(CP_ACP, 0, buf + skip, (int)(read - skip), NULL, 0);
        if (len <= 0) return;
        MultiByteToWideChar(CP_ACP, 0, buf + skip, (int)(read - skip),
                            previewText, 2047);
    } else {
        MultiByteToWideChar(CP_UTF8, 0, buf + skip, (int)(read - skip),
                            previewText, 2047);
    }
    previewText[2047] = L'\0';
    // 将除制表符/换行符外的控制字符替换为空格。

    for (wchar_t* p = previewText; *p; p++) {
        if (*p < L' ' && *p != L'\t' && *p != L'\n' && *p != L'\r') *p = L' ';
    }
}

void previewUpdate(void) {
    if (!previewOn || !hwndPreview) return;
    // 释放先前的资源。

    if (previewPic) { previewPic->lpVtbl->Release(previewPic); previewPic = NULL; }
    if (previewIcon) { DestroyIcon(previewIcon); previewIcon = NULL; }
    if (previewCustomBmp) { DeleteObject(previewCustomBmp); previewCustomBmp = NULL; }
    previewPath[0] = L'\0';
    previewTypeName[0] = L'\0';
    previewSizeStr[0] = L'\0';
    previewDateStr[0] = L'\0';
    previewText[0] = L'\0';

    wchar_t path[MAX_PATH] = {0};
    int ftype = -1;
    cvGetFirstSelected(path, &ftype);
    if (!path[0]) { InvalidateRect(hwndPreview, NULL, TRUE); return; }
    wcscpy_s(previewPath, MAX_PATH, path);

    // 非图像文件的大图标：优先用与主视图一致的自定义图标（图片缩略图/格式图标/exe增强），回退系统图标
    bool isExeFile = false;
    {
        const wchar_t* dot = wcsrchr(path, L'.');
        isExeFile = dot && (wcsicmp(dot, L".exe")==0 || wcsicmp(dot, L".lnk")==0);
    }
    // 先尝试自定义图标（与主视图图标识别联动，96x96）
    previewCustomBmp = cvGetFileIconBitmap(path, 96, 96);
    if (!previewCustomBmp && isExeFile) {
        previewIcon = getExeIconEnhanced(path);
    }
    if (!previewCustomBmp && !previewIcon) {
        SHFILEINFOW sfi = {0};
        if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi),
                           SHGFI_ICON | SHGFI_LARGEICON | SHGFI_TYPENAME)) {
            if (!previewIcon) previewIcon = sfi.hIcon;
            wcscpy_s(previewTypeName, 64, sfi.szTypeName);
        }
    }

    // 文件大小 + 日期。

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER sz; sz.LowPart = fad.nFileSizeLow; sz.HighPart = fad.nFileSizeHigh;
        double gb = (double)sz.QuadPart / 1073741824.0;
        double mb = (double)sz.QuadPart / 1048576.0;
        double kb = (double)sz.QuadPart / 1024.0;
        if (gb >= 1.0) swprintf_s(previewSizeStr, 32, L"%.2f GB", gb);
        else if (mb >= 1.0) swprintf_s(previewSizeStr, 32, L"%.1f MB", mb);
        else if (kb >= 1.0) swprintf_s(previewSizeStr, 32, L"%.0f KB", kb);
        else swprintf_s(previewSizeStr, 32, L"%lu bytes", (unsigned long)sz.QuadPart);
        SYSTEMTIME st, lt;
        FileTimeToSystemTime(&fad.ftLastWriteTime, &st);
        SystemTimeToTzSpecificLocalTime(NULL, &st, &lt);
        swprintf_s(previewDateStr, 64, L"%04d-%02d-%02d %02d:%02d",
                   lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute);
    }

    // 为图像文件加载图像。

    if (isImageExt(path)) {
        BSTR bstrPath = SysAllocString(path);
        if (bstrPath) {
            OleLoadPicturePath(bstrPath, NULL, 0, 0, &IID_IPicture, (void**)&previewPic);
            SysFreeString(bstrPath);
        }
    } else if (isTextExt(path)) {
        loadPreviewText(path);
    }
    InvalidateRect(hwndPreview, NULL, TRUE);
}

// 点击预览面板缩略图打开的全尺寸图像查看器。

static LRESULT CALLBACK ZoomWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(hdc, &rc, bg); DeleteObject(bg);
            if (previewPic) {
                long pw = 0, ph = 0;
                previewPic->lpVtbl->get_Width(previewPic, &pw);
                previewPic->lpVtbl->get_Height(previewPic, &ph);
                if (pw > 0 && ph > 0) {
                    // IPicture 返回 HIMETRIC（0.01mm）；转换为像素。

                    HDC scr = GetDC(NULL);
                    int iw = MulDiv(pw, GetDeviceCaps(scr, LOGPIXELSX), 2540);
                    int ih = MulDiv(ph, GetDeviceCaps(scr, LOGPIXELSY), 2540);
                    ReleaseDC(NULL, scr);
                    // 在保持宽高比的同时适应客户区。

                    double sx = (double)rc.right / (double)iw;
                    double sy = (double)rc.bottom / (double)ih;
                    double scale = sx < sy ? sx : sy;
                    if (scale > 1.0) scale = 1.0;  // do not upscale beyond native size
                    int dw = (int)(iw * scale), dh = (int)(ih * scale);
                    int dx = (rc.right - dw) / 2, dy = (rc.bottom - dh) / 2;
                    SetStretchBltMode(hdc, HALFTONE);
                    SetBrushOrgEx(hdc, 0, 0, NULL);
                    previewPic->lpVtbl->Render(previewPic, hdc, dx, dy, dw, dh,
                                              0, ph, pw, -ph, NULL);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_KEYDOWN:
            if (msg != WM_KEYDOWN || wParam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;
        case WM_KILLFOCUS:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            hwndZoom = NULL;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void openZoomWindow(void) {
    if (!previewPic) return;
    if (hwndZoom) { SetForegroundWindow(hwndZoom); return; }
    long pw = 0, ph = 0;
    previewPic->lpVtbl->get_Width(previewPic, &pw);
    previewPic->lpVtbl->get_Height(previewPic, &ph);
    HDC scrDc = GetDC(NULL);
    int cw = pw > 0 ? MulDiv(pw, GetDeviceCaps(scrDc, LOGPIXELSX), 2540) : 480;
    int ch = ph > 0 ? MulDiv(ph, GetDeviceCaps(scrDc, LOGPIXELSY), 2540) : 360;
    ReleaseDC(NULL, scrDc);
    // 考虑重叠窗口的标题栏和边框。

    cw += GetSystemMetrics(SM_CXFRAME) * 2;
    ch += GetSystemMetrics(SM_CYFRAME) * 2 + GetSystemMetrics(SM_CYCAPTION);
    // 限制为工作区的 85%，确保大图仍能适应屏幕。

    RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int maxW = (work.right - work.left) * 85 / 100;
    int maxH = (work.bottom - work.top) * 85 / 100;
    if (cw > maxW) cw = maxW;
    if (ch > maxH) ch = maxH;
    if (cw < 200) cw = 200;
    if (ch < 150) ch = 150;
    int x = work.left + ((work.right - work.left) - cw) / 2;
    int y = work.top + ((work.bottom - work.top) - ch) / 2;
    const wchar_t* name = wcsrchr(previewPath, L'\\');
    hwndZoom = CreateWindowExW(WS_EX_TOPMOST, zoomWndClass,
        name ? name + 1 : previewPath,
        WS_OVERLAPPEDWINDOW, x, y, cw, ch, hwndMain, NULL, globalHInstance, NULL);
    ShowWindow(hwndZoom, SW_SHOW);
    SetFocus(hwndZoom);
}

static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN:
            // 点击缩略图打开全尺寸查看器。

            if (previewPic) openZoomWindow();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            // 背景。

            HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(hdc, &rc, bg); DeleteObject(bg);

            int margin = 12;
            int contentW = rc.right - margin * 2;
            int y = margin;

            if (!previewPath[0]) {
                SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
                HFONT old = (HFONT)SelectObject(hdc, getUIFont());
                RECT tr = {margin, y, rc.right - margin, y + 40};
                DrawTextW(hdc, L"选择文件以预览", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, old);
                EndPaint(hwnd, &ps);
                return 0;
            }

            // 图像、文本预览或大图标。

            int mediaH = 120;
            if (previewPic) {
                long pw = 0, ph = 0;
                previewPic->lpVtbl->get_Width(previewPic, &pw);
                previewPic->lpVtbl->get_Height(previewPic, &ph);
                if (pw > 0 && ph > 0) {
                    double scale = (double)contentW / (double)pw;
                    double scaleH = (double)mediaH / (double)ph;
                    if (scaleH < scale) scale = scaleH;
                    int dw = (int)(pw * scale), dh = (int)(ph * scale);
                    int dx = margin + (contentW - dw) / 2;
                    int dy = y + (mediaH - dh) / 2;
                    // 高质量重采样，确保缩小后的图像在 Wine 下保持清晰。

                    SetStretchBltMode(hdc, HALFTONE);
                    SetBrushOrgEx(hdc, 0, 0, NULL);
                    previewPic->lpVtbl->Render(previewPic, hdc, dx, dy, dw, dh,
                                               0, ph, pw, -ph, NULL);
                }
            } else if (previewText[0]) {
                // 文本文件：在带边框的框中显示前几行。

                HBRUSH boxBg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
                RECT boxR = {margin, y, rc.right - margin, y + mediaH};
                FillRect(hdc, &boxR, boxBg); DeleteObject(boxBg);
                HPEN boxPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DFACE));
                HPEN oldPen = (HPEN)SelectObject(hdc, boxPen);
                Rectangle(hdc, boxR.left, boxR.top, boxR.right, boxR.bottom);
                SelectObject(hdc, oldPen); DeleteObject(boxPen);
                HFONT hMono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
                HFONT oldf = (HFONT)SelectObject(hdc, hMono ? hMono : getUIFont());
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                SetBkMode(hdc, TRANSPARENT);
                RECT tr = {margin + 4, y + 2, rc.right - margin - 4, y + mediaH - 2};
                DrawTextW(hdc, previewText, -1, &tr, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
                SelectObject(hdc, oldf);
                if (hMono) DeleteObject(hMono);
            } else if (previewCustomBmp) {
                // 自定义图标（与主视图图标识别联动）：居中绘制96x96
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP oldBmp = SelectObject(memDC, previewCustomBmp);
                SetStretchBltMode(hdc, HALFTONE);
                StretchBlt(hdc, margin + (contentW - 96)/2, y + (mediaH - 96)/2, 96, 96,
                           memDC, 0, 0, 96, 96, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteDC(memDC);
            } else if (previewIcon) {
                DrawIconEx(hdc, margin + (contentW - 48) / 2, y + (mediaH - 48) / 2,
                           previewIcon, 48, 48, 0, NULL, DI_NORMAL);
            }
            y += mediaH + margin;

            // 文件名（通过较大字体实现粗体效果）。

            HFONT hName = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, NULL);
            HFONT old = (HFONT)SelectObject(hdc, hName ? hName : getUIFont());
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            RECT nr = {margin, y, rc.right - margin, y + 40};
            const wchar_t* name = wcsrchr(previewPath, L'\\');
            name = name ? name + 1 : previewPath;
            DrawTextW(hdc, name, -1, &nr, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
            SelectObject(hdc, old);
            if (hName) DeleteObject(hName);
            y += 44;

            // 分隔符。

            HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DFACE));
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, margin, y, NULL); LineTo(hdc, rc.right - margin, y);
            SelectObject(hdc, oldPen); DeleteObject(pen);
            y += 10;

            // 元数据行。

            HFONT hf = getUIFont();
            old = (HFONT)SelectObject(hdc, hf);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkMode(hdc, TRANSPARENT);
            wchar_t row[128];
            int rowH = 20;
            if (previewTypeName[0]) {
                swprintf_s(row, 128, L"类型：%ls", previewTypeName);
                RECT rr = {margin, y, rc.right - margin, y + rowH};
                DrawTextW(hdc, row, -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += rowH;
            }
            if (previewSizeStr[0]) {
                swprintf_s(row, 128, L"大小：%ls", previewSizeStr);
                RECT rr = {margin, y, rc.right - margin, y + rowH};
                DrawTextW(hdc, row, -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += rowH;
            }
            if (previewDateStr[0]) {
                swprintf_s(row, 128, L"修改时间：%ls", previewDateStr);
                RECT rr = {margin, y, rc.right - margin, y + rowH};
                DrawTextW(hdc, row, -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += rowH;
            }
            // 位置。

            swprintf_s(row, 128, L"Location: %ls", previewPath);
            RECT lr = {margin, y, rc.right - margin, y + rowH * 2};
            DrawTextW(hdc, row, -1, &lr, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
            SelectObject(hdc, old);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            if (previewPic) { previewPic->lpVtbl->Release(previewPic); previewPic = NULL; }
            if (previewIcon) { DestroyIcon(previewIcon); previewIcon = NULL; }
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void resizeControls() {
    RECT rect;
    GetClientRect(hwndMain, &rect);

    RECT toolbarRect;
    RECT buttonRect;
    SendMessage(hwndToolbar, TB_GETITEMRECT, 0, (LPARAM)&buttonRect);
    SetWindowPos(hwndToolbar, NULL, 0, 0, rect.right, buttonRect.bottom + 3, SWP_NOZORDER);
    GetWindowRectInParent(hwndToolbar, &toolbarRect);

    RECT statusbarRect;
    SendMessage(hwndStatusbar, WM_SIZE, 0, 0);
    GetWindowRectInParent(hwndStatusbar, &statusbarRect);

    RECT navbarRect;
    int navbarHeight = getNavbarHeight();
    SetWindowPos(hwndNavbar, NULL, 0, toolbarRect.bottom, rect.right, navbarHeight, SWP_NOZORDER);
    GetWindowRectInParent(hwndNavbar, &navbarRect);

    // 标签栏位于导航栏下方，但仅在可见时（2+ 标签页）。带有

    // 单个标签页时它被隐藏且不占用垂直空间。

    int contentTop = navbarRect.bottom;
    if (hwndTabs && IsWindowVisible(hwndTabs)) {
        int tabsHeight = 24;
        SetWindowPos(hwndTabs, NULL, 0, navbarRect.bottom, rect.right, tabsHeight, SWP_NOZORDER);
        RECT tabsRect;
        GetWindowRectInParent(hwndTabs, &tabsRect);
        contentTop = tabsRect.bottom;
    }

    RECT treeviewRect;
    GetWindowRectInParent(hwndTreeview, &treeviewRect);
    int treeviewHeight = statusbarRect.top - contentTop;
    SetWindowPos(hwndTreeview, NULL, 0, contentTop, treeviewRect.right, treeviewHeight, SWP_NOZORDER);

    const int sizebarWidth = 5;
    SetWindowPos(hwndSizebar, NULL, treeviewRect.right, contentTop, sizebarWidth, treeviewHeight, SWP_NOZORDER);

    int contentViewX = treeviewRect.right + sizebarWidth;
    int contentY = contentTop;
    const int previewW = previewOn ? 220 : 0;
    int contentW = rect.right - contentViewX - previewW;
    int contentH = treeviewHeight;

    bool split = cvSplitOn();
    int inset = split ? PANE_FRAME : 0;

    if (split) {
        const int gap = 6;
        int half = (contentW - gap) / 2;
        paneCell[0] = (RECT){contentViewX, contentY, contentViewX + half, contentY + contentH};
        paneCell[1] = (RECT){contentViewX + half + gap, contentY, contentViewX + contentW, contentY + contentH};
    }
    else {
        paneCell[0] = (RECT){contentViewX, contentY, contentViewX + contentW, contentY + contentH};
        paneCell[1] = (RECT){0, 0, 0, 0};
    }

    int nPanes = split ? 2 : 1;
    int labelH = split ? 20 : 0;
    for (int i = 0; i < nPanes; i++) {
        RECT c = paneCell[i];
        int x = c.left + inset, y = c.top + inset;
        int w = (c.right - c.left) - 2 * inset;
        int h = (c.bottom - c.top) - 2 * inset;
        if (split) {
            SetWindowPos(cvPaneLabel(i), NULL, x, y, w, labelH, SWP_NOZORDER);
        }
        SetWindowPos(cvPaneHwnd(i), NULL, x, y + labelH, w, h - labelH, SWP_NOZORDER);
        cvFitColumns(cvPaneHwnd(i), w);
    }

    // 预览面板位于内容区域的右侧。

    if (hwndPreview) {
        SetWindowPos(hwndPreview, NULL, rect.right - previewW, contentTop, previewW, treeviewHeight, SWP_NOZORDER);
        ShowWindow(hwndPreview, previewOn ? SW_SHOW : SW_HIDE);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_USER_EXTRACT_DONE:
            hideExtractProgress();
            navigateRefresh();
            {
                wchar_t msg[512];
                if (wParam == 0) {
                    swprintf_s(msg, _countof(msg), L"%ls 完成", g_sevenZipLastOpName);
                    MessageBoxW(hwnd, msg, L"7-Zip", MB_OK | MB_ICONINFORMATION);
                } else {
                    swprintf_s(msg, _countof(msg), L"%ls 失败（退出码：%lu）", g_sevenZipLastOpName, (unsigned long)wParam);
                    MessageBoxW(hwnd, msg, L"7-Zip", MB_OK | MB_ICONERROR);
                }
            }
            break;
        case WM_USER_BOOST_START:
            setStatusbarText(lc_str.boost_working);
            showBoostTip(lc_str.boost_working);
            break;
        case WM_USER_BOOST_RESULT: {
            // wParam = 释放的内存量(MB), lParam = 模式(0均衡/1激进)
            SIZE_T freedMB = (SIZE_T)wParam;
            int mode = (int)lParam;
            wchar_t buf[256];
            if (freedMB > 0) {
                swprintf_s(buf, _countof(buf),
                    L"%ls %llu MB (%ls)",
                    lc_str.boost_done,
                    (unsigned long long)freedMB,
                    mode ? L"激进模式" : L"均衡模式");
            } else {
                swprintf_s(buf, _countof(buf),
                    L"%ls (%ls)",
                    lc_str.boost_done,
                    mode ? L"激进模式" : L"均衡模式");
            }
            setStatusbarText(buf);
            // 更新boost tip显示详细结果
            hideBoostTip();
            showBoostTip(buf);
            break;
        }
        case WM_USER_BOOST_DONE:
            // 结果已在WM_USER_BOOST_RESULT中显示，设置定时器3秒后自动隐藏
            SetTimer(hwnd, BOOST_TIP_TIMER_ID, BOOST_TIP_TIMER_MS, NULL);
            break;
        case WM_TIMER:
            if (wParam == BOOST_TIP_TIMER_ID) {
                KillTimer(hwnd, BOOST_TIP_TIMER_ID);
                hideBoostTip();
            }
            break;
        case WM_SIZE: {
            resizeControls();
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (cvSplitOn()) {
                HBRUSH accent = CreateSolidBrush(RGB(0, 120, 215));
                HBRUSH normal = CreateSolidBrush(themeFaceBg());
                for (int i = 0; i < 2; i++) {
                    // 子窗口被裁剪（WS_CLIPCHILDREN），因此这里只绘制

                    // 每个列表视图周围的 PANE_FRAME 带。

                    FillRect(hdc, &paneCell[i], (i == cvActiveIdx()) ? accent : normal);
                }
                DeleteObject(accent);
                DeleteObject(normal);
            }
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_SYSCOLORCHANGE: {
            // 容器主题切换（亮色 <-> 暗色）：用新颜色重绘所有内容。

            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HWND ctl = (HWND)lParam;
            for (int i = 0; i < 2; i++) {
                if (ctl == cvPaneLabel(i)) {
                    bool dark = isDarkMode();
                    if (!paneLabelActiveBrush) paneLabelActiveBrush = CreateSolidBrush(RGB(0, 120, 215));
                    if (!paneLabelInactiveBrush || dark != paneLabelInactiveDark) {
                        if (paneLabelInactiveBrush) DeleteObject(paneLabelInactiveBrush);
                        paneLabelInactiveBrush = CreateSolidBrush(themeFaceBg());
                        paneLabelInactiveDark = dark;
                    }
                    HDC hdc = (HDC)wParam;
                    bool active = (i == cvActiveIdx());
                    SetBkMode(hdc, OPAQUE);
                    SetTextColor(hdc, active ? RGB(255, 255, 255) : themeFaceText());
                    SetBkColor(hdc, active ? RGB(0, 120, 215) : themeFaceBg());
                    return (LRESULT)(active ? paneLabelActiveBrush : paneLabelInactiveBrush);
                }
            }
            break;
        }
        case WM_PARENTNOTIFY: {
            // 中键单击标签页将其关闭。

            if (LOWORD(wParam) == WM_MBUTTONDOWN && (HWND)lParam == hwndTabs) {
                DWORD pos = GetMessagePos();
                POINT pt = {(short)LOWORD(pos), (short)HIWORD(pos)};
                ScreenToClient(hwndTabs, &pt);
                TCHITTESTINFO thti = {0};
                thti.pt = pt;
                int idx = TabCtrl_HitTest(hwndTabs, &thti);
                if (idx >= 0) tabsClose(idx);
            }
            break;
        }
        case WM_COMMAND: {
            if (lParam == 0 && HIWORD(wParam) == 0) {
                mainMenuCommand(wParam);
                return 0;
            }
            else if ((HWND)lParam == hwndToolbar) {
                toolbarCommand(LOWORD(wParam));
            }
            else if (HIWORD(wParam) == STN_CLICKED && cvActivatePaneByLabel((HWND)lParam)) {
                return 0;
            }
            break;
        }
        case WM_SYSCOMMAND: {
            switch (LOWORD(wParam)) {
                case ID_HELP_ABOUT: {
                    DialogBox(globalHInstance, MAKEINTRESOURCE(IDD_ABOUT), hwnd, &AboutDialogProc);
                    return 0;
                }
            }
            break;
        }
        case WM_APP + 77: {  // WM_FOLDERSIZE_DONE
            unsigned long long sz = (unsigned long long)wParam;
            wchar_t* path = (wchar_t*)lParam;
            double mb = sz / 1048576.0;
            wchar_t msg[512];
            if (mb >= 1024) swprintf_s(msg, 512, L"%.2f GB\n%ls", mb / 1024.0, path ? path : L"");
            else swprintf_s(msg, 512, L"%.2f MB\n%ls", mb, path ? path : L"");
            MessageBoxW(hwnd, msg, lc_str.folder_size, MB_OK | MB_ICONINFORMATION);
            if (path) free(path);
            return 0;
        }
        case WM_CLOSE: {
            if (showConfirmDialog(NULL, lc_str.confirm_exit, lc_str.msg_confirm_exit_app)) {
                PostQuitMessage(0);
            }
            return 0;
        }       
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* nmhdr = (NMHDR*)lParam;
            if (cvIsContentView(nmhdr->hwndFrom)) {
                return contentViewNotify(nmhdr);
            }
            else if (nmhdr->hwndFrom == hwndTreeview) {
                return treeviewNotify(nmhdr);
            }
            else if (nmhdr->hwndFrom == hwndTabs) {
                if (nmhdr->code == TCN_SELCHANGE) {
                    int newSel = TabCtrl_GetCurSel(hwndTabs);
                    if (newSel != activeTab && newSel >= 0) {
                        tabsSaveCurrent();
                        tabsRestore(newSel);
                    }
                }
                return 0;
            }
            else return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void navigateToFileNode(struct FileNode* node) {
    if (node) {
        wchar_t p[MAX_PATH]={0}; getFileNodePath(node,p);
        navPushHistory(p); recentAdd(p);
        clearAddrButtons();
        clearContentView();
        setCurrPathFileNode(node);
        navigateRefresh();
        tabsSyncCurrent(p);
        cvSyncOtherPane(node->name);
    }
}

void navigateToPath(wchar_t* path) {
    if (path) {
        navPushHistory(path); recentAdd(path);
        clearAddrButtons();
        clearContentView();
        setCurrPathFromString(path);
        navigateRefresh();
        tabsSyncCurrent(path);
    }
}

void navigateUp() {
    if (currPathFileNode->parent) {
        clearAddrButtons();
        clearContentView();
        setCurrPathFileNode(currPathFileNode->parent);
        navigateRefresh();
        wchar_t p[MAX_PATH]={0};
        getFileNodePath(currPathFileNode, p);
        tabsSyncCurrent(p);
    }
}

void navigateRefresh() {
    if (currPathFileNode) {
        buildChildNodes(currPathFileNode, false);
        SetWindowText(hwndMain, currPathFileNode->name);    
        updateAddrButtons();
        refreshContentView();
    }
}

void openFileNode(struct FileNode* node) {
    if (node->type == TYPE_FILE) {
        wchar_t path[MAX_PATH] = {0};
        wchar_t parentPath[MAX_PATH] = {0};
        getFileNodePath(node, path);
        getFileNodePath(node->parent, parentPath);
        ShellExecute(hwndMain, L"open", path, NULL, parentPath, SW_SHOW);
    }
    else navigateToFileNode(node);
}

static void createMainMenu() {
    HMENU hmOld = GetMenu(hwndMain);

    HMENU hmFile = CreatePopupMenu();
    AppendMenu(hmFile, MF_STRING, ID_FILE_EXIT, lc_str.exit);

    HMENU hmEdit = CreatePopupMenu();
    AppendMenu(hmEdit, MF_STRING, ID_EDIT_CUT, lc_str.cut);
    AppendMenu(hmEdit, MF_STRING, ID_EDIT_COPY, lc_str.copy);
    AppendMenu(hmEdit, MF_STRING, ID_EDIT_PASTE, lc_str.paste);
    AppendMenu(hmEdit, MF_STRING, ID_EDIT_PASTE_SHORTCUT, lc_str.paste_shortcut);
    AppendMenu(hmEdit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmEdit, MF_STRING, ID_EDIT_SELECT_ALL, lc_str.select_all);

    HMENU hmView = CreatePopupMenu();
    AppendMenu(hmView, MF_STRING, ID_VIEW_LARGEICONS, lc_str.large_icons);
    AppendMenu(hmView, MF_STRING, ID_VIEW_SMALLICONS, lc_str.small_icons);
    AppendMenu(hmView, MF_STRING, ID_VIEW_LIST, lc_str.list);
    AppendMenu(hmView, MF_STRING, ID_VIEW_DETAILS, lc_str.details);
    AppendMenu(hmView, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmView, MF_STRING, ID_VIEW_SPLIT, lc_str.split_view);
    AppendMenu(hmView, MF_STRING, ID_VIEW_PREVIEW, lc_str.preview_pane);
    AppendMenu(hmView, MF_STRING, ID_VIEW_HIDDEN, lc_str.show_hidden);
    AppendMenu(hmView, MF_STRING, ID_VIEW_MEMORY, L"显示存储信息");
    // 排序方式子菜单：所有视图通用，不依赖右键空白
    {
        HMENU hmSort = CreatePopupMenu();
        AppendMenu(hmSort, MF_STRING, ID_SORT_NAME, lc_str.sort_name);
        AppendMenu(hmSort, MF_STRING, ID_SORT_TYPE, lc_str.sort_type);
        AppendMenu(hmSort, MF_STRING, ID_SORT_SIZE, lc_str.sort_size);
        AppendMenu(hmSort, MF_STRING, ID_SORT_DATE, lc_str.sort_date);
        AppendMenu(hmView, MF_POPUP | MF_STRING, (UINT_PTR)hmSort, lc_str.sort_by ? lc_str.sort_by : L"排序方式");
    }
    // 字体大小子菜单：用户可自由调整，比自适应DPI更直接有效
    {
        HMENU hmFont = CreatePopupMenu();
        UINT checked = MF_BYCOMMAND | MF_CHECKED;
        AppendMenu(hmFont, (g_fontSizePt == 9 ? checked : MF_STRING), ID_VIEW_FONT_SMALL, L"小 (9pt)");
        AppendMenu(hmFont, (g_fontSizePt == 11 ? checked : MF_STRING), ID_VIEW_FONT_MEDIUM, L"标准 (11pt)");
        AppendMenu(hmFont, (g_fontSizePt == 13 ? checked : MF_STRING), ID_VIEW_FONT_LARGE, L"大 (13pt)");
        AppendMenu(hmFont, (g_fontSizePt == 15 ? checked : MF_STRING), ID_VIEW_FONT_XLARGE, L"特大 (15pt)");
        AppendMenu(hmView, MF_POPUP | MF_STRING, (UINT_PTR)hmFont, lc_str.font_size);
    }
    AppendMenu(hmView, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmView, MF_STRING, ID_VIEW_GAME_MODE, lc_str.game_mode);
    AppendMenu(hmView, MF_STRING, ID_VIEW_COMPARE, lc_str.compare_panes);
    // 主题子菜单：浅色/深色/自定义（原主题系统无菜单入口，用户找不到深色模式）
    {
        HMENU hmTheme = CreatePopupMenu();
        UINT checked = MF_BYCOMMAND | MF_CHECKED;
        int curTheme = themeGetMode();
        AppendMenu(hmTheme, (curTheme == THEME_LIGHT ? checked : MF_STRING), ID_VIEW_THEME_LIGHT, L"浅色主题");
        AppendMenu(hmTheme, (curTheme == THEME_DARK ? checked : MF_STRING), ID_VIEW_THEME_DARK, L"深色主题");
        AppendMenu(hmTheme, (curTheme == THEME_CUSTOM ? checked : MF_STRING), ID_VIEW_THEME_CUSTOM, L"自定义");
        AppendMenu(hmView, MF_POPUP | MF_STRING, (UINT_PTR)hmTheme, L"主题");
    }
    hViewMenu = hmView;
    // 设置视图菜单初始勾选状态
    CheckMenuItem(hViewMenu, ID_VIEW_HIDDEN, MF_BYCOMMAND | (showHiddenFiles ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hViewMenu, ID_VIEW_MEMORY, MF_BYCOMMAND | (cvMemoryVisible() ? MF_CHECKED : MF_UNCHECKED));

    HMENU hmNav = CreatePopupMenu();
    AppendMenu(hmNav, MF_STRING, ID_NAV_BACK, lc_str.nav_back);
    AppendMenu(hmNav, MF_STRING, ID_NAV_FORWARD, lc_str.nav_forward);
    AppendMenu(hmNav, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmNav, MF_STRING, ID_NAV_RECENT, lc_str.recent_places);
    AppendMenu(hmNav, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmNav, MF_STRING, ID_TAB_NEW, L"Ctrl+T  新建标签页");
    AppendMenu(hmNav, MF_STRING, ID_TAB_CLOSE, L"Ctrl+W  关闭标签页");

    HMENU hmTools = CreatePopupMenu();
    AppendMenu(hmTools, MF_STRING, ID_TOOL_NOTEPAD, lc_str.tool_notepad);
    AppendMenu(hmTools, MF_STRING, ID_TOOL_CMD, lc_str.tool_cmd);
    AppendMenu(hmTools, MF_STRING, ID_TOOL_REGEDIT, lc_str.tool_regedit);
    AppendMenu(hmTools, MF_STRING, ID_TOOL_TASKMGR, lc_str.tool_taskmgr);

    HMENU hmLang = CreatePopupMenu();
    AppendMenu(hmLang, MF_STRING, ID_LANG_EN, L"English");
    AppendMenu(hmLang, MF_STRING, ID_LANG_ZH, L"\u4e2d\u6587");
    AppendMenu(hmLang, MF_STRING, ID_LANG_PT, L"Portugu\u00eas");
    AppendMenu(hmLang, MF_STRING, ID_LANG_RU, L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439");

    HMENU hmHelp = CreatePopupMenu();
    AppendMenu(hmHelp, MF_STRING, ID_HELP_ABOUT, lc_str.about);

    HMENU hmMain = CreateMenu();
    AppendMenu(hmMain, MF_POPUP, (UINT_PTR)hmFile, lc_str.file);
    AppendMenu(hmMain, MF_POPUP, (UINT_PTR)hmEdit, lc_str.edit);
    AppendMenu(hmMain, MF_POPUP, (UINT_PTR)hmNav, lc_str.nav_menu);
    AppendMenu(hmMain, MF_POPUP, (UINT_PTR)hmView, lc_str.view);
    AppendMenu(hmMain, MF_POPUP, (UINT_PTR)hmTools, lc_str.tools_menu);
    AppendMenu(hmMain, MF_POPUP, (UINT_PTR)hmLang, lc_str.language);
    AppendMenu(hmMain, MF_POPUP, (UINT_PTR)hmHelp, lc_str.help);

    SetMenu(hwndMain, hmMain);
    DrawMenuBar(hwndMain);
    if (hmOld) DestroyMenu(hmOld);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
    int numArgs;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &numArgs);
    
    wchar_t localeName[16] = {0};
    GetSystemDefaultLocaleName(localeName, 16);
    // 强制简体中文；用户可通过语言菜单切换

    loadLCStrings(L"zh-CN");

    globalHInstance = hInstance;

    // 加载用户配置的字体大小（必须在第一次调用getUIFont之前）
    loadFontSize();

    // 为 OLE 拖放初始化 COM

    OleInitialize(NULL);  // OLE init required for drag-and-drop

    // 初始化主题 + 配置（基于注册表）

    themeInit();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEX wcx = {0};
    wcx.cbSize = sizeof(wcx);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = &MainWndProc;
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hInstance = hInstance;
    wcx.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));
    wcx.hCursor = LoadCursor(hInstance, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wcx.lpszClassName = mainWndClass;
    wcx.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN));

    if (!RegisterClassEx(&wcx)) return 0;

    // 预览面板窗口类。

    WNDCLASSEX pwc = {0};
    pwc.cbSize = sizeof(pwc);
    pwc.style = CS_HREDRAW | CS_VREDRAW;
    pwc.lpfnWndProc = &PreviewWndProc;
    pwc.hInstance = hInstance;
    pwc.hCursor = LoadCursor(hInstance, IDC_ARROW);
    pwc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    pwc.lpszClassName = previewWndClass;
    RegisterClassEx(&pwc);

    // 全尺寸图像查看器窗口类。

    WNDCLASSEX zwc = {0};
    zwc.cbSize = sizeof(zwc);
    zwc.style = CS_HREDRAW | CS_VREDRAW;
    zwc.lpfnWndProc = &ZoomWndProc;
    zwc.hInstance = hInstance;
    zwc.hCursor = LoadCursor(hInstance, IDC_ARROW);
    zwc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    zwc.lpszClassName = zoomWndClass;
    RegisterClassEx(&zwc);

    initFileNodes();

    HWND hwndDesktop = GetDesktopWindow();
    RECT desktopRect;
    GetWindowRect(hwndDesktop, &desktopRect);
    int hwndWidth = (desktopRect.right - desktopRect.left) * 0.8f;
    int hwndHeight = (desktopRect.bottom - desktopRect.top) * 0.8f;
    
    hwndMain = CreateWindowEx(0, mainWndClass, L"", WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW, 
                              0, 0, hwndWidth, hwndHeight, NULL, NULL, hInstance, NULL);
    if (!hwndMain) return 0;
    
    // 提升系统菜单/标题字体，使菜单栏、右键菜单与主区域同样清晰（Bionic下默认为小字体）
    boostSystemFonts();
    createMainMenu();
    createToolbar();
    createNavbar();

    // 标签控件必须在 createContentView 之前存在，因为控件创建

    // 触发 WM_SIZE -> resizeControls，将内容定位在标签栏下方。

    hwndTabs = CreateWindowEx(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH | TCS_TOOLTIPS,
        0, 0, 200, 24, hwndMain, NULL, hInstance, NULL);
    TabCtrl_SetItemSize(hwndTabs, 120, 22);
    tabsAdd(NULL);  // initial tab; strip stays hidden until a second tab opens

    createTreeview();
    createSizebar();
    createContentView();
    cvInitPanePaths();
    cvEnsureLocaleFallback();  // 启动时回退上次转区残留的Locale（崩溃保护）
    // 预览面板（默认隐藏；通过 视图 > 预览面板 切换）。

    hwndPreview = CreateWindowEx(WS_EX_CLIENTEDGE, previewWndClass, L"",
        WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 220, 100, hwndMain, NULL, hInstance, NULL);
    createStatusbar();

    setViewStyle(STYLE_DETAILS);
    int treeviewWidth = hwndWidth * 0.2f;
    SetWindowPos(hwndTreeview, NULL, 0, 0, treeviewWidth, 0, SWP_NOZORDER | SWP_NOMOVE);    
    
    if (numArgs > 1) {
        navigateToPath(args[1]);
    }
    else navigateRefresh();

    ShowWindow(hwndMain, SW_SHOW);
    UpdateWindow(hwndMain);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}
