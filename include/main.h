#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
// SysLink control (NMLINK/PNMLINK) requires commctrl v6+
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <windows.h>
#include <commctrl.h>
#include <wingdi.h>
#include <uxtheme.h>
#include <winnls.h>
#include <wchar.h>
#include <strsafe.h>
#include <shlobj.h>
#include <process.h>
#include <time.h>
#include <math.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>

static inline void alertf(const char *fmt, ...) {
    char text[128] = {0};
    va_list args;
    va_start(args, fmt);
    vsprintf_s(text, 128, fmt, args);
    va_end(args);
    MessageBoxA(NULL, text, "Alert", MB_OK);
}

static inline void timetToFileTime(time_t t, LPFILETIME result) {
    ULARGE_INTEGER timeValue;
    timeValue.QuadPart = (t * 10000000LL) + 116444736000000000LL;
    result->dwLowDateTime = timeValue.LowPart;
    result->dwHighDateTime = timeValue.HighPart;
}

#include "resource.h"
#include "theme.h"
#include "config.h"
#include "favorites.h"
#include "diff.h"
#include "content_view.h"
#include "toolbar.h"
#include "navbar.h"
#include "treeview.h"
#include "sizebar.h"
#include "statusbar.h"
#include "file_node.h"
#include "file_actions.h"
#include "file_utils.h"
#include "input_dialog.h"

extern HINSTANCE globalHInstance;
#include "strings.h"

#define MEMFREE(x) \
    do { \
        if (x != NULL) { \
            free(x); \
            x = NULL; \
        } \
    } \
    while(0)

void navigateToFileNode(struct FileNode* node);
void navigateToPath(wchar_t* path);
void navigateUp();
void navigateRefresh();
void tabNew(void);
void tabCloseActive(void);
void navGoBack(void);
void navGoForward(void);
void navPushHistory(wchar_t* path);
void recentAdd(wchar_t* path);
void recentMenu(void);
void onMenuItemGameModeClick(void);
void onMenuItemComparePanesClick(void);
void onMenuItemLauncherChooseClick(void);
void cvSyncOtherPane(const wchar_t* targetName);
void openFileNode(struct FileNode* node);
void GetWindowRectInParent(HWND hwnd, RECT* rect);
void resizeControls();
HFONT getUIFont(void);
void createStatusbar(void);
void setStatusbarText(wchar_t* text);
void setStatusbarParts(wchar_t* p0, wchar_t* p1, wchar_t* p2, wchar_t* p3);
bool isDarkMode(void);
COLORREF themeFaceBg(void);
COLORREF themeFaceText(void);
COLORREF themeFaceLine(void);
COLORREF themeFieldBg(void);
COLORREF themeFieldText(void);
COLORREF themePlaceholder(void);
void themePaintScrollbars(HWND hwnd);
bool themeScrollbarsNeedRepaint(UINT msg);
bool themeScrollbarsHookBefore(HWND hwnd, UINT msg, WPARAM wParam);
void themeScrollbarsHookAfter(HWND hwnd, UINT msg);

#endif