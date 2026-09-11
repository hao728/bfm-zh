#include "main.h"
#include <olectl.h>   // OleLoadPicturePath for image preview
#include <ocidl.h>    // IPicture interface

static const wchar_t mainWndClass[] = L"WFM-MainWnd";

extern struct FileNode* currPathFileNode;
extern HWND hwndNavbar;
extern HWND hwndSizebar;
extern HWND hwndStatusbar;
extern HWND hwndToolbar;
extern HWND hwndTreeview;

HINSTANCE globalHInstance = NULL;
HWND hwndMain = NULL;
HWND hwndTabs = NULL;
HWND hwndPreview = NULL;
bool previewOn = false;
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

// Hide the tab strip when only one tab is open (it is redundant clutter);
// show it again once a second tab is created.
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

// Called after every navigation: persist current path into the active tab and
// refresh its label so the tab bar reflects where the user is.
static void tabsSyncCurrent(const wchar_t* path) {
    if (!hwndTabs || activeTab < 0 || activeTab >= tabCount) return;
    if (path) wcscpy_s(tabStates[activeTab].path, MAX_PATH, path);
    wchar_t label[64] = {0};
    if (path && path[0]) {
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        if (!name[0]) {
            // drive root like "C:\" — show "C:"
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

// Public tab API used by keyboard shortcuts (Ctrl+T / Ctrl+W) and menus.
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

// Theme functions live in theme.c (light/dark/custom, accent color, hover/alt-row).
// Config persistence lives in config.c (registry-backed cfgGet/cfgSet).

// --- Dark non-client scrollbars -------------------------------------------------------
// Wine paints a window's own (non-client) scrollbars with the classic light 3D look no
// matter what the container theme is, so every list view / tree view ends up with a white
// bar down its right edge and across its bottom. There is no message to recolour them
// (WM_CTLCOLORSCROLLBAR only applies to standalone scrollbar controls), so we repaint them
// ourselves over the window DC — the same owner-draw approach used for the column header,
// status bar and navbar buttons. Light mode is left to the system.
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

// Paints one scrollbar dark. Returns its rect (in window coords) via `out` so the caller can
// fill the corner square where a horizontal and a vertical bar meet.
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

    // Arrow buttons at each end.
    if (btn > 0) {
        RECT a = rc, b = rc;
        if (vertical) { a.bottom = a.top + btn; b.top = b.bottom - btn; }
        else          { a.right = a.left + btn; b.left = b.right - btn; }
        drawScrollArrow(hdc, &a, vertical ? SB_ARROW_UP : SB_ARROW_LEFT, SB_ARROW_DARK);
        drawScrollArrow(hdc, &b, vertical ? SB_ARROW_DOWN : SB_ARROW_RIGHT, SB_ARROW_DARK);
    }

    // Thumb. xyThumbTop/Bottom are offsets from the start of rcScrollBar; if Wine reports
    // nothing usable, fall back to computing it from the scroll range.
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

    // The dead square where the two bars meet is drawn light by Wine as well.
    if (!IsRectEmpty(&vrc) && !IsRectEmpty(&hrc)) {
        RECT corner = {vrc.left, hrc.top, vrc.right, hrc.bottom};
        fillRect(hdc, &corner, SB_TROUGH_DARK);
    }

    ReleaseDC(hwnd, hdc);
}

// While a scrollbar is being used, Wine runs its OWN modal message loop (track_scroll_bar)
// and repaints the bar light on every mouse move and auto-repeat tick — so a press in the
// trough stayed white for as long as it was held, because our repaint only ran once the
// loop returned. That loop does dispatch plain WM_TIMER messages to the window, so we drive
// a fast repaint timer for exactly as long as the tracking lasts and win the race.
#define THEME_SB_TIMER_ID 0x7BF1
#define THEME_SB_TIMER_MS 10

// Called BEFORE the original window proc. Returns true if the message was fully handled.
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

// Called AFTER the original window proc (which is where the modal tracking loop returns).
void themeScrollbarsHookAfter(HWND hwnd, UINT msg) {
    if (msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK ||
        msg == WM_NCLBUTTONUP || msg == WM_CAPTURECHANGED) {
        KillTimer(hwnd, THEME_SB_TIMER_ID);
    }
    if (themeScrollbarsNeedRepaint(msg)) themePaintScrollbars(hwnd);
}

// Wine redraws the scrollbars from inside the control's own handling (SetScrollInfo draws
// immediately), so repainting on WM_NCPAINT alone is not enough — subclasses call
// themePaintScrollbars() after the original proc for any message that can move or resize a
// bar. Kept to a whitelist so hot query messages don't repaint.
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

// CreateFontW never returns NULL on a missing face — it silently substitutes —
// so a requested name is not proof the face exists. We verify two things:
//   (a) GetTextFace reports the same name we asked for;
//   (b) the face actually carries a glyph for a common CJK char (U+6587 文),
//       which is the real test of whether Chinese will render instead of boxes.
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

// EnumFontFamiliesEx callback: record the first installed TrueType face that
// can render CJK. This discovers whatever the container actually ships
// (Droid Sans Fallback, Source Han, etc.) without us guessing its name.
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

// Shared UI font. We do NOT trust SystemParametersInfo (its lfMessageFont is a
// logical name like "MS Shell Dlg" that Wine substitutes anyway). Instead we
// pick a real, installed, CJK-capable TrueType face: first a preferred name,
// then whatever the system enumerates. DPI-aware height keeps it sharp on
// high-density phone panels.
static HFONT uiFont = NULL;
HFONT getUIFont(void) {
    if (!uiFont) {
        HDC screen = GetDC(NULL);
        int dpiY = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(NULL, screen);
        if (dpiY <= 0) dpiY = 96;
        // 10pt reads sharper than 9pt under Wine's freetype at small sizes;
        // clamp so a misconfigured high DPI does not blow the font up huge.
        int height = -MulDiv(10, dpiY, 72);  // 10pt -> device pixels
        if (height > -11) height = -11;
        if (height < -18) height = -18;

        wchar_t chosen[LF_FACESIZE] = {0};

        // 1. Preferred faces, verified to exist AND carry CJK glyphs.
        static const wchar_t* preferred[] = {
            L"Microsoft YaHei", L"微软雅黑", L"Noto Sans CJK SC",
            L"Source Han Sans SC", L"WenQuanYi Micro Hei",
            L"Droid Sans Fallback", NULL
        };
        for (int i = 0; preferred[i] && !chosen[0]; i++) {
            if (fontFaceUsable(preferred[i], true))
                wcscpy_s(chosen, LF_FACESIZE, preferred[i]);
        }

        // 2. Enumerate every installed face and take the first CJK-capable one.
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

        // 3. Build the chosen CJK face; if none, Tahoma (Wine always ships it).
        // OUT_TT_PRECIS forces the mapper to pick a TrueType face (raster .fon
        // fonts scale badly); CLEARTYPE_QUALITY gives subpixel rendering on
        // LCD panels and falls back to grayscale AA where unsupported.
        if (chosen[0])
            uiFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, chosen);
        if (!uiFont && fontFaceUsable(L"Tahoma", false))
            uiFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
        // 4. Last resort.
        if (!uiFont)
            uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    return uiFont;
}

void GetWindowRectInParent(HWND hwnd, RECT* rect) {
    GetWindowRect(hwnd, rect);
    MapWindowPoints(HWND_DESKTOP, GetParent(hwnd), (LPPOINT)rect, 2);
}

INT_PTR CALLBACK AboutDialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NOTIFY: {
            // SysLink click: open repository URL in default browser
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

// Defined in content_view.c; refreshes column headers and status bar after language switch
extern void cvRefreshLanguage(void);
extern void onMenuItemLoadISOImageClick(void);
extern void onMenuItemUnloadISOImageClick(void);

// Forward declaration: createMainMenu is defined later but called by mainMenuCommand
static void createMainMenu();

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
            navigateRefresh();
            break;
        case ID_NAV_BACK: navGoBack(); break;
        case ID_NAV_FORWARD: navGoForward(); break;
        case ID_NAV_RECENT: recentMenu(); break;
        case ID_VIEW_GAME_MODE: onMenuItemGameModeClick(); break;
        case ID_VIEW_COMPARE: onMenuItemComparePanesClick(); break;
        case ID_TAB_NEW: tabNew(); break;
        case ID_TAB_CLOSE: tabCloseActive(); break;
        case ID_TOOL_NOTEPAD: ShellExecuteW(NULL, L"open", L"notepad.exe", NULL, NULL, SW_SHOW); break;
        case ID_TOOL_CMD: ShellExecuteW(NULL, L"open", L"cmd.exe", NULL, NULL, SW_SHOW); break;
        case ID_TOOL_REGEDIT: ShellExecuteW(NULL, L"open", L"regedit.exe", NULL, NULL, SW_SHOW); break;
        case ID_TOOL_TASKMGR: ShellExecuteW(NULL, L"open", L"taskmgr.exe", NULL, NULL, SW_SHOW); break;
        case ID_TOOL_LAUNCHER: onMenuItemLauncherChooseClick(); break;
    }
}

// --- Preview pane -----------------------------------------------------------------------
static const wchar_t previewWndClass[] = L"WFM-PreviewPane";
static wchar_t previewPath[MAX_PATH] = {0};
static IPicture* previewPic = NULL;
static HICON previewIcon = NULL;
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
           !_wcsicmp(ext, L"bmp") || !_wcsicmp(ext, L"ico");
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
    // Skip UTF-8 BOM if present.
    int skip = (read >= 3 && (unsigned char)buf[0] == 0xEF &&
                (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
    // Try UTF-8 first; fall back to ANSI.
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
    // Replace control chars except tab/newline with spaces.
    for (wchar_t* p = previewText; *p; p++) {
        if (*p < L' ' && *p != L'\t' && *p != L'\n' && *p != L'\r') *p = L' ';
    }
}

void previewUpdate(void) {
    if (!previewOn || !hwndPreview) return;
    // Release previous resources.
    if (previewPic) { previewPic->lpVtbl->Release(previewPic); previewPic = NULL; }
    if (previewIcon) { DestroyIcon(previewIcon); previewIcon = NULL; }
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

    // Large icon for non-images (and fallback).
    SHFILEINFOW sfi = {0};
    if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_LARGEICON | SHGFI_TYPENAME)) {
        previewIcon = sfi.hIcon;
        wcscpy_s(previewTypeName, 64, sfi.szTypeName);
    }

    // File size + date.
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

    // Load image for image files.
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

static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            // Background.
            HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(hdc, &rc, bg); DeleteObject(bg);

            int margin = 12;
            int contentW = rc.right - margin * 2;
            int y = margin;

            if (!previewPath[0]) {
                SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
                HFONT old = (HFONT)SelectObject(hdc, getUIFont());
                RECT tr = {margin, y, rc.right - margin, y + 40};
                DrawTextW(hdc, L"Select a file to preview", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, old);
                EndPaint(hwnd, &ps);
                return 0;
            }

            // Image, text preview, or large icon.
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
                    // High-quality resampling so downscaled images stay crisp in Wine.
                    SetStretchBltMode(hdc, HALFTONE);
                    SetBrushOrgEx(hdc, 0, 0, NULL);
                    previewPic->lpVtbl->Render(previewPic, hdc, dx, dy, dw, dh,
                                               0, ph, pw, -ph, NULL);
                }
            } else if (previewText[0]) {
                // Text file: show first lines in a bordered box.
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
            } else if (previewIcon) {
                DrawIconEx(hdc, margin + (contentW - 48) / 2, y + (mediaH - 48) / 2,
                           previewIcon, 48, 48, 0, NULL, DI_NORMAL);
            }
            y += mediaH + margin;

            // File name (bold-ish via larger font).
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

            // Separator.
            HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DFACE));
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, margin, y, NULL); LineTo(hdc, rc.right - margin, y);
            SelectObject(hdc, oldPen); DeleteObject(pen);
            y += 10;

            // Metadata rows.
            HFONT hf = getUIFont();
            old = (HFONT)SelectObject(hdc, hf);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkMode(hdc, TRANSPARENT);
            wchar_t row[128];
            int rowH = 20;
            if (previewTypeName[0]) {
                swprintf_s(row, 128, L"Type: %ls", previewTypeName);
                RECT rr = {margin, y, rc.right - margin, y + rowH};
                DrawTextW(hdc, row, -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += rowH;
            }
            if (previewSizeStr[0]) {
                swprintf_s(row, 128, L"Size: %ls", previewSizeStr);
                RECT rr = {margin, y, rc.right - margin, y + rowH};
                DrawTextW(hdc, row, -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += rowH;
            }
            if (previewDateStr[0]) {
                swprintf_s(row, 128, L"Modified: %ls", previewDateStr);
                RECT rr = {margin, y, rc.right - margin, y + rowH};
                DrawTextW(hdc, row, -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                y += rowH;
            }
            // Location.
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

    // Tab bar sits below navbar, but only when it is visible (2+ tabs). With a
    // single tab it is hidden and consumes no vertical space.
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

    // Preview pane sits to the right of the content area.
    if (hwndPreview) {
        SetWindowPos(hwndPreview, NULL, rect.right - previewW, contentTop, previewW, treeviewHeight, SWP_NOZORDER);
        ShowWindow(hwndPreview, previewOn ? SW_SHOW : SW_HIDE);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_USER_EXTRACT_DONE:
            navigateRefresh();
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
                    // Children are clipped (WS_CLIPCHILDREN), so this only paints the
                    // PANE_FRAME band around each list view.
                    FillRect(hdc, &paneCell[i], (i == cvActiveIdx()) ? accent : normal);
                }
                DeleteObject(accent);
                DeleteObject(normal);
            }
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_SYSCOLORCHANGE: {
            // Container theme switched (light <-> dark): repaint everything with new colors.
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
            // Middle-click on a tab closes it.
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
            if (MessageBox(NULL, lc_str.msg_confirm_exit_app, lc_str.confirm_exit, MB_YESNO | MB_ICONQUESTION) == IDYES) {
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
    AppendMenu(hmView, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmView, MF_STRING, ID_VIEW_GAME_MODE, lc_str.game_mode);
    AppendMenu(hmView, MF_STRING, ID_VIEW_COMPARE, lc_str.compare_panes);
    hViewMenu = hmView;

    HMENU hmNav = CreatePopupMenu();
    AppendMenu(hmNav, MF_STRING, ID_NAV_BACK, lc_str.nav_back);
    AppendMenu(hmNav, MF_STRING, ID_NAV_FORWARD, lc_str.nav_forward);
    AppendMenu(hmNav, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmNav, MF_STRING, ID_NAV_RECENT, lc_str.recent_places);
    AppendMenu(hmNav, MF_SEPARATOR, 0, NULL);
    AppendMenu(hmNav, MF_STRING, ID_TAB_NEW, L"Ctrl+T  New Tab");
    AppendMenu(hmNav, MF_STRING, ID_TAB_CLOSE, L"Ctrl+W  Close Tab");

    HMENU hmTools = CreatePopupMenu();
    AppendMenu(hmTools, MF_STRING, ID_TOOL_NOTEPAD, lc_str.tool_notepad);
    AppendMenu(hmTools, MF_STRING, ID_TOOL_CMD, lc_str.tool_cmd);
    AppendMenu(hmTools, MF_STRING, ID_TOOL_REGEDIT, lc_str.tool_regedit);
    AppendMenu(hmTools, MF_STRING, ID_TOOL_TASKMGR, lc_str.tool_taskmgr);

    HMENU hmLang = CreatePopupMenu();
    AppendMenu(hmLang, MF_STRING, ID_LANG_EN, L"English");
    AppendMenu(hmLang, MF_STRING, ID_LANG_ZH, L"\u4e2d\u6587");
    AppendMenu(hmLang, MF_STRING, ID_LANG_PT, L"Portugues");
    AppendMenu(hmLang, MF_STRING, ID_LANG_RU, L"Russian");

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
    // Force Simplified Chinese; user can switch via Language menu
    loadLCStrings(L"zh-CN");

    globalHInstance = hInstance;

    // Initialize COM for OLE drag and drop
    OleInitialize(NULL);  // OLE init required for drag-and-drop

    // Initialize theme + config (registry-backed)
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

    // Preview pane window class.
    WNDCLASSEX pwc = {0};
    pwc.cbSize = sizeof(pwc);
    pwc.style = CS_HREDRAW | CS_VREDRAW;
    pwc.lpfnWndProc = &PreviewWndProc;
    pwc.hInstance = hInstance;
    pwc.hCursor = LoadCursor(hInstance, IDC_ARROW);
    pwc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    pwc.lpszClassName = previewWndClass;
    RegisterClassEx(&pwc);

    initFileNodes();

    HWND hwndDesktop = GetDesktopWindow();
    RECT desktopRect;
    GetWindowRect(hwndDesktop, &desktopRect);
    int hwndWidth = (desktopRect.right - desktopRect.left) * 0.8f;
    int hwndHeight = (desktopRect.bottom - desktopRect.top) * 0.8f;
    
    hwndMain = CreateWindowEx(0, mainWndClass, L"", WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW, 
                              0, 0, hwndWidth, hwndHeight, NULL, NULL, hInstance, NULL);
    if (!hwndMain) return 0;
    
    createMainMenu();
    createToolbar();
    createNavbar();

    // Tab control must exist before createContentView, because control creation
    // triggers WM_SIZE -> resizeControls, which positions content below the tab bar.
    hwndTabs = CreateWindowEx(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH | TCS_TOOLTIPS,
        0, 0, 200, 24, hwndMain, NULL, hInstance, NULL);
    TabCtrl_SetItemSize(hwndTabs, 120, 22);
    tabsAdd(NULL);  // initial tab; strip stays hidden until a second tab opens

    createTreeview();
    createSizebar();
    createContentView();
    cvInitPanePaths();
    // Preview pane (hidden by default; toggled via View > Preview pane).
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
