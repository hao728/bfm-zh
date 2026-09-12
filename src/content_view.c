#include "main.h"
#include <oleidl.h>
#include <wincrypt.h>
#include <tlhelp32.h>

#define COLUMN_NAME_IDX 0
#define COLUMN_TYPE_IDX 1
#define COLUMN_SIZE_IDX 2
#define COLUMN_DATE_IDX 3
#define COLUMN_PATH_IDX 4

#define NUM_PANES 2

enum Msg {
    MSG_ADD_ITEM = WM_APP,
    MSG_SEARCH_DONE
};

enum ContextMenuType {
    MENU_SINGLE,
    MENU_MULTIPLE,
    MENU_EMPTY
};

struct ListItem {
    int icon;
    struct FileNode* node;
    wchar_t type[64];
    wchar_t formattedSize[32];
    wchar_t formattedDate[32];
    bool loaded;
    uint64_t size;
    wchar_t* path;
    FILETIME modifiedTime;
};

// Per-pane state. Both list views are live simultaneously (each fires its own
// LVN_GETDISPINFO on repaint), so the backing data must be resolvable per HWND.
struct Pane {
    HWND hwndList;
    HWND hwndPathLabel;             // per-pane path bar (shown only in split view)
    struct FileNode* currPath;      // this pane's path chain (independent per pane)
    struct ListItem* items;
    int numItems;
    uint64_t totalSize;             // sum of item sizes, shown in the status bar
    enum ViewStyle viewStyle;
    char sortColumnIdx;
    bool sortAscending;
    struct SearchData* searchData;
};

struct SearchData {
    wchar_t* keyword;
    bool active;
    bool canceled;
    struct Pane* pane;
};

struct ContextMenuItem {
    wchar_t* text;
    void(*proc)();
    wchar_t* cmdData;       // legacy: run via cmd /C
    wchar_t* openExe;       // "Open with" app: ShellExecute this exe with openFile
    wchar_t* openFile;      // the file to hand to openExe (heap; freed with the item)
};

void onMenuItemLoadISOImageClick();
void onMenuItemUnloadISOImageClick();
static void onMenuItemCopyPathClick();
static void onMenuItemOpenCmdClick();
static void onMenuItemExtractHereClick();
static void onMenuItemExtractToFolderClick();
static bool isArchiveExt(const wchar_t* path);
static void startFileDrag(HWND hwnd);
static void updateSelectedItems(void);
static void onMenuItemNewTxtClick();
static void onMenuItemNewBatClick();
static void onMenuItemNewRegClick();
static IDropTarget* createDropTarget(void);
static void onMenuItemExtractIconClick(void);
static void onMenuItemMD5Click(void);
static void onMenuItemViewTextClick(void);
static void onMenuItemBatchRenameClick(void);
void onMenuItemGameModeClick(void);
static void onMenuItemFolderSizeClick(void);
void onMenuItemComparePanesClick(void);
static void onMenuItemCopyToClick(void);
static void onMenuItemMoveToClick(void);
static void onMenuItemAddToFavClick(void);
static bool launcherGetSaved(wchar_t* out);
void onMenuItemLauncherBoostClick(void);
void onMenuItemLauncherRunWithClick(void);
void onMenuItemLauncherChooseClick(void);
static void onMenuItemDiffClick(void);
void recentMenu(void);
void navGoBack(void);
void navGoForward(void);
void navPushHistory(wchar_t* path);
void recentAdd(wchar_t* path);

static struct ContextMenuItem cmiOpen = {NULL, &onMenuItemOpenClick, NULL};
static struct ContextMenuItem cmiEdit = {NULL, &onMenuItemEditClick, NULL};
static struct ContextMenuItem cmiCut = {NULL, &onMenuItemCutClick, NULL};
static struct ContextMenuItem cmiCopy = {NULL, &onMenuItemCopyClick, NULL};
static struct ContextMenuItem cmiCreateShortcut = {NULL, &onMenuItemCreateShortcutClick, NULL};
static struct ContextMenuItem cmiDelete = {NULL, &onMenuItemDeleteClick, NULL};
static struct ContextMenuItem cmiRename = {NULL, &onMenuItemRenameClick, NULL};
static struct ContextMenuItem cmiPaste = {NULL, &onMenuItemPasteClick, NULL};
static struct ContextMenuItem cmiPasteShortcut = {NULL, &onMenuItemPasteShortcutClick, NULL};
static struct ContextMenuItem cmiNewFolder = {NULL, &onMenuItemNewFolderClick, NULL};
static struct ContextMenuItem cmiNewFile = {NULL, &onMenuItemNewFileClick, NULL};
static struct ContextMenuItem cmiLoadISOImage = {NULL, &onMenuItemLoadISOImageClick, NULL};
static struct ContextMenuItem cmiUnloadISOImage = {NULL, &onMenuItemUnloadISOImageClick, NULL};
static struct ContextMenuItem cmiOpenAsAdmin = {NULL, &onMenuItemOpenAsAdminClick, NULL};
static struct ContextMenuItem cmiChooseProgram = {NULL, &onMenuItemOpenWithClick, NULL};
static struct ContextMenuItem cmiProperties = {NULL, &onMenuItemPropertiesClick, NULL};
static struct ContextMenuItem cmiCopyPath = {NULL, &onMenuItemCopyPathClick, NULL};
static struct ContextMenuItem cmiOpenCmd = {NULL, &onMenuItemOpenCmdClick, NULL};
static struct ContextMenuItem cmiExtractHere = {NULL, &onMenuItemExtractHereClick, NULL};
static struct ContextMenuItem cmiExtractToFolder = {NULL, &onMenuItemExtractToFolderClick, NULL};
static struct ContextMenuItem cmiNewTxt = {NULL, &onMenuItemNewTxtClick, NULL};
static struct ContextMenuItem cmiNewBat = {NULL, &onMenuItemNewBatClick, NULL};
static struct ContextMenuItem cmiNewReg = {NULL, &onMenuItemNewRegClick, NULL};
static struct ContextMenuItem cmiExtractIcon = {NULL, &onMenuItemExtractIconClick, NULL};
static struct ContextMenuItem cmiMD5 = {NULL, &onMenuItemMD5Click, NULL};
static struct ContextMenuItem cmiViewText = {NULL, &onMenuItemViewTextClick, NULL};
static struct ContextMenuItem cmiBatchRename = {NULL, &onMenuItemBatchRenameClick, NULL};
static struct ContextMenuItem cmiFolderSize = {NULL, &onMenuItemFolderSizeClick, NULL};
static struct ContextMenuItem cmiCopyTo = {NULL, &onMenuItemCopyToClick, NULL};
static struct ContextMenuItem cmiMoveTo = {NULL, &onMenuItemMoveToClick, NULL};
static struct ContextMenuItem cmiAddToFav = {NULL, &onMenuItemAddToFavClick, NULL};
// Launcher (RamBooster-style): free RAM then launch the target, optionally via an external launcher exe.
// Forward declarations for launcher / launch-args menu handlers (defined below).
void onMenuItemLauncherBoostAggressiveClick(void);
void onMenuItemRunDX11Click(void);
void onMenuItemRunD3D9Click(void);
void onMenuItemRunNoDebugClick(void);
void onMenuItemRunWindowedClick(void);
void onMenuItemRunCustomClick(void);
void onMenuItemFolderSizeClick(void);
void onMenuItemHashSHA1Click(void);
void onMenuItemHashSHA256Click(void);
void onMenuItemProcessManagerClick(void);

static struct ContextMenuItem cmiLauncherBoost = {NULL, &onMenuItemLauncherBoostClick, NULL};
static struct ContextMenuItem cmiLauncherBoostAggressive = {NULL, &onMenuItemLauncherBoostAggressiveClick, NULL};
static struct ContextMenuItem cmiRunDX11 = {NULL, &onMenuItemRunDX11Click, NULL};
static struct ContextMenuItem cmiRunD3D9 = {NULL, &onMenuItemRunD3D9Click, NULL};
static struct ContextMenuItem cmiRunNoDebug = {NULL, &onMenuItemRunNoDebugClick, NULL};
static struct ContextMenuItem cmiRunWindowed = {NULL, &onMenuItemRunWindowedClick, NULL};
static struct ContextMenuItem cmiRunCustom = {NULL, &onMenuItemRunCustomClick, NULL};
static struct ContextMenuItem cmiHashSHA1 = {NULL, &onMenuItemHashSHA1Click, NULL};
static struct ContextMenuItem cmiHashSHA256 = {NULL, &onMenuItemHashSHA256Click, NULL};
static struct ContextMenuItem cmiLauncherRunWith = {NULL, &onMenuItemLauncherRunWithClick, NULL};
static struct ContextMenuItem cmiLauncherChoose = {NULL, &onMenuItemLauncherChooseClick, NULL};
static struct ContextMenuItem cmiDiff = {NULL, &onMenuItemDiffClick, NULL};

static WNDPROC OrigWndProc;

// OLE drag and drop state
static POINT dragStartPt = {0};
static bool dragPending = false;
static int hoveredItem = -1;
static IDropTarget* g_dropTarget = NULL;
static HWND g_dropHwnd = NULL;
static bool gameMode = false;
static HMENU hContextMenu;
#define MAX_MENU_IDS 256
static struct ContextMenuItem* menuById[MAX_MENU_IDS];

static struct Pane panes[NUM_PANES] = {0};
static int activeIdx = 0;
static bool splitOn = false;
static struct Pane* g_sortPane = NULL;

static struct FileNode** selectedItems = NULL;
static int numSelectedItems = 0;

static struct ContextMenuItem** menuItems = NULL;
static int numMenuItems = 0;

// Append a fresh, individually-allocated context-menu item. Returning a stable pointer
// (rather than &menuItems[i]) keeps menu dwItemData valid across later reallocs.
static struct ContextMenuItem* addMenuItemSlot() {
    int index = numMenuItems++;
    menuItems = realloc(menuItems, numMenuItems * sizeof(struct ContextMenuItem*));
    struct ContextMenuItem* it = calloc(1, sizeof(struct ContextMenuItem));
    menuItems[index] = it;
    return it;
}

extern struct FileNode* currPathFileNode;
extern HINSTANCE globalHInstance;
extern HWND hwndMain;

// forward declarations (defined later in this file / in main.c)
static void refreshPane(struct Pane* p);
static void cvSetActiveByHwnd(HWND h);
static void updatePaneLabel(struct Pane* p);
void cvInvalidatePaneFrames(void); // main.c

static struct Pane* activePane() {
    return &panes[activeIdx];
}

static struct Pane* paneFromHwnd(HWND h) {
    for (int i = 0; i < NUM_PANES; i++) {
        if (panes[i].hwndList == h) return &panes[i];
    }
    return &panes[activeIdx];
}

// ---- exported accessors used by main.c / navbar.c ----
HWND cvActiveHwnd() {
    return panes[activeIdx].hwndList;
}

HWND cvPaneHwnd(int i) {
    return (i >= 0 && i < NUM_PANES) ? panes[i].hwndList : NULL;
}

int cvActiveIdx() {
    return activeIdx;
}

bool cvSplitOn() {
    return splitOn;
}

bool cvIsContentView(HWND h) {
    for (int i = 0; i < NUM_PANES; i++) {
        if (panes[i].hwndList == h) return true;
    }
    return false;
}

HWND cvPaneLabel(int i) {
    return (i >= 0 && i < NUM_PANES) ? panes[i].hwndPathLabel : NULL;
}

// A pane's path bar was clicked -> make that pane active. Returns true if h was a label.
bool cvActivatePaneByLabel(HWND h) {
    for (int i = 0; i < NUM_PANES; i++) {
        if (panes[i].hwndPathLabel == h) {
            cvSetActiveByHwnd(panes[i].hwndList);
            SetFocus(panes[i].hwndList);
            return true;
        }
    }
    return false;
}

static void updatePaneLabel(struct Pane* p) {
    if (!p->hwndPathLabel || !p->currPath) return;
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(p->currPath, path);
    wchar_t label[MAX_PATH + 2] = {0};
    swprintf_s(label, MAX_PATH + 2, L" %ls", path[0] ? path : p->currPath->name);
    SetWindowText(p->hwndPathLabel, label);
}

// ---- icon / type-name caches (large-folder scroll perf) ----------------------------------
// Resolving an icon + type name goes through SHGetFileInfo, a full shell/registry lookup under
// Wine. Without caching, a folder of 200 .txt files does 200 identical lookups, redone every time
// the folder is re-entered. These caches make each distinct extension cost one lookup, persisting
// across navigation. They are intentionally never cleared. The system-imagelist icon index is the
// same for large and small views, so a cached index is valid regardless of view style.

static int folderIconCached = 0;
static int folderIconIndex = 0;

#define EXT_ICON_CACHE_SIZE 256
static struct { wchar_t ext[24]; int icon; wchar_t typeName[64]; } extIconCache[EXT_ICON_CACHE_SIZE];
static int extIconCacheCount = 0;

// .exe/.lnk carry per-file embedded icons, so they can't share an extension entry — cache by path.
#define EXE_ICON_CACHE_SIZE 256
static struct { wchar_t path[MAX_PATH]; int icon; } exeIconCache[EXE_ICON_CACHE_SIZE];
static int exeIconCacheCount = 0;

static int findExtIconCache(const wchar_t* ext, const wchar_t** typeNameOut) {
    if (!ext) return -1;
    for (int i = 0; i < extIconCacheCount; i++) {
        if (wcsicmp(extIconCache[i].ext, ext) == 0) {
            if (typeNameOut) *typeNameOut = extIconCache[i].typeName;
            return extIconCache[i].icon;
        }
    }
    return -1;
}

static void addExtIconCache(const wchar_t* ext, int icon, const wchar_t* typeName) {
    if (!ext || extIconCacheCount >= EXT_ICON_CACHE_SIZE) return;
    wcsncpy_s(extIconCache[extIconCacheCount].ext, 24, ext, _TRUNCATE);
    extIconCache[extIconCacheCount].icon = icon;
    if (typeName) wcsncpy_s(extIconCache[extIconCacheCount].typeName, 64, typeName, _TRUNCATE);
    else extIconCache[extIconCacheCount].typeName[0] = L'\0';
    extIconCacheCount++;
}

static int findExeIconCache(const wchar_t* path) {
    if (!path) return -1;
    for (int i = 0; i < exeIconCacheCount; i++) {
        if (wcsicmp(exeIconCache[i].path, path) == 0) return exeIconCache[i].icon;
    }
    return -1;
}

static void addExeIconCache(const wchar_t* path, int icon) {
    if (!path || exeIconCacheCount >= EXE_ICON_CACHE_SIZE) return;
    wcsncpy_s(exeIconCache[exeIconCacheCount].path, MAX_PATH, path, _TRUNCATE);
    exeIconCache[exeIconCacheCount].icon = icon;
    exeIconCacheCount++;
}

// Size + mtime are captured once during enumeration (buildChildNodes) straight out of the
// WIN32_FIND_DATA, so filling a list item is now a pure copy — no per-file GetFileAttributesEx.
// This is the key large-folder win: a 5k-file folder no longer does 5k sync stat round-trips
// through Wine onto FUSE storage at load time.
static void fillFileInfo(struct FileNode* node, struct ListItem* item) {
    item->size = node->size;
    memcpy(&item->modifiedTime, &node->modifiedTime, sizeof(FILETIME));
}

static void updateStatusbar(struct Pane* p) {
    if (p != activePane()) return;
    wchar_t sizeStr[32] = {0};
    formatFileSize(p->totalSize, sizeStr);

    // Memory usage
    wchar_t memStr[48] = {0};
    MEMORYSTATUSEX msx = {0};
    msx.dwLength = sizeof(msx);
    if (GlobalMemoryStatusEx(&msx)) {
        wchar_t used[16], total[16];
        formatFileSize(msx.ullTotalPhys - msx.ullAvailPhys, used);
        formatFileSize(msx.ullTotalPhys, total);
        swprintf_s(memStr, 48, L"  |  %ls: %ls/%ls", lc_str.memory, used, total);
    }

    // Free space of current drive
    wchar_t freeStr[48] = {0};
    if (currPathFileNode) {
        wchar_t path[MAX_PATH] = {0};
        getFileNodePath(currPathFileNode, path);
        if (wcslen(path) == 2 && path[1] == L':') wcscat_s(path, MAX_PATH, L"\\");
        ULARGE_INTEGER fb, tb, tf;
        if (GetDiskFreeSpaceExW(path, &fb, &tb, &tf)) {
            wchar_t fs[16], ts[16];
            formatFileSize(fb.QuadPart, fs);
            formatFileSize(tb.QuadPart, ts);
            swprintf_s(freeStr, 48, L"  |  %ls: %ls/%ls", lc_str.free_space, fs, ts);
        }
    }

    // 4-part status bar: items | size | memory | free space
    wchar_t part0[80], part1[80];
    swprintf_s(part0, 80, L"%d %ls", p->numItems, lc_str.items);
    swprintf_s(part1, 80, L"%ls", sizeStr);
    setStatusbarParts(part0, part1, memStr[0] ? memStr : L"", freeStr[0] ? freeStr : L"");
}

static void freeMenuItems() {
    if (menuItems) {
        for (int i = 0; i < numMenuItems; i++) {
            struct ContextMenuItem* it = menuItems[i];
            if (it->cmdData) free(it->cmdData);
            if (it->openExe) {
                free(it->openExe);
                if (it->openFile) free(it->openFile);
                if (it->text) free(it->text);
            }
            free(it);
        }
        free(menuItems);
        menuItems = NULL;
    }
    numMenuItems = 0;
}

static void clearPane(struct Pane* p) {
    ListView_SetItemCountEx(p->hwndList, 0, 0);
    ListView_DeleteColumn(p->hwndList, COLUMN_PATH_IDX);

    if (p->items) {
        for (int i = 0; i < p->numItems; i++) {
            if (p->items[i].path) {
                free(p->items[i].path);
                p->items[i].path = NULL;
            }
        }
        free(p->items);
        p->items = NULL;
    }
    p->numItems = 0;
    p->totalSize = 0;

    freeMenuItems();
}

void clearContentView() {
    clearPane(activePane());
}

static void execCommandLine(wchar_t *command) {
    SHELLEXECUTEINFO shExecInfo = {0};
    shExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    shExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shExecInfo.hwnd = hwndMain;
    shExecInfo.lpVerb = NULL;
    shExecInfo.lpFile = L"C:\\windows\\system32\\cmd.exe";
    shExecInfo.lpParameters = command;
    shExecInfo.lpDirectory = NULL;
    shExecInfo.nShow = SW_SHOW;
    shExecInfo.hInstApp = NULL;
    ShellExecuteEx(&shExecInfo);
    WaitForSingleObject(shExecInfo.hProcess, INFINITE);
    CloseHandle(shExecInfo.hProcess);
}

LRESULT CALLBACK ContentViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (themeScrollbarsHookBefore(hwnd, msg, wParam)) return 0;
    switch (msg) {
        case WM_SETFOCUS: {
            cvSetActiveByHwnd(hwnd);
            break;
        }
        case WM_LBUTTONDOWN: {
            // Let ListView update selection first, then arm drag detection
            OrigWndProc(hwnd, msg, wParam, lParam);
            dragStartPt.x = (short)LOWORD(lParam);
            dragStartPt.y = (short)HIWORD(lParam);
            dragPending = true;
            updateSelectedItems();
            return 0;
        }
        case WM_MOUSEMOVE: {
            // Hover tracking for row highlight
            if (!(wParam & MK_LBUTTON)) {
                LVHITTESTINFO ht;
                ht.pt.x = (short)LOWORD(lParam);
                ht.pt.y = (short)HIWORD(lParam);
                ListView_HitTest(hwnd, &ht);
                int newHover = (ht.flags & LVHT_ONITEM) ? ht.iItem : -1;
                if (newHover != hoveredItem) {
                    int old = hoveredItem;
                    hoveredItem = newHover;
                    if (old >= 0) ListView_RedrawItems(hwnd, old, old);
                    if (newHover >= 0) ListView_RedrawItems(hwnd, newHover, newHover);
                }
            }
            // Drag detection
            if (dragPending && (wParam & MK_LBUTTON)) {
                int dx = abs((short)LOWORD(lParam) - dragStartPt.x);
                int dy = abs((short)HIWORD(lParam) - dragStartPt.y);
                if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG)) {
                    dragPending = false;
                    if (numSelectedItems > 0) startFileDrag(hwnd);
                }
            } else if (!(wParam & MK_LBUTTON)) {
                dragPending = false;
            }
            break;
        }
        case WM_LBUTTONUP: {
            dragPending = false;
            ReleaseCapture();
            break;
        }
        case WM_COMMAND: {
            if ((HWND)lParam == 0) {
                int cmdId = LOWORD(wParam);
                if (cmdId >= 0 && cmdId < MAX_MENU_IDS) {
                    struct ContextMenuItem* cmItem = menuById[cmdId];
                    if (cmItem) {
                        if (cmItem->openExe) {
                            wchar_t params[MAX_PATH + 4] = {0};
                            swprintf_s(params, MAX_PATH + 4, L"\"%ls\"", cmItem->openFile ? cmItem->openFile : L"");
                            ShellExecuteW(hwndMain, L"open", cmItem->openExe, params, NULL, SW_SHOW);
                        }
                        else if (cmItem->cmdData) {
                            wchar_t command[MAX_PATH];
                            wcscpy_s(command, MAX_PATH, L"/C ");
                            wcscat_s(command, MAX_PATH, cmItem->cmdData);
                            execCommandLine(command);
                            navigateRefresh();
                        }
                        else if (cmItem->proc) cmItem->proc();
                    }
                }
            }
            break;
        }
        case MSG_ADD_ITEM: {
            struct Pane* p = paneFromHwnd(hwnd);
            if (p->searchData != NULL && p->searchData->active) {
                struct FileNode* node = (struct FileNode*)lParam;
                int index = p->numItems++;
                p->items = realloc(p->items, p->numItems * sizeof(struct ListItem));
                struct ListItem* item = &p->items[index];
                item->node = node;
                item->path = NULL;
                item->loaded = false;

                fillFileInfo(node, item);
                p->totalSize += item->size;

                ListView_SetItemCountEx(p->hwndList, p->numItems, LVSICF_NOINVALIDATEALL);
                updateStatusbar(p);
            }
            break;
        }
        case MSG_SEARCH_DONE: {
            struct Pane* p = paneFromHwnd(hwnd);
            if (p->searchData) {
                p->searchData->active = false;
                bool canceled = p->searchData->canceled;
                free(p->searchData);
                p->searchData = NULL;
                if (canceled) {
                    refreshPane(p);
                }
                else updateStatusbar(p);
            }
            break;
        }
    }
    LRESULT result = OrigWndProc(hwnd, msg, wParam, lParam);
    // Wine draws the list view's own scrollbars light; repaint them dark on top.
    themeScrollbarsHookAfter(hwnd, msg);
    return result;
}

static void updateSelectedItems(void) {
    struct Pane* p = activePane();
    MEMFREE(selectedItems);
    numSelectedItems = 0;

    int i = ListView_GetNextItem(p->hwndList, -1, LVNI_SELECTED);
    while (i != -1) {
        int index = numSelectedItems++;
        selectedItems = realloc(selectedItems, numSelectedItems * sizeof(struct FileNode*));
        selectedItems[index] = p->items[i].node;
        i = ListView_GetNextItem(p->hwndList, i, LVNI_SELECTED);
    }
    previewUpdate();
}

// Public: fill path/type of the first selected item in the active pane.
// Used by the preview pane. Returns without modifying outputs if nothing selected.
void cvGetFirstSelected(wchar_t* path, int* type) {
    if (path) path[0] = L'\0';
    if (type) *type = -1;
    if (numSelectedItems < 1 || !selectedItems[0]) return;
    if (path) getFileNodePath(selectedItems[0], path);
    if (type) *type = selectedItems[0]->type;
}

static void addContextMenuItem(HMENU hMenu, int id, struct ContextMenuItem* cmItem, bool separate) {
    MENUITEMINFO item = {0};
    item.cbSize = sizeof(MENUITEMINFO);
    item.fMask = MIIM_TYPE | MIIM_DATA | MIIM_ID;
    item.fType = MFT_STRING;
    item.dwTypeData = cmItem->text;
    item.cch = cmItem->text ? wcslen(cmItem->text) : 0;
    item.wID = id;
    item.dwItemData = (ULONG_PTR)cmItem;
    if (id >= 0 && id < MAX_MENU_IDS) menuById[id] = cmItem;

    InsertMenuItem(hMenu, -1, TRUE, &item);

    if (separate) {
        item.fMask = MIIM_TYPE;
        item.fType = MFT_SEPARATOR;
        InsertMenuItem(hMenu, -1, TRUE, &item);
    }
}

static void createContextMenuFromRegistry(int* id) {
    HKEY hkeyContextMenu, hkeyItem;
    if (RegOpenKey(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM\\ContextMenu", &hkeyContextMenu) != ERROR_SUCCESS) return;

    WCHAR itemName[30] = {0};
    WCHAR subitemName[100] = {0};
    WCHAR itemValue[MAX_PATH];
    DWORD i, j, itemNameLen, itemValueLen;

    i = 0;
    while (i < 10) {
        itemNameLen = 30;
        if (RegEnumKey(hkeyContextMenu, i++, itemName, itemNameLen) != ERROR_SUCCESS) break;
        if (RegOpenKey(hkeyContextMenu, itemName, &hkeyItem) == ERROR_SUCCESS) {
            MENUITEMINFO item = {0};
            item.cbSize = sizeof(MENUITEMINFO);
            item.fMask = MIIM_TYPE | MIIM_ID | MIIM_SUBMENU;
            item.fType = MFT_STRING;
            item.dwTypeData = itemName;
            item.cch = itemNameLen;
            item.wID = ++(*id);

            HMENU hSubmenu = CreatePopupMenu();

            j = 0;
            while (j < 10) {
                itemNameLen = 100;
                itemValueLen = MAX_PATH;
                if (RegEnumValue(hkeyItem, j++, subitemName, &itemNameLen, NULL, NULL, (LPBYTE)itemValue, &itemValueLen) != ERROR_SUCCESS) break;

                struct ContextMenuItem* cmItem = addMenuItemSlot();
                cmItem->text = subitemName;
                cmItem->proc = NULL;

                wchar_t *cmdData = malloc(1024);
                wcscpy_s(cmdData, MAX_PATH, itemValue);

                wchar_t path[MAX_PATH] = {0};
                getFileNodePath(selectedItems[0], path);
                cmdData = strReplace(cmdData, L"%FILE%", path, true);

                wchar_t basename[80] = {0};
                getBasenameFromPath(path, basename, true);
                cmdData = strReplace(cmdData, L"%BASENAME%", basename, true);

                getFileNodePath(selectedItems[0]->parent, path);
                cmdData = strReplace(cmdData, L"%DIR%", path, true);

                cmItem->cmdData = cmdData;
                addContextMenuItem(hSubmenu, (*id)++, cmItem, false);
            }

            item.hSubMenu = hSubmenu;

            InsertMenuItem(hContextMenu, -1, TRUE, &item);

            item.fMask = MIIM_TYPE;
            item.fType = MFT_SEPARATOR;
            InsertMenuItem(hContextMenu, -1, TRUE, &item);

            RegCloseKey(hkeyItem);
        }
    }

    RegCloseKey(hkeyContextMenu);
}

static void createCDDriveContextMenu(int* id) {
    HMENU hSubmenu = CreatePopupMenu();

    wchar_t currentISOPath[MAX_PATH] = {0};
    int currentISOPathLen = MAX_PATH;
    HKEY hkey;
    if (RegOpenKey(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM\\CurrentISOPath", &hkey) == ERROR_SUCCESS) {
        RegQueryValue(hkey, NULL, currentISOPath, (PLONG)&currentISOPathLen);
        RegCloseKey(hkey);
    }

    wchar_t itemText[64] = {0};
    swprintf_s(itemText, MAX_PATH, L"%ls <%ls>", lc_str.load_iso_image, currentISOPathLen != MAX_PATH ? currentISOPath : lc_str.no_media);
    cmiLoadISOImage.text = itemText;
    addContextMenuItem(hSubmenu, (*id)++, &cmiLoadISOImage, false);
    cmiLoadISOImage.text = NULL;

    addContextMenuItem(hSubmenu, (*id)++, &cmiUnloadISOImage, false);

    MENUITEMINFO item = {0};
    item.cbSize = sizeof(MENUITEMINFO);
    item.fMask = MIIM_TYPE | MIIM_ID | MIIM_SUBMENU;
    item.fType = MFT_STRING;
    swprintf_s(itemText, 64, L"%ls [X:]", lc_str.cd_drive);
    item.dwTypeData = itemText;
    item.cch = wcslen(itemText);
    item.wID = ++(*id);

    item.hSubMenu = hSubmenu;
    InsertMenuItem(hContextMenu, -1, TRUE, &item);

    item.fMask = MIIM_TYPE;
    item.fType = MFT_SEPARATOR;
    InsertMenuItem(hContextMenu, -1, TRUE, &item);
}

// Pull the executable path out of a registered "shell\open\command" value, e.g.
//   "C:\Program Files\App\app.exe" "%1"   ->   C:\Program Files\App\app.exe
static void extractExeFromCommand(const wchar_t* cmd, wchar_t* exeOut) {
    exeOut[0] = L'\0';
    const wchar_t* p = cmd;
    while (*p == L' ') p++;

    const wchar_t* start;
    const wchar_t* end;
    if (*p == L'"') {
        start = ++p;
        end = wcschr(p, L'"');
        if (!end) return;
    }
    else {
        start = p;
        end = wcschr(p, L' ');
        if (!end) end = p + wcslen(p);
    }

    int n = (int)(end - start);
    if (n <= 0 || n >= MAX_PATH) return;
    wcsncpy(exeOut, start, n);
    exeOut[n] = L'\0';
}

// "Open with" submenu: best-effort list of registered applications (HKCR\Applications)
// plus a "Choose another program..." entry that opens Wine's Open-With dialog. Robust when
// the registry list is sparse (still offers the dialog).
static void createOpenWithMenu(int* id) {
    wchar_t filePath[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], filePath);

    HMENU hSubmenu = CreatePopupMenu();
    int count = 0;

    HKEY hApps;
    if (RegOpenKeyW(HKEY_CLASSES_ROOT, L"Applications", &hApps) == ERROR_SUCCESS) {
        wchar_t appName[128];
        DWORD i = 0, len;
        while (count < 12) {
            len = 128;
            if (RegEnumKeyExW(hApps, i++, appName, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;

            wchar_t cmdKey[300] = {0};
            swprintf_s(cmdKey, 300, L"Applications\\%ls\\shell\\open\\command", appName);
            HKEY hCmd;
            if (RegOpenKeyW(HKEY_CLASSES_ROOT, cmdKey, &hCmd) != ERROR_SUCCESS) continue;

            wchar_t cmdVal[MAX_PATH] = {0};
            DWORD cl = sizeof(cmdVal);
            wchar_t exe[MAX_PATH] = {0};
            if (RegQueryValueExW(hCmd, NULL, NULL, NULL, (LPBYTE)cmdVal, &cl) == ERROR_SUCCESS) {
                extractExeFromCommand(cmdVal, exe);
            }
            RegCloseKey(hCmd);
            if (!exe[0] || !isPathExists(exe)) continue;

            struct ContextMenuItem* cmItem = addMenuItemSlot();
            cmItem->text = wcsdup(appName);
            cmItem->proc = NULL;
            cmItem->cmdData = NULL;
            cmItem->openExe = wcsdup(exe);
            cmItem->openFile = wcsdup(filePath);

            addContextMenuItem(hSubmenu, (*id)++, cmItem, false);
            count++;
        }
        RegCloseKey(hApps);
    }

    if (count > 0) {
        MENUITEMINFO sep = {0};
        sep.cbSize = sizeof(MENUITEMINFO);
        sep.fMask = MIIM_TYPE;
        sep.fType = MFT_SEPARATOR;
        InsertMenuItem(hSubmenu, -1, TRUE, &sep);
    }
    // Merge the external-launcher actions into this submenu so every "run via
    // another program" choice lives in one place instead of duplicating items.
    {
        wchar_t savedLauncher[MAX_PATH] = {0};
        if (launcherGetSaved(savedLauncher))
            addContextMenuItem(hSubmenu, (*id)++, &cmiLauncherRunWith, false);
        addContextMenuItem(hSubmenu, (*id)++, &cmiLauncherChoose, count > 0);
    }
    addContextMenuItem(hSubmenu, (*id)++, &cmiChooseProgram, false);

    MENUITEMINFO item = {0};
    item.cbSize = sizeof(MENUITEMINFO);
    item.fMask = MIIM_TYPE | MIIM_ID | MIIM_SUBMENU;
    item.fType = MFT_STRING;
    item.dwTypeData = lc_str.open_with;
    item.cch = wcslen(lc_str.open_with);
    item.wID = ++(*id);
    item.hSubMenu = hSubmenu;
    InsertMenuItem(hContextMenu, -1, TRUE, &item);
}

static void createContextMenu(enum ContextMenuType type) {
    freeMenuItems();
    memset(menuById, 0, sizeof(menuById));

    HMENU hMenu = CreatePopupMenu();
    hContextMenu = hMenu;

    int id = 0;

    if (type == MENU_SINGLE || type == MENU_MULTIPLE) {
        if (type == MENU_SINGLE) {
            if (selectedItems[0]->type == TYPE_FILE) {
                addContextMenuItem(hMenu, id++, &cmiOpen, false);
                addContextMenuItem(hMenu, id++, &cmiOpenAsAdmin, false);
                // Archive extraction (7z). Only shown for recognised archive types.
                {
                    wchar_t apath[MAX_PATH] = {0};
                    getFileNodePath(selectedItems[0], apath);
                    if (isArchiveExt(apath)) {
                        addContextMenuItem(hMenu, id++, &cmiExtractHere, false);
                        addContextMenuItem(hMenu, id++, &cmiExtractToFolder, true);
                    }
                }
                addContextMenuItem(hMenu, id++, &cmiLauncherBoost, false);
                addContextMenuItem(hMenu, id++, &cmiLauncherBoostAggressive, false);
                {
                    // "Run with args" submenu: generic presets + custom input.
                    // No engine labels — args are usable across engines where
                    // supported; custom input covers everything else.
                    HMENU hArgs = CreatePopupMenu();
                    addContextMenuItem(hArgs, id++, &cmiRunCustom, true);
                    addContextMenuItem(hArgs, id++, &cmiRunDX11, false);
                    addContextMenuItem(hArgs, id++, &cmiRunD3D9, false);
                    addContextMenuItem(hArgs, id++, &cmiRunNoDebug, false);
                    addContextMenuItem(hArgs, id++, &cmiRunWindowed, true);
                    AppendMenuW(hMenu, MF_POPUP | MF_STRING, (UINT_PTR)hArgs, lc_str.run_with_args);
                }
                createOpenWithMenu(&id);
                addContextMenuItem(hMenu, id++, &cmiEdit, true);
                createCDDriveContextMenu(&id);
                createContextMenuFromRegistry(&id);
            }
            else addContextMenuItem(hMenu, id++, &cmiOpen, true);
        }
        addContextMenuItem(hMenu, id++, &cmiCut, false);
        addContextMenuItem(hMenu, id++, &cmiCopy, true);
        addContextMenuItem(hMenu, id++, &cmiCreateShortcut, false);
        if (type == MENU_MULTIPLE) addContextMenuItem(hMenu, id++, &cmiBatchRename, false);
        addContextMenuItem(hMenu, id++, &cmiDelete, false);

        if (type == MENU_SINGLE) {
            addContextMenuItem(hMenu, id++, &cmiRename, true);
            addContextMenuItem(hMenu, id++, &cmiCopyPath, false);
            addContextMenuItem(hMenu, id++, &cmiExtractIcon, false);
            addContextMenuItem(hMenu, id++, &cmiMD5, false);
            addContextMenuItem(hMenu, id++, &cmiHashSHA1, false);
            addContextMenuItem(hMenu, id++, &cmiHashSHA256, false);
            addContextMenuItem(hMenu, id++, &cmiViewText, false);
            addContextMenuItem(hMenu, id++, &cmiFolderSize, false);
            addContextMenuItem(hMenu, id++, &cmiCopyTo, false);
            addContextMenuItem(hMenu, id++, &cmiMoveTo, false);
            addContextMenuItem(hMenu, id++, &cmiAddToFav, false);
            if (splitOn) {
                struct Pane* other = (activeIdx == 0) ? &panes[1] : &panes[0];
                if (ListView_GetSelectedCount(other->hwndList) > 0)
                    addContextMenuItem(hMenu, id++, &cmiDiff, false);
            }
            addContextMenuItem(hMenu, id++, &cmiProperties, false);
        }
    }
    else {
        addContextMenuItem(hMenu, id++, &cmiPaste, false);
        addContextMenuItem(hMenu, id++, &cmiPasteShortcut, true);
        createCDDriveContextMenu(&id);
        addContextMenuItem(hMenu, id++, &cmiNewFolder, false);
        addContextMenuItem(hMenu, id++, &cmiNewFile, false);
        addContextMenuItem(hMenu, id++, &cmiNewTxt, false);
        addContextMenuItem(hMenu, id++, &cmiNewBat, false);
        addContextMenuItem(hMenu, id++, &cmiNewReg, true);
        addContextMenuItem(hMenu, id++, &cmiOpenCmd, false);
    }

    POINT cursor;
    GetCursorPos(&cursor);
    TrackPopupMenu(hMenu, 0, cursor.x, cursor.y, 0, activePane()->hwndList, NULL);
}

LRESULT contentViewNotify(NMHDR* nmhdr) {
    struct Pane* p = paneFromHwnd(nmhdr->hwndFrom);

    switch (nmhdr->code) {
        case NM_SETFOCUS: {
            cvSetActiveByHwnd(nmhdr->hwndFrom);
            break;
        }
        case NM_CUSTOMDRAW: {
            LPNMLVCUSTOMDRAW lpcd = (LPNMLVCUSTOMDRAW)nmhdr;
            // Custom drawing is for report/details view only. Icon/list views use default rendering.
            if (p->viewStyle != STYLE_DETAILS) return CDRF_DODEFAULT;
            if (lpcd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (lpcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                HDC hdc = lpcd->nmcd.hdc;
                int itemIdx = (int)lpcd->nmcd.dwItemSpec;
                if (itemIdx < 0 || itemIdx >= p->numItems) break;
                struct ListItem* item = &p->items[itemIdx];
                RECT rc = lpcd->nmcd.rc;
                int rowH = rc.bottom - rc.top;

                // Force-load if needed (LVS_OWNERDATA may call customdraw before getdispinfo)
                if (!item->loaded) fillFileInfo(item->node, item);

                BOOL selected = (ListView_GetItemState(p->hwndList, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                BOOL hovered = (itemIdx == hoveredItem);

                // Use system colors for proper dark/light theme adaptation
                COLORREF bgColor, textColor;
                if (selected) {
                    bgColor = GetSysColor(COLOR_HIGHLIGHT);
                    textColor = GetSysColor(COLOR_HIGHLIGHTTEXT);
                    HBRUSH selBrush = CreateSolidBrush(bgColor);
                    FillRect(hdc, &rc, selBrush);
                    DeleteObject(selBrush);
                } else {
                    COLORREF winBg = GetSysColor(COLOR_WINDOW);
                    if (hovered) {
                        // Hover: blend highlight color with window bg (30% highlight)
                        COLORREF hl = GetSysColor(COLOR_HIGHLIGHT);
                        bgColor = RGB(
                            (GetRValue(winBg)*7 + GetRValue(hl)*3)/10,
                            (GetGValue(winBg)*7 + GetGValue(hl)*3)/10,
                            (GetBValue(winBg)*7 + GetBValue(hl)*3)/10);
                    } else if (itemIdx % 2 == 1) {
                        // Alternate row: slightly darker/lighter than window bg
                        int r=GetRValue(winBg), g=GetGValue(winBg), b=GetBValue(winBg);
                        int adj = (r+g+b > 384) ? -12 : 16;  // light bg -> darker, dark bg -> lighter
                        bgColor = RGB(max(0,min(255,r+adj)), max(0,min(255,g+adj)), max(0,min(255,b+adj)));
                    } else bgColor = winBg;
                    textColor = GetSysColor(COLOR_WINDOWTEXT);
                    HBRUSH bgBrush = CreateSolidBrush(bgColor);
                    FillRect(hdc, &rc, bgBrush);
                    DeleteObject(bgBrush);
                }

                int w0 = SendMessage(p->hwndList, LVM_GETCOLUMNWIDTH, 0, 0);
                int w1 = SendMessage(p->hwndList, LVM_GETCOLUMNWIDTH, 1, 0);
                int w2 = SendMessage(p->hwndList, LVM_GETCOLUMNWIDTH, 2, 0);

                HIMAGELIST himl = ListView_GetImageList(p->hwndList, LVSIL_SMALL);
                if (himl && item->icon >= 0) {
                    ImageList_Draw(himl, item->icon, hdc, rc.left + 4, rc.top + (rowH - 16) / 2, ILD_TRANSPARENT);
                }

                SetTextColor(hdc, textColor);
                SetBkMode(hdc, TRANSPARENT);
                HGDIOBJ oldFont = SelectObject(hdc, getUIFont());

                RECT nameR = {rc.left + 26, rc.top, rc.left + w0 - 4, rc.bottom};
                DrawTextW(hdc, item->node->name, -1, &nameR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                RECT typeR = {rc.left + w0 + 4, rc.top, rc.left + w0 + w1 - 4, rc.bottom};
                DrawTextW(hdc, item->type, -1, &typeR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                int sizeX = rc.left + w0 + w1;
                if (item->node->type == TYPE_DRIVE) {
                    wchar_t dp[MAX_PATH] = {0};
                    getFileNodePath(item->node, dp);
                    if (wcslen(dp) == 2 && dp[1] == L':') wcscat_s(dp, MAX_PATH, L"\\");
                    ULARGE_INTEGER fb, tb, tf;
                    double usedGB = 0, totalGB = 0;
                    if (GetDiskFreeSpaceExW(dp, &fb, &tb, &tf)) {
                        usedGB = (double)(tb.QuadPart - fb.QuadPart) / 1073741824.0;
                        totalGB = (double)tb.QuadPart / 1073741824.0;
                    }
                    // Capacity bar fills the entire size column (Windows Explorer style)
                    double pct = (totalGB > 0) ? usedGB / totalGB : 0;
                    if (pct > 1.0) pct = 1.0;
                    // Phone storage is usually >70%; shift thresholds so red only
                    // means genuinely low (<5% free), yellow = warning.
                    COLORREF barColor = (pct < 0.8) ? RGB(0,150,0) : (pct < 0.95 ? RGB(220,170,0) : RGB(210,50,50));
                    // Capacity bar: bar fills left 50% of the (150px) size
                    // column, full "used/total GB" text in the right 50%.
                    int barX = sizeX + 2;
                    int barW = (w2 - 4) * 50 / 100;
                    int barY = rc.top + 3;
                    int barH = rowH - 6;
                    // Track (light gray background)
                    HBRUSH trackBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
                    RECT trackR = {barX, barY, barX + barW, barY + barH};
                    FillRect(hdc, &trackR, trackBrush);
                    DeleteObject(trackBrush);
                    // Filled portion
                    int fillW = (int)(barW * pct);
                    if (fillW > 0) {
                        HBRUSH fillBrush = CreateSolidBrush(barColor);
                        RECT fillR = {barX, barY, barX + fillW, barY + barH};
                        FillRect(hdc, &fillR, fillBrush);
                        DeleteObject(fillBrush);
                    }
                    // Percentage on the bar
                    wchar_t pctText[16];
                    swprintf_s(pctText, 16, L"%d%%", (int)(pct * 100));
                    SetTextColor(hdc, RGB(255,255,255));
                    SetBkMode(hdc, TRANSPARENT);
                    RECT pctR = {barX, barY, barX + barW, barY + barH};
                    DrawTextW(hdc, pctText, -1, &pctR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    // Full used/total GB to the right of the bar.
                    wchar_t capText[32];
                    swprintf_s(capText, 32, L"%.0f/%.0f GB", usedGB, totalGB);
                    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                    RECT textR = {barX + barW + 4, rc.top, sizeX + w2 - 2, rc.bottom};
                    DrawTextW(hdc, capText, -1, &textR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                } else if (item->node->type == TYPE_FILE) {
                    RECT sizeR = {sizeX + 4, rc.top, sizeX + w2 - 4, rc.bottom};
                    DrawTextW(hdc, item->formattedSize, -1, &sizeR, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }

                if (item->node->type == TYPE_FILE && item->formattedDate[0]) {
                    RECT dateR = {rc.left + w0 + w1 + w2 + 4, rc.top, rc.right - 4, rc.bottom};
                    DrawTextW(hdc, item->formattedDate, -1, &dateR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }

                SelectObject(hdc, oldFont);
                return CDRF_SKIPDEFAULT;
            }
            break;
        }
        case LVN_GETDISPINFO: {
            NMLVDISPINFO* nmlvdi = (NMLVDISPINFO*)nmhdr;
            UINT mask = nmlvdi->item.mask;
            struct ListItem* item = &p->items[nmlvdi->item.iItem];

            if (!item->loaded) {
                bool largeIcon = (p->viewStyle == STYLE_LARGE_ICON);

                if (item->node->type == TYPE_DIR) {
                    // Every folder shares one system icon — resolve it a single time.
                    if (!folderIconCached) {
                        wchar_t path[MAX_PATH] = {0};
                        getFileNodePath(item->node, path);
                        struct FileInfo fi = {0};
                        getFileInfo(path, TYPE_DIR, largeIcon, &fi);
                        folderIconIndex = fi.icon;
                        folderIconCached = 1;
                    }
                    item->icon = folderIconIndex;
                    wcscpy_s(item->type, 64, lc_str.folder);
                }
                else if (item->node->type == TYPE_FILE) {
                    wchar_t* ext = wcsrchr(item->node->name, L'.'); // includes the dot, e.g. ".txt"
                    bool isExeOrLnk = ext && (wcsicmp(ext, L".exe") == 0 || wcsicmp(ext, L".lnk") == 0);

                    if (isExeOrLnk) {
                        // exe/lnk carry per-file icons: cache by full path, not extension.
                        wchar_t path[MAX_PATH] = {0};
                        getFileNodePath(item->node, path);
                        int cached = findExeIconCache(path);
                        if (cached >= 0) {
                            item->icon = cached;
                        } else {
                            struct FileInfo fi = {0};
                            getFileInfo(path, TYPE_FILE, largeIcon, &fi);
                            item->icon = fi.icon;
                            addExeIconCache(path, fi.icon);
                        }
                        wcscpy_s(item->type, 64, wcsicmp(ext, L".exe") == 0 ? lc_str.application : lc_str.shortcut);
                    } else {
                        // Everything else keys off the extension (one shell lookup per distinct ext).
                        const wchar_t* cachedType = NULL;
                        int cached = findExtIconCache(ext, &cachedType);
                        if (cached >= 0) {
                            item->icon = cached;
                            wcscpy_s(item->type, 64, (cachedType && cachedType[0]) ? cachedType : lc_str.file);
                        } else {
                            wchar_t path[MAX_PATH] = {0};
                            getFileNodePath(item->node, path);
                            struct FileInfo fi = {0};
                            getFileInfo(path, TYPE_FILE, largeIcon, &fi);
                            item->icon = fi.icon;
                            wcscpy_s(item->type, 64, fi.typeName);
                            if (ext) addExtIconCache(ext, fi.icon, fi.typeName);
                        }
                    }

                    formatFileSize(item->size, item->formattedSize);

                    SYSTEMTIME systemTime = {0};
                    FILETIME localFiletime;
                    if (FileTimeToLocalFileTime(&item->modifiedTime, &localFiletime) && FileTimeToSystemTime(&localFiletime, &systemTime)) {
                        formatModifiedDate(systemTime.wMonth, systemTime.wDay, systemTime.wYear, systemTime.wHour, systemTime.wMinute, item->formattedDate, 32);
                    }
                }
                else {
                    // Drives / desktop / computer / personal — only a handful, not worth caching.
                    wchar_t path[MAX_PATH] = {0};
                    getFileNodePath(item->node, path);
                    struct FileInfo fi = {0};
                    getFileInfo(path, item->node->type, largeIcon, &fi);
                    item->icon = fi.icon;
                    wcscpy_s(item->type, 64, fi.typeName);
                }

                item->loaded = true;
            }

            if (mask & LVIF_STATE) {
                nmlvdi->item.state = 0;
            }

            if (mask & LVIF_IMAGE) {
                nmlvdi->item.iImage = item->icon;
            }

            if (mask & LVIF_TEXT) {
                switch (nmlvdi->item.iSubItem) {
                    case COLUMN_NAME_IDX:
                        if (item->node->type == TYPE_DRIVE && p->viewStyle != STYLE_DETAILS) {
                            // In icon/list views, show drive letter + capacity inline
                            static wchar_t driveLabel[48];
                            wchar_t dp[MAX_PATH] = {0};
                            getFileNodePath(item->node, dp);
                            if (wcslen(dp) == 2 && dp[1] == L':') wcscat_s(dp, MAX_PATH, L"\\");
                            ULARGE_INTEGER fb, tb, tf;
                            if (GetDiskFreeSpaceExW(dp, &fb, &tb, &tf) && tb.QuadPart > 0) {
                                double usedGB = (double)(tb.QuadPart - fb.QuadPart) / 1073741824.0;
                                double totalGB = (double)tb.QuadPart / 1073741824.0;
                                swprintf_s(driveLabel, 48, L"%ls  %.0f/%.0fG", item->node->name, usedGB, totalGB);
                            } else {
                                swprintf_s(driveLabel, 48, L"%ls", item->node->name);
                            }
                            nmlvdi->item.pszText = driveLabel;
                        } else {
                            nmlvdi->item.pszText = item->node->name;
                        }
                        break;
                    case COLUMN_TYPE_IDX:
                        nmlvdi->item.pszText = item->type;
                        break;
                    case COLUMN_SIZE_IDX:
                        if (item->node->type == TYPE_FILE) {
                            nmlvdi->item.pszText = item->formattedSize;
                        } else if (item->node->type == TYPE_DRIVE) {
                            static wchar_t capStr[32];
                            wchar_t dp[MAX_PATH] = {0};
                            getFileNodePath(item->node, dp);
                            // Drive node name is "C:" without trailing slash; GetDiskFreeSpaceExW needs "C:\"
                            if (wcslen(dp) == 2 && dp[1] == L':') wcscat_s(dp, MAX_PATH, L"\\");
                            ULARGE_INTEGER fb, tb, tf;
                            if (GetDiskFreeSpaceExW(dp, &fb, &tb, &tf)) {
                                double usedGB = (double)(tb.QuadPart - fb.QuadPart) / 1073741824.0;
                                double totalGB = (double)tb.QuadPart / 1073741824.0;
                                swprintf_s(capStr, 32, L"%.1fG/%.0fG", usedGB, totalGB);
                            } else {
                                capStr[0] = L'\0';
                            }
                            nmlvdi->item.pszText = capStr;
                        } else {
                            nmlvdi->item.pszText = L"";
                        }
                        break;
                    case COLUMN_DATE_IDX:
                        nmlvdi->item.pszText = item->node->type == TYPE_FILE ? item->formattedDate : L"";
                        break;
                    case COLUMN_PATH_IDX: {
                        if (!item->path) {
                            wchar_t path[MAX_PATH] = {0};
                            getFileNodePath(item->node, path);
                            item->path = wcsdup(path);
                        }
                        nmlvdi->item.pszText = item->path;
                        break;
                    }
                }
            }
            break;
        }
        case NM_RCLICK: {
            NMITEMACTIVATE* nmia = (NMITEMACTIVATE*)nmhdr;
            cvSetActiveByHwnd(nmhdr->hwndFrom);

            if (nmia->iItem != -1 && nmia->iSubItem == 0) {
                updateSelectedItems();

                bool show = true;
                for (int i = 0; i < numSelectedItems; i++) {
                    if (!(selectedItems[i]->type == TYPE_FILE || selectedItems[i]->type == TYPE_DIR)) {
                        show = false;
                        break;
                    }
                }
                if (show) createContextMenu(numSelectedItems == 1 ? MENU_SINGLE : MENU_MULTIPLE);
            }
            else createContextMenu(MENU_EMPTY);
            break;
        }
        case NM_CLICK: {
            cvSetActiveByHwnd(nmhdr->hwndFrom);
            break;
        }
        case NM_DBLCLK: {
            NMITEMACTIVATE* nmia = (NMITEMACTIVATE*)nmhdr;
            cvSetActiveByHwnd(nmhdr->hwndFrom);
            if (nmia->iItem == -1 || nmia->iSubItem != 0) break;

            struct ListItem* item = &p->items[nmia->iItem];
            openFileNode(item->node);
            break;
        }
        case LVN_COLUMNCLICK: {
            LPNMLISTVIEW plvInfo = (LPNMLISTVIEW)nmhdr;
            cvSetActiveByHwnd(nmhdr->hwndFrom);

            if (plvInfo->iSubItem == p->sortColumnIdx) {
                p->sortAscending = !p->sortAscending;
            }
            else {
                p->sortColumnIdx = plvInfo->iSubItem;
                p->sortAscending = true;
            }

            refreshPane(p);
            break;
        }
        case LVN_KEYDOWN: {
            NMLVKEYDOWN* kd = (NMLVKEYDOWN*)nmhdr;
            cvSetActiveByHwnd(nmhdr->hwndFrom);
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            switch (kd->wVKey) {
                case VK_F2: updateSelectedItems(); onMenuItemRenameClick(); break;
                case VK_DELETE: onMenuItemDeleteClick(); break;
                case VK_F5: navigateRefresh(); break;
                case VK_F6: cvToggleSplit(); break;
                case VK_BACK: navigateUp(); break;
                case VK_RETURN:
                    updateSelectedItems();
                    if (GetKeyState(VK_MENU) < 0) onMenuItemPropertiesClick();
                    else if (numSelectedItems == 1) openFileNode(selectedItems[0]);
                    break;
                case 'C': if (ctrl) onMenuItemCopyClick(); break;
                case 'X': if (ctrl) onMenuItemCutClick(); break;
                case 'V': if (ctrl) onMenuItemPasteClick(); break;
                case 'A': if (ctrl) onMenuItemSelectAllClick(); break;
                case 'T': if (ctrl) tabNew(); break;
                case 'W': if (ctrl) tabCloseActive(); break;
            }
            break;
        }
    }

    return 0;
}

static DWORD WINAPI searchTask(void* param) {
    struct SearchData* searchData = (struct SearchData*)param;
    struct Pane* p = searchData->pane;

    const int maxStackSize = 50;
    struct FileNode* stack[maxStackSize];
    int stackSize = 0;
    stack[stackSize++] = p->currPath->children;

    wchar_t keyword[64] = {0};
    wchar_t name[64] = {0};

    strToLower(searchData->keyword, keyword);

    while (stackSize > 0 && p->numItems < 10000 && searchData->active) {
        struct FileNode* node = stack[--stackSize];
        while (node && searchData->active) {
            strToLower(node->name, name);
            if (wcsstr(name, keyword)) {
                SendMessage(p->hwndList, MSG_ADD_ITEM, 0, (LPARAM)node);
            }

            if (p->numItems >= 10000) break;

            if (node->type == TYPE_DIR && stackSize < maxStackSize) {
                buildChildNodes(node, false);
                if (node->children) stack[stackSize++] = node->children;
            }
            node = node->sibling;
        }
    }

    SendMessage(p->hwndList, MSG_SEARCH_DONE, 0, 0);
    return 0;
}

void searchFor(wchar_t* keyword) {
    if (wcslen(keyword) == 0) return;
    struct Pane* p = activePane();
    if (p->searchData != NULL && p->searchData->active) {
        p->searchData->active = false;
        return;
    }

    clearPane(p);

    LVCOLUMN column = {0};
    column.mask = LVCF_WIDTH | LVCF_TEXT;
    column.cx = 250;
    column.pszText = lc_str.path;
    ListView_InsertColumn(p->hwndList, COLUMN_PATH_IDX, &column);
    UpdateWindow(p->hwndList);

    p->searchData = malloc(sizeof(struct SearchData));
    p->searchData->keyword = keyword;
    p->searchData->active = true;
    p->searchData->canceled = false;
    p->searchData->pane = p;

    CreateThread(NULL, 0, searchTask, p->searchData, 0, NULL);
}

void setViewStyle(enum ViewStyle newViewStyle) {
    struct Pane* p = activePane();
    LONG_PTR wndstyle = GetWindowLongPtr(p->hwndList, GWL_STYLE);
    wndstyle &= ~LVS_TYPEMASK;

    switch (newViewStyle) {
        case STYLE_LARGE_ICON:
            wndstyle |= LVS_ICON;
            break;
        case STYLE_SMALL_ICON:
            wndstyle |= LVS_SMALLICON;
            break;
        case STYLE_LIST:
            wndstyle |= LVS_LIST;
            break;
        case STYLE_DETAILS:
            wndstyle |= LVS_REPORT;
            break;
    }

    SetWindowLongPtr(p->hwndList, GWL_STYLE, wndstyle);

    p->viewStyle = newViewStyle;
    // Persist view style to registry (survives restart)
    HKEY hkey;
    if (RegCreateKeyW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", &hkey) == ERROR_SUCCESS) {
        DWORD val = (DWORD)newViewStyle;
        RegSetValueExW(hkey, L"ViewStyle", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
        RegCloseKey(hkey);
    }
    refreshPane(p);
}

static void createLVColumns(HWND hwndList) {
    LVCOLUMN column = {0};
    column.mask = LVCF_WIDTH | LVCF_TEXT;

    // Column widths tuned so the size column has room for a full capacity bar
    // plus "used/total GB" text (no truncation). Name column is flexible.
    column.cx = 170;
    column.pszText = lc_str.name;
    ListView_InsertColumn(hwndList, COLUMN_NAME_IDX, &column);

    column.cx = 80;
    column.pszText = lc_str.type;
    ListView_InsertColumn(hwndList, COLUMN_TYPE_IDX, &column);

    column.cx = 150;
    column.pszText = lc_str.size;
    ListView_InsertColumn(hwndList, COLUMN_SIZE_IDX, &column);

    column.cx = 100;
    column.pszText = lc_str.date;
    ListView_InsertColumn(hwndList, COLUMN_DATE_IDX, &column);
}

// The list-view column header (SysHeader32) renders with a light band under the dark
// container theme, making the titles unreadable. Owner-draw it dark instead.
static WNDPROC OrigHeaderProc = NULL;

static LRESULT CALLBACK HeaderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(themeFaceBg());
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        HGDIOBJ oldFont = SelectObject(hdc, getUIFont());
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, themeFaceText());
        HPEN pen = CreatePen(PS_SOLID, 1, themeFaceLine());
        HGDIOBJ oldPen = SelectObject(hdc, pen);

        int count = (int)SendMessage(hwnd, HDM_GETITEMCOUNT, 0, 0);
        for (int i = 0; i < count; i++) {
            RECT ir;
            if (!(BOOL)SendMessage(hwnd, HDM_GETITEMRECT, i, (LPARAM)&ir)) continue;

            wchar_t text[64] = {0};
            HDITEM hdi = {0};
            hdi.mask = HDI_TEXT;
            hdi.pszText = text;
            hdi.cchTextMax = 64;
            SendMessage(hwnd, HDM_GETITEM, i, (LPARAM)&hdi);

            RECT tr = ir;
            tr.left += 8;
            tr.right -= 4;
            DrawText(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            MoveToEx(hdc, ir.right - 1, ir.top + 3, NULL);
            LineTo(hdc, ir.right - 1, ir.bottom - 3);
        }

        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return CallWindowProc(OrigHeaderProc, hwnd, msg, wParam, lParam);
}

// Fit columns to the pane so the four columns never overflow (no horizontal scrollbar):
// the Name column absorbs the leftover width.
void cvFitColumns(HWND list, int totalWidth) {
    int typeW = 75, sizeW = 170, dateW = 120;
    int other = typeW + sizeW + dateW;
    int nameW = totalWidth - other - GetSystemMetrics(SM_CXVSCROLL) - 6;
    if (nameW < 90) nameW = 90;
    SendMessage(list, LVM_SETCOLUMNWIDTH, COLUMN_NAME_IDX, (LPARAM)nameW);
    SendMessage(list, LVM_SETCOLUMNWIDTH, COLUMN_TYPE_IDX, (LPARAM)typeW);
    SendMessage(list, LVM_SETCOLUMNWIDTH, COLUMN_SIZE_IDX, (LPARAM)sizeW);
    SendMessage(list, LVM_SETCOLUMNWIDTH, COLUMN_DATE_IDX, (LPARAM)dateW);
}

static HWND createOneContentView() {
    HWND hwnd = CreateWindowEx(0, WC_LISTVIEW, NULL, WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER | LVS_OWNERDATA | LVS_REPORT | LVS_SHAREIMAGELISTS,
                               0, 0, 0, 0, hwndMain, (HMENU)NULL, globalHInstance, NULL);
    OrigWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ContentViewWndProc);
    SendMessage(hwnd, WM_SETFONT, (WPARAM)getUIFont(), TRUE);
    // Register as OLE drop target (accept files dragged from Linux file manager / other apps)
    if (!g_dropTarget) g_dropTarget = createDropTarget();
    if (g_dropTarget) RegisterDragDrop(hwnd, g_dropTarget);
    // Modern list behaviour: full-row selection, flicker-free scrolling, tidy label tips.
    // NOTE: do NOT SetWindowTheme("Explorer") here — under the dark container theme it forces
    // a light header band that renders the column titles unreadable.
    ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    createLVColumns(hwnd);

    // Owner-draw the column header dark (see HeaderWndProc). Both list views share the
    // same header class, so capture the original proc once.
    HWND hdr = (HWND)SendMessage(hwnd, LVM_GETHEADER, 0, 0);
    if (hdr) {
        if (!OrigHeaderProc) OrigHeaderProc = (WNDPROC)GetWindowLongPtr(hdr, GWLP_WNDPROC);
        SetWindowLongPtr(hdr, GWLP_WNDPROC, (LONG_PTR)HeaderWndProc);
    }

    UpdateWindow(hwnd);
    return hwnd;
}

void createContentView() {
    cmiOpen.text = lc_str.open;
    cmiEdit.text = lc_str.edit;
    cmiCut.text = lc_str.cut;
    cmiCopy.text = lc_str.copy;
    cmiCreateShortcut.text = lc_str.create_shortcut;
    cmiDelete.text = lc_str.delete;
    cmiRename.text = lc_str.rename;
    cmiPaste.text = lc_str.paste;
    cmiPasteShortcut.text = lc_str.paste_shortcut;
    cmiNewFolder.text = lc_str.new_folder;
    cmiNewFile.text = lc_str.new_file;
    cmiUnloadISOImage.text = lc_str.unload_iso_image;
    cmiOpenAsAdmin.text = lc_str.open_as_admin;
    cmiChooseProgram.text = lc_str.choose_program;
    cmiProperties.text = lc_str.properties;
    cmiCopyPath.text = lc_str.copy_path;
    cmiOpenCmd.text = lc_str.open_cmd;
    cmiExtractHere.text = lc_str.extract_here;
    cmiExtractToFolder.text = lc_str.extract_to_folder;
    cmiNewTxt.text = lc_str.new_txt;
    cmiNewBat.text = lc_str.new_bat;
    cmiNewReg.text = lc_str.new_reg;
    cmiExtractIcon.text = lc_str.extract_icon;
    cmiMD5.text = lc_str.calc_md5;
    cmiViewText.text = lc_str.view_text;
    cmiFolderSize.text = lc_str.folder_size;
    cmiCopyTo.text = lc_str.copy_to;
    cmiMoveTo.text = lc_str.move_to;
    cmiAddToFav.text = lc_str.add_to_favorites;
    cmiBatchRename.text = lc_str.batch_rename;
    cmiLauncherBoost.text = lc_str.launcher_boost;
    cmiLauncherBoostAggressive.text = lc_str.launcher_boost_aggressive;
    cmiRunDX11.text = lc_str.arg_dx11;
    cmiRunD3D9.text = lc_str.arg_d3d9;
    cmiRunNoDebug.text = lc_str.arg_nodebug;
    cmiRunWindowed.text = lc_str.arg_windowed;
    cmiRunCustom.text = lc_str.arg_custom;
    cmiHashSHA1.text = lc_str.hash_sha1;
    cmiHashSHA256.text = lc_str.hash_sha256;
    cmiLauncherRunWith.text = lc_str.launcher_run_with;
    cmiLauncherChoose.text = lc_str.launcher_choose;
    cmiDiff.text = lc_str.diff_files;

    // Restore saved view style from registry
    DWORD savedView = STYLE_DETAILS;
    HKEY hkeyView;
    if (RegOpenKeyW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", &hkeyView) == ERROR_SUCCESS) {
        DWORD data = 0, sz = sizeof(data);
        if (RegQueryValueExW(hkeyView, L"ViewStyle", NULL, NULL, (BYTE*)&data, &sz) == ERROR_SUCCESS)
            savedView = data;
        RegCloseKey(hkeyView);
    }

    for (int i = 0; i < NUM_PANES; i++) {
        panes[i].hwndList = createOneContentView();
        panes[i].viewStyle = (enum ViewStyle)savedView;
        panes[i].hwndPathLabel = CreateWindowEx(0, WC_STATIC, L"", WS_CHILD | WS_CLIPSIBLINGS | SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS | SS_CENTERIMAGE | SS_NOTIFY,
                                                0, 0, 0, 0, hwndMain, (HMENU)NULL, globalHInstance, NULL);
        SendMessage(panes[i].hwndPathLabel, WM_SETFONT, (WPARAM)getUIFont(), TRUE);
        panes[i].currPath = NULL;
        panes[i].items = NULL;
        panes[i].numItems = 0;
        panes[i].viewStyle = STYLE_DETAILS;
        panes[i].sortColumnIdx = COLUMN_NAME_IDX;
        panes[i].sortAscending = true;
        panes[i].searchData = NULL;
    }

    // Pane 1 + both path bars start hidden until split view is enabled.
    ShowWindow(panes[1].hwndList, SW_HIDE);
    ShowWindow(panes[0].hwndPathLabel, SW_HIDE);
    ShowWindow(panes[1].hwndPathLabel, SW_HIDE);
    activeIdx = 0;
}

// Give each pane its own independent path chain. Called after initFileNodes()
// (which leaves the global currPathFileNode pointing at Computer).
void cvInitPanePaths() {
    panes[0].currPath = currPathFileNode;                 // adopt the initial chain
    panes[1].currPath = copyPathChain(currPathFileNode);  // independent copy
    activeIdx = 0;
    currPathFileNode = panes[0].currPath;
}

static void cvSetActiveByHwnd(HWND h) {
    int idx = activeIdx;
    for (int i = 0; i < NUM_PANES; i++) {
        if (panes[i].hwndList == h) { idx = i; break; }
    }
    if (idx == activeIdx) return;
    if (!splitOn) return;

    // Swap the active pane's live path out to its slot, bring the new one in.
    panes[activeIdx].currPath = currPathFileNode;
    activeIdx = idx;
    currPathFileNode = panes[activeIdx].currPath;

    SetWindowText(hwndMain, currPathFileNode->name);
    updateAddrButtons();
    updateStatusbar(activePane());
    cvInvalidatePaneFrames();
    for (int i = 0; i < NUM_PANES; i++) InvalidateRect(panes[i].hwndPathLabel, NULL, TRUE);
}

void cvToggleSplit() {
    splitOn = !splitOn;

    if (splitOn) {
        ShowWindow(panes[1].hwndList, SW_SHOW);
        ShowWindow(panes[0].hwndPathLabel, SW_SHOW);
        ShowWindow(panes[1].hwndPathLabel, SW_SHOW);
        // Populate pane 1 (it was never refreshed while hidden).
        buildChildNodes(panes[1].currPath, false);
        refreshPane(&panes[1]);
        updatePaneLabel(&panes[0]);
    }
    else {
        // Collapsing: make pane 0 active and hide pane 1.
        if (activeIdx == 1) {
            panes[1].currPath = currPathFileNode;
            activeIdx = 0;
            currPathFileNode = panes[0].currPath;
            SetWindowText(hwndMain, currPathFileNode->name);
            updateAddrButtons();
            updateStatusbar(activePane());
        }
        ShowWindow(panes[1].hwndList, SW_HIDE);
        ShowWindow(panes[0].hwndPathLabel, SW_HIDE);
        ShowWindow(panes[1].hwndPathLabel, SW_HIDE);
    }

    resizeControls();
    cvInvalidatePaneFrames();
    SetFocus(panes[activeIdx].hwndList);
}

// Dual-pane sync: when the active pane navigates into a subdirectory,
// mirror the navigation in the other pane if a same-named subdirectory exists there.
void cvSyncOtherPane(const wchar_t* targetName) {
    if (!splitOn || !targetName || !targetName[0]) return;
    struct Pane* other = (activeIdx == 0) ? &panes[1] : &panes[0];
    if (!other->currPath) return;
    // Search other pane's children for a matching directory name.
    for (int i = 0; i < other->numItems; i++) {
        if (other->items[i].node->type == TYPE_DIR &&
            wcscmp(other->items[i].node->name, targetName) == 0) {
            // Navigate other pane into the matching subdirectory.
            int savedActive = activeIdx;
            activeIdx = (activeIdx == 0) ? 1 : 0;
            currPathFileNode = other->items[i].node;
            other->currPath = other->items[i].node;
            buildChildNodes(other->currPath, false);
            refreshPane(other);
            updatePaneLabel(other);
            activeIdx = savedActive;
            currPathFileNode = panes[activeIdx].currPath;
            break;
        }
    }
}

void onMenuItemUpClick() {
    navigateUp();
}

void onMenuItemOpenClick() {
    if (numSelectedItems == 1) openFileNode(selectedItems[0]);
}

static struct {
    wchar_t name[MAX_PATH];
    wchar_t type[80];
    wchar_t location[MAX_PATH];
    wchar_t size[32];
    wchar_t modified[32];
    wchar_t path[MAX_PATH];
} propInfo;

static INT_PTR CALLBACK PropertiesDialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (msg) {
        case WM_INITDIALOG: {
            RECT rect, rect1;
            GetWindowRect(GetParent(hwndDlg), &rect);
            GetClientRect(hwndDlg, &rect1);
            SetWindowPos(hwndDlg, NULL, (rect.right + rect.left) / 2 - (rect1.right - rect1.left) / 2,
                         (rect.bottom + rect.top) / 2 - (rect1.bottom - rect1.top) / 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
            SetWindowText(hwndDlg, lc_str.properties);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_NAME), propInfo.name);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_TYPE), propInfo.type);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_LOCATION), propInfo.location);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_SIZE), propInfo.size);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_MODIFIED), propInfo.modified);
            DWORD attr = GetFileAttributesW(propInfo.path);
            if (attr != INVALID_FILE_ATTRIBUTES) {
                CheckDlgButton(hwndDlg, IDC_ATTR_READONLY, (attr & FILE_ATTRIBUTE_READONLY) ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hwndDlg, IDC_ATTR_HIDDEN, (attr & FILE_ATTRIBUTE_HIDDEN) ? BST_CHECKED : BST_UNCHECKED);
            }
            // Localize all labels at runtime (RC file stays English to avoid encoding issues)
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_LNAME), lc_str.prop_name);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_LTYPE), lc_str.prop_type);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_LLOCATION), lc_str.prop_location);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_LSIZE), lc_str.prop_size);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_LMODIFIED), lc_str.prop_modified);
            SetWindowText(GetDlgItem(hwndDlg, IDC_PROP_LATTRIBUTES), lc_str.prop_attributes);
            SetWindowText(GetDlgItem(hwndDlg, IDC_ATTR_READONLY), lc_str.prop_readonly);
            SetWindowText(GetDlgItem(hwndDlg, IDC_ATTR_HIDDEN), lc_str.prop_hidden);
            SetWindowText(GetDlgItem(hwndDlg, IDOK), lc_str.ok);
            SetWindowText(GetDlgItem(hwndDlg, IDCANCEL), lc_str.cancel);
            return (INT_PTR)TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                DWORD attr = GetFileAttributesW(propInfo.path);
                if (attr != INVALID_FILE_ATTRIBUTES) {
                    if (IsDlgButtonChecked(hwndDlg, IDC_ATTR_READONLY)) attr |= FILE_ATTRIBUTE_READONLY;
                    else attr &= ~FILE_ATTRIBUTE_READONLY;
                    if (IsDlgButtonChecked(hwndDlg, IDC_ATTR_HIDDEN)) attr |= FILE_ATTRIBUTE_HIDDEN;
                    else attr &= ~FILE_ATTRIBUTE_HIDDEN;
                    if (attr == 0) attr = FILE_ATTRIBUTE_NORMAL;
                    SetFileAttributesW(propInfo.path, attr);
                }
                EndDialog(hwndDlg, (INT_PTR)IDOK);
                return (INT_PTR)TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwndDlg, (INT_PTR)IDCANCEL);
                return (INT_PTR)TRUE;
            }
            break;
    }
    return (INT_PTR)FALSE;
}

void onMenuItemPropertiesClick() {
    if (numSelectedItems != 1) return;
    struct FileNode* node = selectedItems[0];

    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(node, path);
    wcscpy_s(propInfo.path, MAX_PATH, path);

    wcscpy_s(propInfo.name, MAX_PATH, node->name);

    struct FileInfo fi = {0};
    getFileInfo(path, node->type, false, &fi);
    wcscpy_s(propInfo.type, 80, fi.typeName);

    if (node->parent) getFileNodePath(node->parent, propInfo.location);
    else propInfo.location[0] = L'\0';

    propInfo.size[0] = L'\0';
    propInfo.modified[0] = L'\0';

    if (node->type == TYPE_FILE) {
        WIN32_FILE_ATTRIBUTE_DATA info = {0};
        if (GetFileAttributesEx(path, GetFileExInfoStandard, &info)) {
            LARGE_INTEGER sz;
            sz.LowPart = info.nFileSizeLow;
            sz.HighPart = info.nFileSizeHigh;
            formatFileSize(sz.QuadPart, propInfo.size);

            SYSTEMTIME st = {0};
            FILETIME lt;
            if (FileTimeToLocalFileTime(&info.ftLastWriteTime, &lt) && FileTimeToSystemTime(&lt, &st)) {
                formatModifiedDate(st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, propInfo.modified, 32);
            }
        }
    }
    else wcscpy_s(propInfo.size, 32, L"-");

    DialogBox(globalHInstance, MAKEINTRESOURCE(IDD_PROPERTIES), hwndMain, &PropertiesDialogProc);
}

void onMenuItemOpenAsAdminClick() {
    if (numSelectedItems == 1 && selectedItems[0]->type == TYPE_FILE) {
        wchar_t path[MAX_PATH] = {0};
        wchar_t parentPath[MAX_PATH] = {0};
        getFileNodePath(selectedItems[0], path);
        getFileNodePath(selectedItems[0]->parent, parentPath);
        // "runas" verb requests elevation. Wine's elevation is largely cosmetic, but
        // apps that gate on the verb / the elevated flag get what they expect.
        ShellExecuteW(hwndMain, L"runas", path, NULL, parentPath, SW_SHOW);
    }
}

void onMenuItemOpenWithClick() {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t filePath[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], filePath);
    // Browse for an .exe to open the file with (rundll32 OpenAs_RunDLL does not work under Wine)
    OPENFILENAMEW ofn = {0};
    wchar_t exePath[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hwndMain;
    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = exePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = lc_str.choose_program;
    // Dynamically load comdlg32 to avoid adding -lcomdlg32 to the link line
    typedef BOOL (WINAPI *PFN_GetOpenFileNameW)(LPOPENFILENAMEW);
    HMODULE hCd = LoadLibraryW(L"comdlg32.dll");
    if (hCd) {
        PFN_GetOpenFileNameW pfn = (PFN_GetOpenFileNameW)GetProcAddress(hCd, "GetOpenFileNameW");
        if (pfn && pfn(&ofn)) {
            wchar_t params[MAX_PATH + 8] = {0};
            swprintf_s(params, MAX_PATH + 8, L"\"%ls\"", filePath);
            ShellExecuteW(hwndMain, L"open", exePath, params, NULL, SW_SHOW);
        }
        FreeLibrary(hCd);
    }
}

// ===================== Launcher (RamBooster-style) =====================
// Saved external launcher exe (optional). When unset, the target itself is launched after
// the memory boost. Stored under HKCU\SOFTWARE\Winlator\WFM\LauncherPath (REG_SZ).
static bool launcherGetSaved(wchar_t* out) {
    out[0] = L'\0';
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        DWORD cb = MAX_PATH * sizeof(wchar_t);
        RegQueryValueExW(hk, L"LauncherPath", NULL, NULL, (LPBYTE)out, &cb);
        RegCloseKey(hk);
    }
    return (out[0] != L'\0' && isPathExists(out));
}

// Apply memory pressure so Android's LMK reclaims background processes before
// the game starts. Parameters follow Noysz/RamBooster-Winlator v1.2.2 (the tuned
// Winlator fork), NOT the 5.5GB upstream single-file version: 8MB chunks that
// halve on failure, touch every real page, a dynamic safety floor (keep 12% of
// physical RAM free so the container never kills itself), and a hard absolute
// cap (1GB balanced / 1.5GB aggressive) — 45% of an 8GB phone = 3.6GB would
// stall ~13s, which the fork explicitly fixed by capping.
// mode 0 = balanced (35%, cap 1GB); mode 1 = aggressive (45%, cap 1.5GB + trim).
static void launcherBoostMemory(int mode) {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms) || ms.ullTotalPhys == 0) return;

    int targetPct = (mode == 1) ? 45 : 35;
    SIZE_T absCap = (mode == 1) ? (SIZE_T)1536 * 1024 * 1024
                                : (SIZE_T)1024 * 1024 * 1024;
    SIZE_T totalToAlloc = (SIZE_T)(ms.ullTotalPhys * targetPct / 100);
    if (totalToAlloc > absCap) totalToAlloc = absCap;

    // Keep at least 12% of physical RAM free (Box64 floor from the fork).
    SIZE_T safetyFloor = (SIZE_T)(ms.ullTotalPhys * 12 / 100);

    SYSTEM_INFO si; GetSystemInfo(&si);
    SIZE_T pageStep = si.dwPageSize ? si.dwPageSize : 4096;

    SIZE_T bSize = 8 * 1024 * 1024;  // 8MB per chunk, halved on alloc failure
    SIZE_T maxBlocks = totalToAlloc / (1024 * 1024) + 8;
    void** blocks = (void**)malloc(sizeof(void*) * maxBlocks);
    if (!blocks) return;

    int count = 0;
    SIZE_T allocated = 0;
    while (allocated < totalToAlloc && count < (int)maxBlocks) {
        // Check the safety floor every 4 blocks (GlobalMemoryStatusEx is costly
        // under Wine's Wine→Android translation).
        if (count % 4 == 0) {
            MEMORYSTATUSEX chk; chk.dwLength = sizeof(chk);
            if (GlobalMemoryStatusEx(&chk) && chk.ullAvailPhys < safetyFloor)
                break;
        }
        void* m = VirtualAlloc(NULL, bSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!m) {
            bSize /= 2;
            if (bSize < 1024 * 1024) break;
            continue;
        }
        // Touch every real page so physical memory is actually committed.
        for (SIZE_T off = 0; off < bSize; off += pageStep)
            ((volatile char*)m)[off] = 1;
        blocks[count++] = m;
        allocated += bSize;
        Sleep(25);  // Box64 chunk interval from the fork
    }

    if (count > 0) Sleep(500);  // hold briefly so LMK can select & kill victims
    for (int i = 0; i < count; i++) VirtualFree(blocks[i], 0, MEM_RELEASE);
    free(blocks);

    // Aggressive mode also trims WFM's own working set to the minimum.
    if (mode == 1)
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

struct LauncherArg {
    wchar_t target[MAX_PATH];
    wchar_t launcher[MAX_PATH];
    bool useExternal;   // true = run via external launcher; false = always run target
    int boostMode;      // 0 = balanced, 1 = aggressive, -1 = no boost
    wchar_t extraArgs[256];  // command-line args appended to the game (Unity/UE flags)
};

static DWORD WINAPI launcherThread(LPVOID param) {
    struct LauncherArg* a = (struct LauncherArg*)param;
    if (a->boostMode >= 0) {
        PostMessage(hwndMain, WM_USER_BOOST_START, 0, 0);
        launcherBoostMemory(a->boostMode);
        PostMessage(hwndMain, WM_USER_BOOST_DONE, 0, 0);
    }

    // Working directory MUST be the target's folder (games load sibling files
    // relative to their own exe), never the launcher's folder.
    wchar_t targetDir[MAX_PATH] = {0};
    getParentDirFromPath(a->target, targetDir);

    bool useExternal = a->useExternal && a->launcher[0] && isPathExists(a->launcher);
    wchar_t* app = useExternal ? a->launcher : a->target;

    // Command line: exe path + optional extra args (Unity -force-d3d11 etc.).
    // This mirrors Winlator shortcut "Exec Arguments", which are passed to the
    // game itself, not set as Wine environment variables.
    wchar_t cmdLine[MAX_PATH * 2 + 256] = {0};
    if (useExternal)
        swprintf_s(cmdLine, _countof(cmdLine), L"\"%ls\" \"%ls\" %ls",
                   a->launcher, a->target, a->extraArgs);
    else
        swprintf_s(cmdLine, _countof(cmdLine), L"\"%ls\" %ls",
                   a->target, a->extraArgs);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessW(app, cmdLine, NULL, NULL, FALSE, 0, NULL,
                             targetDir[0] ? targetDir : NULL, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else {
        // Fallback for non-PE targets / association-based open.
        ShellExecuteW(hwndMain, L"open", a->target, NULL,
                      targetDir[0] ? targetDir : NULL, SW_SHOW);
    }
    free(a);
    return 0;
}

// Context menu entry: free RAM (balanced mode) then run the selected file directly.
// This NEVER uses the external launcher — that is a separate explicit action.
void onMenuItemLauncherBoostClick(void) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    struct LauncherArg* a = (struct LauncherArg*)calloc(1, sizeof(struct LauncherArg));
    if (!a) return;
    getFileNodePath(selectedItems[0], a->target);
    a->useExternal = false;
    a->boostMode = 0;
    HANDLE h = CreateThread(NULL, 0, launcherThread, a, 0, NULL);
    if (h) CloseHandle(h); else free(a);
}

// Aggressive boost: harder memory pressure + working-set trim, for games that
// sit near the device RAM limit and risk OOM kills during combat/transitions.
void onMenuItemLauncherBoostAggressiveClick(void) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    struct LauncherArg* a = (struct LauncherArg*)calloc(1, sizeof(struct LauncherArg));
    if (!a) return;
    getFileNodePath(selectedItems[0], a->target);
    a->useExternal = false;
    a->boostMode = 1;
    HANDLE h = CreateThread(NULL, 0, launcherThread, a, 0, NULL);
    if (h) CloseHandle(h); else free(a);
}

// Helper: launch with extra command-line arguments (no memory boost).
// These mirror Winlator shortcut "Exec Arguments": passed directly to the game.
static void launchWithArgs(const wchar_t* args) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    struct LauncherArg* a = (struct LauncherArg*)calloc(1, sizeof(struct LauncherArg));
    if (!a) return;
    getFileNodePath(selectedItems[0], a->target);
    a->useExternal = false;
    a->boostMode = -1;  // -1 = no memory boost
    wcscpy_s(a->extraArgs, 256, args);
    HANDLE h = CreateThread(NULL, 0, launcherThread, a, 0, NULL);
    if (h) CloseHandle(h); else free(a);
}

void onMenuItemRunDX11Click(void) { launchWithArgs(L"-force-d3d11 -force-d3d11-singlethread"); }
void onMenuItemRunD3D9Click(void) { launchWithArgs(L"-force-d3d9"); }
void onMenuItemRunNoDebugClick(void) { launchWithArgs(L"-force-opengl"); }
// Generic: windowed mode (works across Unity, Unreal, and many native games).
void onMenuItemRunWindowedClick(void) { launchWithArgs(L"-windowed"); }
// Custom: prompt the user for any command-line args (e.g. -screen-width 1920
// -screen-height 1080 for custom resolution, or any engine-specific flags).
void onMenuItemRunCustomClick(void) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t* input = InputDialog(lc_str.arg_custom,
        L"输入命令行参数（如 -screen-width 1920 -screen-height 1080）", L"", false);
    if (input && input[0]) {
        launchWithArgs(input);
        free(input);
    }
}

// ============================================================================
// File hash: compute SHA1/SHA256 via CryptoAPI, copy to clipboard.
// (MD5 uses the existing computeFileMD5 helper.)
// ============================================================================
static void copyFileHash(const wchar_t* path, ALG_ID algId, const wchar_t* algName) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hwndMain, L"无法打开文件", algName, MB_OK | MB_ICONERROR);
        return;
    }
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile); return;
    }
    if (!CryptCreateHash(hProv, algId, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0); CloseHandle(hFile); return;
    }
    BYTE buf[65536];
    DWORD read;
    while (ReadFile(hFile, buf, sizeof(buf), &read, NULL) && read > 0)
        CryptHashData(hHash, buf, read, 0);
    CloseHandle(hFile);

    DWORD hashLen = 0, hashSize = sizeof(DWORD);
    CryptGetHashParam(hHash, HP_HASHSIZE, (BYTE*)&hashLen, &hashSize, 0);
    BYTE* hashBytes = (BYTE*)malloc(hashLen);
    CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    wchar_t hex[128] = {0};
    for (DWORD i = 0; i < hashLen; i++)
        swprintf_s(hex + i * 2, 3, L"%02x", hashBytes[i]);
    free(hashBytes);

    // Copy to clipboard.
    if (OpenClipboard(hwndMain)) {
        EmptyClipboard();
        size_t bytesLen = (wcslen(hex) + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytesLen);
        if (hMem) {
            wchar_t* p = (wchar_t*)GlobalLock(hMem);
            wcscpy_s(p, wcslen(hex) + 1, hex);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
    wchar_t msg[300];
    swprintf_s(msg, 300, L"%ls 已复制到剪贴板：\n\n%ls", algName, hex);
    MessageBoxW(hwndMain, msg, lc_str.copy_hash, MB_OK | MB_ICONINFORMATION);
}

void onMenuItemHashSHA1Click(void) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t path[MAX_PATH] = {0}; getFileNodePath(selectedItems[0], path);
    copyFileHash(path, CALG_SHA1, L"SHA1");
}
void onMenuItemHashSHA256Click(void) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t path[MAX_PATH] = {0}; getFileNodePath(selectedItems[0], path);
    copyFileHash(path, CALG_SHA_256, L"SHA256");
}

// ============================================================================
// Process manager: list running processes via Toolhelp32, allow termination.
// Simple modal dialog with a listbox and a "Kill" button.
// ============================================================================
static HWND hProcDlg = NULL;
static HWND hProcList = NULL;

static void refreshProcessList(void) {
    SendMessageW(hProcList, LB_RESETCONTENT, 0, 0);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(snap, &pe)) {
        do {
            wchar_t item[260];
            swprintf_s(item, 260, L"%ls  (PID: %lu)", pe.szExeFile, pe.th32ProcessID);
            int idx = (int)SendMessageW(hProcList, LB_ADDSTRING, 0, (LPARAM)item);
            SendMessageW(hProcList, LB_SETITEMDATA, idx, pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void onMenuItemProcessManagerClick(void) {
    // Build a simple dialog template in memory: listbox + 3 buttons.
    // Use a lightweight approach: create a popup window manually.
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", lc_str.process_manager,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 380, hwndMain, NULL, globalHInstance, NULL);
    if (!hwnd) return;
    // Listbox
    CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        10, 10, 390, 290, hwnd, (HMENU)1001, globalHInstance, NULL);
    // Buttons
    CreateWindowExW(0, L"BUTTON", L"结束进程", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 310, 100, 30, hwnd, (HMENU)1002, globalHInstance, NULL);
    CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 310, 80, 30, hwnd, (HMENU)1003, globalHInstance, NULL);
    CreateWindowExW(0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        300, 310, 100, 30, hwnd, (HMENU)IDOK, globalHInstance, NULL);

    hProcDlg = hwnd;
    hProcList = GetDlgItem(hwnd, 1001);
    refreshProcessList();

    // Modal message loop.
    ShowWindow(hwnd, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hwnd)) break;
    }
}

// Context menu entry: free RAM then launch the target through the saved external
// launcher (which receives the target path as its first argument). Only shown
// when a launcher is configured.
void onMenuItemLauncherRunWithClick(void) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    struct LauncherArg* a = (struct LauncherArg*)calloc(1, sizeof(struct LauncherArg));
    if (!a) return;
    getFileNodePath(selectedItems[0], a->target);
    launcherGetSaved(a->launcher);
    a->useExternal = true;
    HANDLE h = CreateThread(NULL, 0, launcherThread, a, 0, NULL);
    if (h) CloseHandle(h); else free(a);
}

// Context menu entry: pick an external launcher exe (e.g. RamBooster) and remember it.
void onMenuItemLauncherChooseClick(void) {
    // If a launcher is already configured, let the user replace it or clear it.
    // A stale launcher pointing at another exe is what made "Boost & Run" open
    // the wrong program, so clearing must be possible without editing registry.
    wchar_t saved[MAX_PATH] = {0};
    if (launcherGetSaved(saved)) {
        wchar_t msg[MAX_PATH + 128];
        swprintf_s(msg, _countof(msg), L"%ls\n\n%ls",
                   lc_str.launcher_exists, saved);
        if (MessageBoxW(hwndMain, msg, lc_str.launcher_choose,
                        MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
            HKEY hk;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", 0,
                              KEY_WRITE, &hk) == ERROR_SUCCESS) {
                RegDeleteValueW(hk, L"LauncherPath");
                RegCloseKey(hk);
            }
            return;  // cleared
        }
    }

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    wchar_t exePath[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hwndMain;
    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = exePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = lc_str.launcher_choose;

    // Dynamically load comdlg32 (mirrors onMenuItemOpenWithClick to avoid an extra link lib).
    typedef BOOL (WINAPI *PFN_GetOpenFileNameW)(LPOPENFILENAMEW);
    HMODULE hCd = LoadLibraryW(L"comdlg32.dll");
    if (!hCd) return;
    PFN_GetOpenFileNameW pfn = (PFN_GetOpenFileNameW)GetProcAddress(hCd, "GetOpenFileNameW");
    if (pfn && pfn(&ofn) && exePath[0]) {
        HKEY hk;
        DWORD disp = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM", 0, NULL, 0,
                            KEY_WRITE, NULL, &hk, &disp) == ERROR_SUCCESS) {
            RegSetValueExW(hk, L"LauncherPath", 0, REG_SZ, (const BYTE*)exePath,
                           (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
        }
    }
    FreeLibrary(hCd);
}

void onMenuItemEditClick() {
    if (numSelectedItems == 1 && selectedItems[0]->type == TYPE_FILE) {
        static const wchar_t editorPath[] = L"C:\\windows\\notepad.exe";

        wchar_t path[MAX_PATH] = {0};
        wchar_t parameters[MAX_PATH] = {0};
        getFileNodePath(selectedItems[0], path);
        swprintf_s(parameters, MAX_PATH, L"\"%ls\"", path);
        getFileNodePath(selectedItems[0]->parent, path);
        ShellExecute(hwndMain, L"open", editorPath, parameters, path, SW_SHOW);
    }
}

void onMenuItemCutClick() {
    updateSelectedItems();
    if (numSelectedItems > 0) cutFiles(selectedItems, numSelectedItems);
}

void onMenuItemCopyClick() {
    updateSelectedItems();
    if (numSelectedItems > 0) copyFiles(selectedItems, numSelectedItems);
}

void onMenuItemCreateShortcutClick() {
    updateSelectedItems();
    if (numSelectedItems > 0) createDesktopShortcuts(selectedItems, numSelectedItems);
}

void onMenuItemDeleteClick() {
    updateSelectedItems();
    if (numSelectedItems > 0) deleteFiles(selectedItems, numSelectedItems);
}

void onMenuItemRenameClick() {
    if (numSelectedItems == 1) {
        wchar_t* result = InputDialog(lc_str.rename, lc_str.enter_new_name, selectedItems[0]->name, true);
        if (result) {
            wchar_t newFilename[MAX_PATH] = {0};
            getFileNodePath(selectedItems[0]->parent, newFilename);
            wcscat_s(newFilename, MAX_PATH, L"\\");
            wcscat_s(newFilename, MAX_PATH, result);
            free(result);

            wchar_t oldFilename[MAX_PATH] = {0};
            getFileNodePath(selectedItems[0], oldFilename);
            MoveFileW(oldFilename, newFilename);
            navigateRefresh();
        }
    }
}

void onMenuItemPasteClick() {
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, path);
    if (!isPathExists(path)) return;
    pasteFiles(path);
}

void onMenuItemPasteShortcutClick() {
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, path);
    if (!isPathExists(path)) return;
    pasteShortcuts(path);
}

void onMenuItemNewFolderClick() {
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, path);
    if (!isPathExists(path)) return;

    wchar_t* result = InputDialog(lc_str.new_folder, lc_str.enter_folder_name, NULL, false);
    if (result) {
        wcscat_s(path, MAX_PATH, L"\\");
        wcscat_s(path, MAX_PATH, result);
        free(result);

        if (!isPathExists(path)) {
            CreateDirectory(path, NULL);
            navigateRefresh();
        }
    }
}

static void createFileWithExt(const wchar_t* defaultName, const wchar_t* content) {
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, path);
    if (!isPathExists(path)) return;
    wcscat_s(path, MAX_PATH, L"\\");
    wcscat_s(path, MAX_PATH, defaultName);
    if (!isPathExists(path)) {
        HANDLE handle = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            if (content && content[0]) {
                DWORD written;
                WriteFile(handle, content, (DWORD)(wcslen(content) * sizeof(wchar_t)), &written, NULL);
            }
            CloseHandle(handle);
            navigateRefresh();
        }
    }
}

void onMenuItemNewFileClick() {
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, path);
    if (!isPathExists(path)) return;

    wchar_t* result = InputDialog(lc_str.new_file, lc_str.enter_file_name, NULL, false);
    if (result) {
        wcscat_s(path, MAX_PATH, L"\\");
        wcscat_s(path, MAX_PATH, result);
        free(result);

        if (!isPathExists(path)) {
            HANDLE handle = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            navigateRefresh();
        }
    }
}

static void onMenuItemNewTxtClick() { createFileWithExt(L"New Text Document.txt", NULL); }
static void onMenuItemNewBatClick() { createFileWithExt(L"New Script.bat", L"@echo off\r\n"); }
static void onMenuItemNewRegClick() { createFileWithExt(L"New Registry Entry.reg", L"Windows Registry Editor Version 5.00\r\n\r\n"); }

void onMenuItemSelectAllClick() {
    HWND h = activePane()->hwndList;
    ListView_SetItemState(h, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(h, -1, LVIS_SELECTED, LVIS_SELECTED);
    SetFocus(h);
}

void onMenuItemLoadISOImageClick() {
    if (numSelectedItems != 1) {
        MessageBox(NULL, lc_str.msg_invalid_iso_image_file, lc_str.alert, MB_OK);
        return;
    }

    wchar_t currentISOPath[MAX_PATH] = {0};
    HKEY hkey;
    getFileNodePath(selectedItems[0], currentISOPath);

    if (!isPathExists(currentISOPath) || !(hasFileExtension(currentISOPath, L"iso") ||
                                           hasFileExtension(currentISOPath, L"bin") ||
                                           hasFileExtension(currentISOPath, L"cue") ||
                                           hasFileExtension(currentISOPath, L"nrg") ||
                                           hasFileExtension(currentISOPath, L"mdf") ||
                                           hasFileExtension(currentISOPath, L"img"))) {
        MessageBox(NULL, lc_str.msg_invalid_iso_image_file, lc_str.alert, MB_OK);
        return;
    }

    if (RegCreateKey(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM\\CurrentISOPath", &hkey) == ERROR_SUCCESS) {
        RegSetValue(hkey, NULL, REG_SZ, currentISOPath, (wcslen(currentISOPath) + 1) * sizeof(wchar_t));
        RegCloseKey(hkey);
    }

    // X: must be a real CD-ROM drive configured in winecfg (Path: ../drive_x, Type: cdrom).
    // Virtual directory mapping cannot be recognized as an optical drive by games.
    if (GetFileAttributesW(L"X:\\") == INVALID_FILE_ATTRIBUTES) {
        MessageBox(NULL, L"X: drive not found. Please add it in winecfg: Drives tab -> Add -> X: -> Path: ../drive_x -> Type: CD-ROM", lc_str.alert, MB_OK | MB_ICONWARNING);
        return;
    }
    clearDirectory(L"X:");
    extractFilesFromISOImage(currentISOPath, L"X:\\");
}

void onMenuItemUnloadISOImageClick() {
    if (GetFileAttributesW(L"X:\\") != INVALID_FILE_ATTRIBUTES) clearDirectory(L"X:");
    RegDeleteKey(HKEY_CURRENT_USER, L"SOFTWARE\\Winlator\\WFM\\CurrentISOPath");
    navigateRefresh();
}

static int compareType(const void* a, const void* b) {
    struct ListItem* ia = (struct ListItem*)a;
    struct ListItem* ib = (struct ListItem*)b;
    return g_sortPane->sortAscending ? ia->node->type - ib->node->type : ib->node->type - ia->node->type;
}

static int compareName(const void* a, const void* b) {
    struct ListItem* ia = (struct ListItem*)a;
    struct ListItem* ib = (struct ListItem*)b;
    int res = compareType(a, b);
    if (res == 0) res = g_sortPane->sortAscending ? wcscoll(ia->node->name, ib->node->name) : wcscoll(ib->node->name, ia->node->name);
    return res;
}

static int compareSize(const void* a, const void* b) {
    struct ListItem* ia = (struct ListItem*)a;
    struct ListItem* ib = (struct ListItem*)b;
    int res = compareType(a, b);
    if (res == 0) res = g_sortPane->sortAscending ? ia->size - ib->size : ib->size - ia->size;
    return res;
}

static int compareDate(const void* a, const void* b) {
    struct ListItem* ia = (struct ListItem*)a;
    struct ListItem* ib = (struct ListItem*)b;
    int res = compareType(a, b);
    if (res == 0) res = g_sortPane->sortAscending ? CompareFileTime(&ia->modifiedTime, &ib->modifiedTime) : CompareFileTime(&ib->modifiedTime, &ia->modifiedTime);
    return res;
}

static void sortItems(struct Pane* p) {
    g_sortPane = p;
    switch (p->sortColumnIdx) {
        case COLUMN_NAME_IDX:
            qsort(p->items, p->numItems, sizeof(struct ListItem), compareName);
            break;
        case COLUMN_TYPE_IDX:
            qsort(p->items, p->numItems, sizeof(struct ListItem), compareType);
            break;
        case COLUMN_SIZE_IDX:
            qsort(p->items, p->numItems, sizeof(struct ListItem), compareSize);
            break;
        case COLUMN_DATE_IDX:
            qsort(p->items, p->numItems, sizeof(struct ListItem), compareDate);
            break;
    }
}

static void refreshPane(struct Pane* p) {
    if (p->searchData != NULL && p->searchData->active) {
        p->searchData->active = false;
        p->searchData->canceled = true;
        return;
    }

    clearPane(p);
    UpdateWindow(p->hwndList);

    struct FileNode* child = p->currPath->children;

    int maxItems = getChildNodeCount(p->currPath);
    p->items = calloc(maxItems + 1, sizeof(struct ListItem));
    int index = 0;

    while (child) {
        // Game mode: show folders and .exe only
        if (gameMode && child->type == TYPE_FILE) {
            wchar_t* dot = wcsrchr(child->name, L'.');
            if (!dot || _wcsicmp(dot, L".exe") != 0) { child = child->sibling; continue; }
        }
        struct ListItem* item = &p->items[index++];
        item->node = child;
        item->loaded = false;

        fillFileInfo(child, item);
        p->totalSize += item->size;

        child = child->sibling;
    }
    p->numItems = index;  // actual count after game-mode filter

    HIMAGELIST himlBig, himlSmall;
    Shell_GetImageLists(&himlBig, &himlSmall);

    if (p->viewStyle == STYLE_LARGE_ICON) {
        ListView_SetImageList(p->hwndList, himlBig, LVSIL_NORMAL);
    }
    else ListView_SetImageList(p->hwndList, himlSmall, LVSIL_SMALL);

    if (p->sortColumnIdx != -1) sortItems(p);
    ListView_SetItemCountEx(p->hwndList, p->numItems, 0);

    updateStatusbar(p);
    updatePaneLabel(p);
}

void refreshContentView() {
    // Keep the active pane's stored path in sync with the global cursor, then refresh it.
    activePane()->currPath = currPathFileNode;
    refreshPane(activePane());
}


// Refresh column headers and status bar after runtime language switch
void cvRefreshLanguage(void) {
    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT;
    for (int i = 0; i < NUM_PANES; i++) {
        if (!panes[i].hwndList) continue;
        lvc.pszText = lc_str.name;
        ListView_SetColumn(panes[i].hwndList, COLUMN_NAME_IDX, &lvc);
        lvc.pszText = lc_str.type;
        ListView_SetColumn(panes[i].hwndList, COLUMN_TYPE_IDX, &lvc);
        lvc.pszText = lc_str.size;
        ListView_SetColumn(panes[i].hwndList, COLUMN_SIZE_IDX, &lvc);
        lvc.pszText = lc_str.date;
        ListView_SetColumn(panes[i].hwndList, COLUMN_DATE_IDX, &lvc);
    }
    updateStatusbar(activePane());
    InvalidateRect(hwndMain, NULL, TRUE);
}

// Copy the full path of the selected file to clipboard
static void onMenuItemCopyPathClick() {
    if (numSelectedItems != 1) return;
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], path);
    if (OpenClipboard(hwndMain)) {
        EmptyClipboard();
        size_t len = (wcslen(path) + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            wchar_t* p = (wchar_t*)GlobalLock(hMem);
            if (p) {
                wcscpy_s(p, len / sizeof(wchar_t), path);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

// Open cmd.exe at the current directory
static void onMenuItemOpenCmdClick() {
    if (!currPathFileNode) return;
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, path);
    wchar_t params[MAX_PATH + 16] = {0};
    wcscpy_s(params, MAX_PATH + 16, L"/K cd /d \"");
    wcscat_s(params, MAX_PATH + 16, path);
    wcscat_s(params, MAX_PATH + 16, L"\"");
    ShellExecuteW(hwndMain, L"open", L"cmd.exe", params, path, SW_SHOW);
}

// --- 7z archive extraction --------------------------------------------------------------
static bool isArchiveExt(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    const wchar_t* ext = dot + 1;
    return !_wcsicmp(ext, L"zip") || !_wcsicmp(ext, L"7z") ||
           !_wcsicmp(ext, L"rar") || !_wcsicmp(ext, L"tar") ||
           !_wcsicmp(ext, L"gz") || !_wcsicmp(ext, L"bz2") ||
           !_wcsicmp(ext, L"xz") || !_wcsicmp(ext, L"iso");
}

static bool find7z(wchar_t* out) {
    out[0] = L'\0';
    if (SearchPathW(NULL, L"7z.exe", NULL, MAX_PATH, out, NULL)) return true;
    static const wchar_t* candidates[] = {
        L"C:\\Program Files\\7-Zip\\7z.exe",
        L"C:\\Program Files (x86)\\7-Zip\\7z.exe",
        L"Z:\\opt\\apps\\7-Zip\\7z.exe",
        L"Z:\\opt\\apps\\7-Zip\\7za.exe",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (isPathExists(candidates[i])) { wcscpy_s(out, MAX_PATH, candidates[i]); return true; }
    }
    return false;
}

struct ExtractArg {
    wchar_t archive[MAX_PATH];
    wchar_t outDir[MAX_PATH];
    wchar_t exe7z[MAX_PATH];
};

static DWORD WINAPI extractThreadProc(LPVOID param) {
    struct ExtractArg* arg = (struct ExtractArg*)param;
    CreateDirectoryW(arg->outDir, NULL);
    wchar_t cmdLine[MAX_PATH * 3 + 32];
    swprintf_s(cmdLine, _countof(cmdLine), L"\"%ls\" x -y \"%ls\" -o\"%ls\"",
               arg->exe7z, arg->archive, arg->outDir);
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    if (CreateProcessW(arg->exe7z, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
    }
    free(arg);
    PostMessageW(hwndMain, WM_USER_EXTRACT_DONE, 0, 0);
    return 0;
}

static void run7zExtract(const wchar_t* archive, const wchar_t* outDir) {
    wchar_t exe7z[MAX_PATH] = {0};
    if (!find7z(exe7z)) {
        MessageBoxW(hwndMain, L"7-Zip not found in the container. Install 7z or add it to PATH.",
                    L"7z", MB_OK | MB_ICONERROR);
        return;
    }
    struct ExtractArg* arg = (struct ExtractArg*)malloc(sizeof(struct ExtractArg));
    if (!arg) return;
    wcscpy_s(arg->archive, MAX_PATH, archive);
    wcscpy_s(arg->outDir, MAX_PATH, outDir);
    wcscpy_s(arg->exe7z, MAX_PATH, exe7z);
    HANDLE hThread = CreateThread(NULL, 0, extractThreadProc, arg, 0, NULL);
    if (hThread) CloseHandle(hThread);
    else free(arg);
}

static void onMenuItemExtractHereClick() {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t archive[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], archive);
    if (!currPathFileNode) return;
    wchar_t curDir[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, curDir);
    run7zExtract(archive, curDir);
}

static void onMenuItemExtractToFolderClick() {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t archive[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], archive);
    if (!currPathFileNode) return;
    wchar_t curDir[MAX_PATH] = {0};
    getFileNodePath(currPathFileNode, curDir);
    // Output folder = current dir\basename(without extension).
    const wchar_t* base = wcsrchr(archive, L'\\');
    base = base ? base + 1 : archive;
    wchar_t folder[MAX_PATH];
    wcscpy_s(folder, MAX_PATH, curDir);
    if (folder[wcslen(folder)-1] != L'\\') wcscat_s(folder, MAX_PATH, L"\\");
    wchar_t nameNoExt[MAX_PATH] = {0};
    wcscpy_s(nameNoExt, MAX_PATH, base);
    wchar_t* dot = wcsrchr(nameNoExt, L'.');
    if (dot) *dot = L'\0';
    wcscat_s(folder, MAX_PATH, nameNoExt);
    run7zExtract(archive, folder);
}


// ============================================================================
// OLE Drag and Drop: drag files OUT to other apps (AlphaRom etc.), accept files IN
// ============================================================================

// --- IDropSource implementation (required by Wine; NULL does not work) ---
typedef struct {
    IDropSourceVtbl* lpVtbl;
    LONG refCount;
} DropSourceImpl;

static HRESULT STDMETHODCALLTYPE DropSrc_QueryInterface(IDropSource* This, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropSource)) {
        *ppv = This; This->lpVtbl->AddRef(This); return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DropSrc_AddRef(IDropSource* This) { return InterlockedIncrement(&((DropSourceImpl*)This)->refCount); }
static ULONG STDMETHODCALLTYPE DropSrc_Release(IDropSource* This) {
    DropSourceImpl* o = (DropSourceImpl*)This;
    ULONG c = InterlockedDecrement(&o->refCount);
    if (c == 0) free(o);
    return c;
}
static HRESULT STDMETHODCALLTYPE DropSrc_QueryContinueDrag(IDropSource* This, BOOL fEsc, DWORD grfKey) {
    if (fEsc) return DRAGDROP_S_CANCEL;
    // Use GetAsyncKeyState as fallback: Wine may not update grfKey reliably
    if (!(grfKey & MK_LBUTTON) || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        return DRAGDROP_S_DROP;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE DropSrc_GiveFeedback(IDropSource* This, DWORD dwEffect) { return DRAGDROP_S_USEDEFAULTCURSORS; }

static IDropSourceVtbl dropSourceVtbl = {
    DropSrc_QueryInterface, DropSrc_AddRef, DropSrc_Release,
    DropSrc_QueryContinueDrag, DropSrc_GiveFeedback
};

static IDropSource* createDropSource(void) {
    DropSourceImpl* o = calloc(1, sizeof(DropSourceImpl));
    if (!o) return NULL;
    o->lpVtbl = &dropSourceVtbl;
    o->refCount = 1;
    return (IDropSource*)o;
}

// --- IDataObject implementation (drag source) ---
typedef struct {
    IDataObjectVtbl* lpVtbl;
    LONG refCount;
    STGMEDIUM stgMedium;
    UINT numFiles;
} FileDataObject;

static HRESULT STDMETHODCALLTYPE DataObj_QueryInterface(IDataObject* This, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DataObj_AddRef(IDataObject* This) {
    return InterlockedIncrement(&((FileDataObject*)This)->refCount);
}
static ULONG STDMETHODCALLTYPE DataObj_Release(IDataObject* This) {
    FileDataObject* obj = (FileDataObject*)This;
    ULONG c = InterlockedDecrement(&obj->refCount);
    if (c == 0) {
        if (obj->stgMedium.hGlobal) ReleaseStgMedium(&obj->stgMedium);
        free(obj);
    }
    return c;
}
static HRESULT STDMETHODCALLTYPE DataObj_GetData(IDataObject* This, FORMATETC* pfe, STGMEDIUM* pstg) {
    FileDataObject* obj = (FileDataObject*)This;
    if (pfe->cfFormat == CF_HDROP && (pfe->tymed == TYMED_NULL || (pfe->tymed & TYMED_HGLOBAL))) {
        SIZE_T sz = GlobalSize(obj->stgMedium.hGlobal);
        HGLOBAL hCopy = GlobalAlloc(GHND, sz);
        if (!hCopy) return E_OUTOFMEMORY;
        void* src = GlobalLock(obj->stgMedium.hGlobal);
        void* dst = GlobalLock(hCopy);
        memcpy(dst, src, sz);
        GlobalUnlock(obj->stgMedium.hGlobal);
        GlobalUnlock(hCopy);
        pstg->tymed = TYMED_HGLOBAL;
        pstg->hGlobal = hCopy;
        pstg->pUnkForRelease = NULL;
        return S_OK;
    }
    return DV_E_FORMATETC;
}
static HRESULT STDMETHODCALLTYPE DataObj_GetDataHere(IDataObject* This, FORMATETC* pfe, STGMEDIUM* pstg) {
    return DataObj_GetData(This, pfe, pstg);
}
static HRESULT STDMETHODCALLTYPE DataObj_QueryGetData(IDataObject* This, FORMATETC* pfe) {
    if (pfe->cfFormat == CF_HDROP && (pfe->tymed == TYMED_NULL || (pfe->tymed & TYMED_HGLOBAL))) return S_OK;
    return DV_E_FORMATETC;
}
static HRESULT STDMETHODCALLTYPE DataObj_GetCanonicalFormatEtc(IDataObject* This, FORMATETC* pfe, FORMATETC* pfeOut) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DataObj_SetData(IDataObject* This, FORMATETC* pfe, STGMEDIUM* pstg, BOOL fRelease) { return E_NOTIMPL; }
// Simple IEnumFORMATETC for CF_HDROP
typedef struct {
    IEnumFORMATETCVtbl* lpVtbl;
    LONG refCount;
    ULONG index;
} EnumFmtEtcImpl;

static HRESULT STDMETHODCALLTYPE EnumFmt_QueryInterface(IEnumFORMATETC* This, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumFORMATETC)) {
        *ppv = This; This->lpVtbl->AddRef(This); return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE EnumFmt_AddRef(IEnumFORMATETC* This) { return InterlockedIncrement(&((EnumFmtEtcImpl*)This)->refCount); }
static ULONG STDMETHODCALLTYPE EnumFmt_Release(IEnumFORMATETC* This) {
    ULONG c = InterlockedDecrement(&((EnumFmtEtcImpl*)This)->refCount);
    if (c == 0) free(This);
    return c;
}
static HRESULT STDMETHODCALLTYPE EnumFmt_Next(IEnumFORMATETC* This, ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) {
    EnumFmtEtcImpl* e = (EnumFmtEtcImpl*)This;
    if (e->index >= 1) { if (pceltFetched) *pceltFetched = 0; return S_FALSE; }
    rgelt[0].cfFormat = CF_HDROP;
    rgelt[0].ptd = NULL;
    rgelt[0].dwAspect = DVASPECT_CONTENT;
    rgelt[0].lindex = -1;
    rgelt[0].tymed = TYMED_HGLOBAL;
    e->index = 1;
    if (pceltFetched) *pceltFetched = 1;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE EnumFmt_Skip(IEnumFORMATETC* This, ULONG celt) {
    ((EnumFmtEtcImpl*)This)->index += celt;
    return ((EnumFmtEtcImpl*)This)->index >= 1 ? S_FALSE : S_OK;
}
static HRESULT STDMETHODCALLTYPE EnumFmt_Reset(IEnumFORMATETC* This) {
    ((EnumFmtEtcImpl*)This)->index = 0; return S_OK;
}
static HRESULT STDMETHODCALLTYPE EnumFmt_Clone(IEnumFORMATETC* This, IEnumFORMATETC** pp) {
    if (!pp) return E_POINTER;
    EnumFmtEtcImpl* e = calloc(1, sizeof(EnumFmtEtcImpl));
    e->lpVtbl = ((EnumFmtEtcImpl*)This)->lpVtbl;
    e->refCount = 1;
    e->index = ((EnumFmtEtcImpl*)This)->index;
    *pp = (IEnumFORMATETC*)e;
    return S_OK;
}
static IEnumFORMATETCVtbl enumFmtVtbl = {
    EnumFmt_QueryInterface, EnumFmt_AddRef, EnumFmt_Release,
    EnumFmt_Next, EnumFmt_Skip, EnumFmt_Reset, EnumFmt_Clone
};

static HRESULT STDMETHODCALLTYPE DataObj_EnumFormatEtc(IDataObject* This, DWORD dw, IEnumFORMATETC** pp) {
    if (!pp) return E_POINTER;
    if (dw != DATADIR_GET) return E_NOTIMPL;
    EnumFmtEtcImpl* e = calloc(1, sizeof(EnumFmtEtcImpl));
    e->lpVtbl = &enumFmtVtbl;
    e->refCount = 1;
    e->index = 0;
    *pp = (IEnumFORMATETC*)e;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE DataObj_DAdvise(IDataObject* This, FORMATETC* pfe, DWORD advf, IAdviseSink* pAdv, DWORD* pdw) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DataObj_DUnadvise(IDataObject* This, DWORD dw) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DataObj_EnumDAdvise(IDataObject* This, IEnumSTATDATA** pp) { return E_NOTIMPL; }

static IDataObjectVtbl dataObjVtbl = {
    DataObj_QueryInterface, DataObj_AddRef, DataObj_Release,
    DataObj_GetData, DataObj_GetDataHere, DataObj_QueryGetData,
    DataObj_GetCanonicalFormatEtc, DataObj_SetData, DataObj_EnumFormatEtc,
    DataObj_DAdvise, DataObj_DUnadvise, DataObj_EnumDAdvise
};

// Build an HDROP global memory block from selected file paths
static HGLOBAL buildHDropFromSelection(void) {
    // Collect paths
    wchar_t paths[1024]; // concatenated double-null terminated list
    paths[0] = L'\0';
    int totalLen = 0;
    for (int i = 0; i < numSelectedItems && totalLen < 1000; i++) {
        wchar_t path[MAX_PATH] = {0};
        getFileNodePath(selectedItems[i], path);
        int len = wcslen(path);
        if (totalLen + len + 1 >= 1024) break;
        wcscpy_s(paths + totalLen, 1024 - totalLen, path);
        totalLen += len + 1;
    }
    paths[totalLen] = L'\0'; // double null terminator
    totalLen++;

    size_t hdrSize = sizeof(DROPFILES);
    size_t dataSize = totalLen * sizeof(wchar_t);
    // GMEM_DDESHARE makes the block visible across processes (needed when the
    // same HDROP is delivered to another app via WM_DROPFILES under Wine).
    HGLOBAL hMem = GlobalAlloc(GHND | GMEM_DDESHARE, hdrSize + dataSize);
    if (!hMem) return NULL;
    DROPFILES* df = (DROPFILES*)GlobalLock(hMem);
    df->pFiles = hdrSize;
    df->fWide = TRUE;
    df->pt.x = 0; df->pt.y = 0;
    df->fNC = FALSE;
    memcpy((BYTE*)df + hdrSize, paths, dataSize);
    GlobalUnlock(hMem);
    return hMem;
}

// Start dragging selected files
// Fallback: when OLE drag is rejected by the target (common under Wine/X11),
// deliver the files to the window under the cursor. Classic Win32 programs
// (e.g. crack/patcher tools) read drops through WM_DROPFILES and ignore command
// line arguments, so we post a shared HDROP to the already-running window; we
// only ShellExecute when dropping onto the desktop/taskbar (no app window).
static void dragFallbackOpenWith(HWND hwndMain) {
    POINT pt; GetCursorPos(&pt);
    HWND target = WindowFromPoint(pt);
    if (!target) return;
    HWND top = GetAncestor(target, GA_ROOT);
    if (top == hwndMain) return;  // dropped on our own window

    wchar_t cls[64] = {0};
    GetClassNameW(top, cls, 63);
    bool isShell = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
                   !wcscmp(cls, L"Shell_TrayWnd");

    updateSelectedItems();
    if (numSelectedItems == 0) return;

    if (!isShell) {
        // A real application window is already open: hand it a WM_DROPFILES.
        // Only post to the top-level window (DragAcceptFiles is registered there)
        // and use a timed send so a hung/crash-prone target cannot freeze us.
        HGLOBAL hDrop = buildHDropFromSelection();
        if (hDrop) {
            LRESULT result = 0;
            SendMessageTimeoutW(top, WM_DROPFILES, (WPARAM)hDrop, 0,
                                SMTO_ABORTIFHUNG, 2000, (PDWORD_PTR)&result);
            // Ownership transfers to the receiver on success; if the target did
            // not handle it (no DragAcceptFiles), we must free it ourselves.
            // We cannot reliably detect handling, so leak the small block rather
            // than double-free. This is acceptable for a one-off drag.
            return;
        }
    }

    // Shell/desktop or allocation failure: fall back to launching the program.
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    if (pid == 0) return;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return;
    wchar_t exePath[MAX_PATH] = {0};
    DWORD exeLen = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, exePath, &exeLen);
    CloseHandle(hProc);
    if (!ok || !exePath[0]) return;
    wchar_t filePath[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], filePath);
    wchar_t workDir[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0]->parent, workDir);
    ShellExecuteW(hwndMain, L"open", exePath, filePath, workDir, SW_SHOW);
}

static void startFileDrag(HWND hwnd) {
    ReleaseCapture();  // DoDragDrop manages its own mouse capture
    updateSelectedItems();
    if (numSelectedItems == 0) return;
    HGLOBAL hDrop = buildHDropFromSelection();
    if (!hDrop) return;

    FileDataObject* obj = calloc(1, sizeof(FileDataObject));
    if (!obj) { GlobalFree(hDrop); return; }
    obj->lpVtbl = &dataObjVtbl;
    obj->refCount = 1;
    obj->stgMedium.tymed = TYMED_HGLOBAL;
    obj->stgMedium.hGlobal = hDrop;
    obj->numFiles = numSelectedItems;

    DWORD dwEffect = DROPEFFECT_COPY;
    IDropSource* pDropSrc = createDropSource();
    DoDragDrop((IDataObject*)obj, pDropSrc, DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwEffect);
    // If no OLE target accepted the drop (Wine/X11 cross-process limitation),
    // fall back to opening the file with whatever program is under the cursor.
    if (dwEffect == DROPEFFECT_NONE) {
        dragFallbackOpenWith(hwndMain);
    }
    if (pDropSrc) pDropSrc->lpVtbl->Release(pDropSrc);
    obj->lpVtbl->Release((IDataObject*)obj);
}

// --- IDropTarget implementation (drop target) ---
typedef struct {
    IDropTargetVtbl* lpVtbl;
    LONG refCount;
    bool canAccept;
} DropTargetImpl;

static HRESULT STDMETHODCALLTYPE DropTarget_QueryInterface(IDropTarget* This, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DropTarget_AddRef(IDropTarget* This) {
    return InterlockedIncrement(&((DropTargetImpl*)This)->refCount);
}
static ULONG STDMETHODCALLTYPE DropTarget_Release(IDropTarget* This) {
    DropTargetImpl* obj = (DropTargetImpl*)This;
    ULONG c = InterlockedDecrement(&obj->refCount);
    if (c == 0) free(obj);
    return c;
}
static HWND resolveListHwnd(HWND h);
// Resolve which list pane a screen point is over. WindowFromPoint is unreliable
// during the DoDragDrop modal loop (mouse capture / child controls), so first
// hit-test every pane's window rectangle, then fall back to parent walking.
static HWND paneListAtPoint(POINTL pt) {
    POINT sp = {pt.x, pt.y};
    for (int i = 0; i < NUM_PANES; i++) {
        HWND lh = panes[i].hwndList;
        if (lh && IsWindowVisible(lh)) {
            RECT r;
            if (GetWindowRect(lh, &r) && PtInRect(&r, sp)) return lh;
        }
    }
    return resolveListHwnd(WindowFromPoint(sp));
}
static HRESULT STDMETHODCALLTYPE DropTarget_DragEnter(IDropTarget* This, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    FORMATETC fe = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    ((DropTargetImpl*)This)->canAccept = (pDataObj->lpVtbl->QueryGetData(pDataObj, &fe) == S_OK);
    g_dropHwnd = paneListAtPoint(pt);
    *pdwEffect = ((DropTargetImpl*)This)->canAccept ? (g_dropHwnd ? DROPEFFECT_COPY : DROPEFFECT_NONE) : DROPEFFECT_NONE;
    return S_OK;
}
static HWND resolveListHwnd(HWND h) {
    // Walk up parents until we find one of our list panes
    while (h) {
        for (int i = 0; i < NUM_PANES; i++) if (panes[i].hwndList == h) return h;
        h = GetParent(h);
    }
    return NULL;
}
static HRESULT STDMETHODCALLTYPE DropTarget_DragOver(IDropTarget* This, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    g_dropHwnd = paneListAtPoint(pt);
    *pdwEffect = ((DropTargetImpl*)This)->canAccept ? (g_dropHwnd ? DROPEFFECT_COPY : DROPEFFECT_NONE) : DROPEFFECT_NONE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE DropTarget_DragLeave(IDropTarget* This) { return S_OK; }
static HRESULT STDMETHODCALLTYPE DropTarget_Drop(IDropTarget* This, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    FORMATETC fe = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg = {0};
    if (pDataObj->lpVtbl->GetData(pDataObj, &fe, &stg) == S_OK) {
        HDROP hDrop = (HDROP)stg.hGlobal;
        // Determine destination: check if dropped on a folder item
        wchar_t dstDir[MAX_PATH] = {0};
        bool dropOnFolder = false;
        if (g_dropHwnd) {
            POINT clientPt = {pt.x, pt.y};
            ScreenToClient(g_dropHwnd, &clientPt);
            LVHITTESTINFO ht;
            ht.pt = clientPt;
            int itemIdx = ListView_HitTest(g_dropHwnd, &ht);
            if (itemIdx >= 0 && (ht.flags & LVHT_ONITEM)) {
                struct Pane* tp = paneFromHwnd(g_dropHwnd);
                if (tp && itemIdx < tp->numItems && tp->items[itemIdx].node->type == TYPE_DIR) {
                    getFileNodePath(tp->items[itemIdx].node, dstDir);
                    dropOnFolder = true;
                }
            }
        }
        if (!dropOnFolder) {
            // Dropped on empty area: use the target pane's current directory (supports dual-pane)
            struct Pane* targetPane = g_dropHwnd ? paneFromHwnd(g_dropHwnd) : NULL;
            struct FileNode* targetNode = targetPane ? targetPane->currPath : currPathFileNode;
            if (targetNode) getFileNodePath(targetNode, dstDir);
        }

        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < count; i++) {
            wchar_t srcPath[MAX_PATH] = {0};
            DragQueryFileW(hDrop, i, srcPath, MAX_PATH);
            if (dstDir[0]) {
                wchar_t dstPath[MAX_PATH] = {0};
                wchar_t* name = wcsrchr(srcPath, L'\\');
                name = name ? name + 1 : srcPath;
                swprintf_s(dstPath, MAX_PATH, L"%ls\\%ls", dstDir, name);
                CopyFileW(srcPath, dstPath, FALSE);
            }
        }
        DragFinish(hDrop);
        ReleaseStgMedium(&stg);
        // Refresh both panes (drop may target the inactive pane)
        for (int pi = 0; pi < NUM_PANES; pi++) refreshPane(&panes[pi]);
    }
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}

static IDropTargetVtbl dropTargetVtbl = {
    DropTarget_QueryInterface, DropTarget_AddRef, DropTarget_Release,
    DropTarget_DragEnter, DropTarget_DragOver, DropTarget_DragLeave, DropTarget_Drop
};

static IDropTarget* createDropTarget(void) {
    DropTargetImpl* obj = calloc(1, sizeof(DropTargetImpl));
    if (!obj) return NULL;
    obj->lpVtbl = &dropTargetVtbl;
    obj->refCount = 1;
    return (IDropTarget*)obj;
}

// ============================================================================
// FEATURE PACK: MD5, Extract Icon, Nav History, Text Viewer, Folder Size,
// Game Mode, Batch Rename, Recent Places, Compare Panes, Copy/Move To
// ============================================================================

// ---------- MD5 (RFC 1321, pure C, no external dependency) ----------
typedef struct { unsigned int a,b,c,d; unsigned long long len; unsigned char buf[64]; } MD5_CTX;
static void md5_init(MD5_CTX* c) { c->a=0x67452301;c->b=0xefcdab89;c->c=0x98badcfe;c->d=0x10325476;c->len=0; }
#define ML(x,n) (((x)>>(n))|((x)<<(32-(n))))
static unsigned int F(unsigned int x,unsigned int y,unsigned int z){return (x&y)|(~x&z);}
static unsigned int G(unsigned int x,unsigned int y,unsigned int z){return (x&z)|(y&~z);}
static unsigned int H(unsigned int x,unsigned int y,unsigned int z){return x^y^z;}
static unsigned int I(unsigned int x,unsigned int y,unsigned int z){return y^(x|~z);}
static void R1(unsigned int*a,unsigned int b,unsigned int c,unsigned int d,unsigned int x,unsigned int s,unsigned int t){*a=b+ML((*a+F(b,c,d)+x+t),s);}
static void R2(unsigned int*a,unsigned int b,unsigned int c,unsigned int d,unsigned int x,unsigned int s,unsigned int t){*a=b+ML((*a+G(b,c,d)+x+t),s);}
static void R3(unsigned int*a,unsigned int b,unsigned int c,unsigned int d,unsigned int x,unsigned int s,unsigned int t){*a=b+ML((*a+H(b,c,d)+x+t),s);}
static void R4(unsigned int*a,unsigned int b,unsigned int c,unsigned int d,unsigned int x,unsigned int s,unsigned int t){*a=b+ML((*a+I(b,c,d)+x+t),s);}
static void md5_transform(MD5_CTX* c, unsigned char* p) {
    unsigned int x[16],i; for(i=0;i<16;i++) x[i]=p[i*4]|(p[i*4+1]<<8)|(p[i*4+2]<<16)|((unsigned int)p[i*4+3]<<24);
    unsigned int a=c->a,b=c->b,cc=c->c,d=c->d;
    R1(&a,b,cc,d,x[0],7,0xd76aa478);R1(&d,a,b,cc,x[1],12,0xe8c7b756);R1(&cc,d,a,b,x[2],17,0x242070db);R1(&b,cc,d,a,x[3],22,0xc1bdceee);
    R1(&a,b,cc,d,x[4],7,0xf57c0faf);R1(&d,a,b,cc,x[5],12,0x4787c62a);R1(&cc,d,a,b,x[6],17,0xa8304613);R1(&b,cc,d,a,x[7],22,0xfd469501);
    R1(&a,b,cc,d,x[8],7,0x698098d8);R1(&d,a,b,cc,x[9],12,0x8b44f7af);R1(&cc,d,a,b,x[10],17,0xffff5bb1);R1(&b,cc,d,a,x[11],22,0x895cd7be);
    R1(&a,b,cc,d,x[12],7,0x6b901122);R1(&d,a,b,cc,x[13],12,0xfd987193);R1(&cc,d,a,b,x[14],17,0xa679438e);R1(&b,cc,d,a,x[15],22,0x49b40821);
    R2(&a,b,cc,d,x[1],5,0xf61e2562);R2(&d,a,b,cc,x[6],9,0xc040b340);R2(&cc,d,a,b,x[11],14,0x265e5a51);R2(&b,cc,d,a,x[0],20,0xe9b6c7aa);
    R2(&a,b,cc,d,x[5],5,0xd62f105d);R2(&d,a,b,cc,x[10],9,0x02441453);R2(&cc,d,a,b,x[15],14,0xd8a1e681);R2(&b,cc,d,a,x[4],20,0xe7d3fbc8);
    R2(&a,b,cc,d,x[9],5,0x21e1cde6);R2(&d,a,b,cc,x[14],9,0xc33707d6);R2(&cc,d,a,b,x[3],14,0xf4d50d87);R2(&b,cc,d,a,x[8],20,0x455a14ed);
    R2(&a,b,cc,d,x[13],5,0xa9e3e905);R2(&d,a,b,cc,x[2],9,0xfcefa3f8);R2(&cc,d,a,b,x[7],14,0x676f02d9);R2(&b,cc,d,a,x[12],20,0x8d2a4c8a);
    R3(&a,b,cc,d,x[5],4,0xfffa3942);R3(&d,a,b,cc,x[8],11,0x8771f681);R3(&cc,d,a,b,x[11],16,0x6d9d6122);R3(&b,cc,d,a,x[14],23,0xfde5380c);
    R3(&a,b,cc,d,x[1],4,0xa4beea44);R3(&d,a,b,cc,x[4],11,0x4bdecfa9);R3(&cc,d,a,b,x[7],16,0xf6bb4b60);R3(&b,cc,d,a,x[10],23,0xbebfbc70);
    R3(&a,b,cc,d,x[13],4,0x289b7ec6);R3(&d,a,b,cc,x[0],11,0xeaa127fa);R3(&cc,d,a,b,x[3],16,0xd4ef3085);R3(&b,cc,d,a,x[6],23,0x04881d05);
    R3(&a,b,cc,d,x[9],4,0xd9d4d039);R3(&d,a,b,cc,x[12],11,0xe6db99e5);R3(&cc,d,a,b,x[15],16,0x1fa27cf8);R3(&b,cc,d,a,x[2],23,0xc4ac5665);
    R4(&a,b,cc,d,x[0],6,0xf4292244);R4(&d,a,b,cc,x[7],10,0x432aff97);R4(&cc,d,a,b,x[14],15,0xab9423a7);R4(&b,cc,d,a,x[5],21,0xfc93a039);
    R4(&a,b,cc,d,x[12],6,0x655b59c3);R4(&d,a,b,cc,x[3],10,0x8f0ccc92);R4(&cc,d,a,b,x[10],15,0xffeff47d);R4(&b,cc,d,a,x[1],21,0x85845dd1);
    R4(&a,b,cc,d,x[8],6,0x6fa87e4f);R4(&d,a,b,cc,x[15],10,0xfe2ce6e0);R4(&cc,d,a,b,x[6],15,0xa3014314);R4(&b,cc,d,a,x[13],21,0x4e0811a1);
    R4(&a,b,cc,d,x[4],6,0xf7537e82);R4(&d,a,b,cc,x[11],10,0xbd3af235);R4(&cc,d,a,b,x[2],15,0x2ad7d2bb);R4(&b,cc,d,a,x[9],21,0xeb86d391);
    c->a+=a;c->b+=b;c->c+=cc;c->d+=d;
}
static void md5_update(MD5_CTX* c, const unsigned char* data, size_t n) {
    size_t i, idx = c->len & 63; c->len += n;
    for(i=0;i<n;i++){ c->buf[idx++]=data[i]; if(idx==64){md5_transform(c,c->buf);idx=0;} }
}
static void md5_final(MD5_CTX* c, unsigned char out[16]) {
    size_t idx = c->len & 63; c->buf[idx++]=0x80;
    if(idx>56){ while(idx<64)c->buf[idx++]=0; md5_transform(c,c->buf); idx=0; }
    while(idx<56)c->buf[idx++]=0;
    unsigned long long bits=c->len*8; int j; for(j=0;j<8;j++)c->buf[56+j]=(unsigned char)(bits>>(j*8));
    md5_transform(c,c->buf);
    for(j=0;j<4;j++){out[j]=(unsigned char)(c->a>>(j*8));out[j+4]=(unsigned char)(c->b>>(j*8));out[j+8]=(unsigned char)(c->c>>(j*8));out[j+12]=(unsigned char)(c->d>>(j*8));}
}
static void computeFileMD5(wchar_t* path, wchar_t* out, size_t outLen) {
    out[0]=0;
    HANDLE hf=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(hf==INVALID_HANDLE_VALUE)return;
    MD5_CTX ctx; md5_init(&ctx);
    unsigned char buf[65536]; DWORD rd;
    while(ReadFile(hf,buf,sizeof(buf),&rd,NULL)&&rd>0) md5_update(&ctx,buf,rd);
    CloseHandle(hf);
    unsigned char digest[16]; md5_final(&ctx,digest);
    int i,pos=0; for(i=0;i<16;i++){pos+=swprintf_s(out+pos,outLen-pos,L"%02x",digest[i]);}
}

// ---------- Navigation history (back/forward) ----------
#define NAV_MAX 32
static wchar_t* navBack[NAV_MAX]; static int navBackCount=0;
static wchar_t* navFwd[NAV_MAX]; static int navFwdCount=0;
static bool navSuppressing=false;
void navPushHistory(wchar_t* path) {
    if(navSuppressing||!path)return;
    if(navBackCount>=NAV_MAX){free(navBack[0]);memmove(navBack,navBack+1,sizeof(wchar_t*)*(NAV_MAX-1));navBackCount--;}
    navBack[navBackCount++]=wcsdup(path);
    int i; for(i=0;i<navFwdCount;i++)free(navFwd[i]); navFwdCount=0;
}
void navGoBack(void) {
    if(navBackCount<2)return;
    // current is at top, move it to forward, navigate to previous
    wchar_t* cur=navBack[--navBackCount];
    if(navFwdCount<NAV_MAX)navFwd[navFwdCount++]=cur; else free(cur);
    wchar_t* target=navBack[navBackCount-1];
    navSuppressing=true;
    navigateToPath(target);
    navSuppressing=false;
}
void navGoForward(void) {
    if(navFwdCount==0)return;
    wchar_t* target=navFwd[--navFwdCount];
    if(navBackCount<NAV_MAX)navBack[navBackCount++]=wcsdup(target);
    navSuppressing=true;
    navigateToPath(target);
    navSuppressing=false;
}

// ---------- Recent places ----------
#define RECENT_MAX 10
static wchar_t* recentPaths[RECENT_MAX]; static int recentCount=0;
void recentAdd(wchar_t* path) {
    if(!path||!path[0])return;
    int i; for(i=0;i<recentCount;i++) if(wcscmp(recentPaths[i],path)==0){free(recentPaths[i]);memmove(recentPaths+i,recentPaths+i+1,sizeof(wchar_t*)*(recentCount-i-1));recentCount--;break;}
    if(recentCount>=RECENT_MAX){free(recentPaths[RECENT_MAX-1]);recentCount--;}
    memmove(recentPaths+1,recentPaths,sizeof(wchar_t*)*recentCount);
    recentPaths[0]=wcsdup(path); recentCount++;
}
void recentMenu(void) {
    HMENU m=CreatePopupMenu();
    if(recentCount==0){AppendMenuW(m,MF_STRING|MF_GRAYED,0,lc_str.no_recent);}
    int i; for(i=0;i<recentCount;i++) AppendMenuW(m,MF_STRING,400+i,recentPaths[i]);
    POINT pt; GetCursorPos(&pt);
    int cmd=TrackPopupMenu(m,TPM_RETURNCMD,pt.x,pt.y,0,hwndMain,NULL);
    DestroyMenu(m);
    if(cmd>=400&&cmd<400+recentCount) navigateToPath(recentPaths[cmd-400]);
}

// ---------- Extract icon to BMP ----------
static void onMenuItemExtractIconClick(void) {
    updateSelectedItems();
    if(numSelectedItems!=1)return;
    wchar_t srcPath[MAX_PATH]={0}; getFileNodePath(selectedItems[0],srcPath);
    SHFILEINFOW sfi={0};
    if(!SHGetFileInfoW(srcPath,0,&sfi,sizeof(sfi),SHGFI_ICON|SHGFI_LARGEICON)||!sfi.hIcon)return;
    ICONINFO ii={0}; GetIconInfo(sfi.hIcon,&ii);
    BITMAP bm={0}; GetObject(ii.hbmColor?ii.hbmColor:ii.hbmMask,sizeof(bm),&bm);
    int w=bm.bmWidth,h=bm.bmHeight;
    HDC hdc=GetDC(NULL);
    HDC memDC=CreateCompatibleDC(hdc);
    BITMAPINFO bi={0}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=h*2;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void* bits=NULL;
    HBITMAP hDib=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,&bits,NULL,0);
    HGDIOBJ old=SelectObject(memDC,hDib);
    DrawIconEx(memDC,0,0,sfi.hIcon,w,h,0,NULL,DI_NORMAL);
    // Write BMP file
    wchar_t* name=wcsrchr(srcPath,L'\\'); name=name?name+1:srcPath;
    wchar_t* dot=wcsrchr(name,L'.');
    wchar_t baseName[MAX_PATH]; wcsncpy_s(baseName,MAX_PATH,name,dot?(size_t)(dot-name):wcslen(name));
    wchar_t dir[MAX_PATH]={0}; getFileNodePath(selectedItems[0]->parent,dir);
    wchar_t outPath[MAX_PATH]={0};
    swprintf_s(outPath,MAX_PATH,L"%ls\\%ls_icon.bmp",dir,baseName);
    HANDLE hf=CreateFileW(outPath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(hf!=INVALID_HANDLE_VALUE){
        DWORD imgSize=w*h*4, wr;
        unsigned char hdr[54]={0};
        hdr[0]='B';hdr[1]='M';
        DWORD fileSize=54+imgSize;
        memcpy(hdr+2,&fileSize,4); hdr[10]=54;
        DWORD biSize=40; memcpy(hdr+14,&biSize,4);
        memcpy(hdr+18,&w,4); int h2=h*2; memcpy(hdr+22,&h2,4);
        short planes=1,bpp=32; memcpy(hdr+26,&planes,2); memcpy(hdr+28,&bpp,2);
        memcpy(hdr+34,&imgSize,4);
        WriteFile(hf,hdr,54,&wr,NULL);
        WriteFile(hf,bits,imgSize,&wr,NULL); CloseHandle(hf);
    }
    SelectObject(memDC,old); DeleteObject(hDib); DeleteDC(memDC); ReleaseDC(NULL,hdc);
    if(ii.hbmMask){DeleteObject(ii.hbmMask);}
    if(ii.hbmColor){DeleteObject(ii.hbmColor);}
    DestroyIcon(sfi.hIcon);
    MessageBoxW(hwndMain,outPath,lc_str.saved_icon,MB_OK|MB_ICONINFORMATION);
}

// ---------- MD5 menu ----------
static void onMenuItemMD5Click(void) {
    updateSelectedItems();
    if(numSelectedItems!=1)return;
    wchar_t path[MAX_PATH]={0}; getFileNodePath(selectedItems[0],path);
    wchar_t hash[64]={0}; computeFileMD5(path,hash,64);
    wchar_t msg[256]={0};
    wchar_t* name=wcsrchr(path,L'\\'); name=name?name+1:path;
    swprintf_s(msg,256,L"%ls\n\nMD5: %ls",name,hash);
    MessageBoxW(hwndMain,msg,lc_str.md5_title,MB_OK|MB_ICONINFORMATION);
}

// ---------- Text viewer (read-only, up to 64KB) ----------
static void onMenuItemViewTextClick(void) {
    updateSelectedItems();
    if(numSelectedItems!=1)return;
    wchar_t path[MAX_PATH]={0}; getFileNodePath(selectedItems[0],path);
    HANDLE hf=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(hf==INVALID_HANDLE_VALUE)return;
    static char raw[65536]; DWORD rd; ReadFile(hf,raw,65535,&rd,NULL); CloseHandle(hf);
    raw[rd]=0;
    // Try UTF-8 first, fall back to ANSI codepage
    int wlen=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw,-1,NULL,0);
    UINT cp = (wlen>0)?CP_UTF8:CP_ACP;
    if(wlen<=0) wlen=MultiByteToWideChar(CP_ACP,0,raw,-1,NULL,0);
    if(wlen<=0) return;
    wchar_t* wbuf=calloc(wlen+2,sizeof(wchar_t));
    MultiByteToWideChar(cp,0,raw,-1,wbuf,wlen);
    MessageBoxW(hwndMain,wbuf,lc_str.text_viewer,MB_OK);
    free(wbuf);
}

// ---------- Batch rename ----------
static void onMenuItemBatchRenameClick(void) {
    updateSelectedItems();
    if(numSelectedItems<2)return;
    wchar_t* find=InputDialog(lc_str.batch_rename,lc_str.find_text,L"",true);
    if(!find)return;
    wchar_t* repl=InputDialog(lc_str.batch_rename,lc_str.replace_text,L"",true);
    if(!repl){free(find);return;}
    int i;
    for(i=0;i<numSelectedItems;i++){
        wchar_t oldPath[MAX_PATH]={0}; getFileNodePath(selectedItems[i],oldPath);
        wchar_t newName[MAX_PATH]={0}; wcscpy_s(newName,MAX_PATH,selectedItems[i]->name);
        wchar_t* pos=wcsstr(newName,find);
        if(pos){
            wchar_t result[MAX_PATH]={0};
            size_t prefixLen=pos-newName;
            wcsncpy_s(result,MAX_PATH,newName,prefixLen);
            wcscat_s(result,MAX_PATH,repl);
            wcscat_s(result,MAX_PATH,pos+wcslen(find));
            wchar_t newPath[MAX_PATH]={0};
            getFileNodePath(selectedItems[i]->parent,newPath);
            wcscat_s(newPath,MAX_PATH,L"\\"); wcscat_s(newPath,MAX_PATH,result);
            MoveFileW(oldPath,newPath);
        }
    }
    free(find); free(repl);
    navigateRefresh();
}

// ---------- Game mode: large icons + exe only ----------
void onMenuItemGameModeClick(void) {
    gameMode=!gameMode;
    // Keep current view style; only filter to folders + .exe to avoid Wine LVS_ICON rendering issues
    navigateRefresh();
}

// ---------- Folder size (background thread) ----------
#define WM_FOLDERSIZE_DONE (WM_APP+77)
struct FolderSizeReq { struct FileNode* node; HWND hwnd; };
static unsigned long __stdcall folderSizeThread(void* param) {
    struct FolderSizeReq* req=(struct FolderSizeReq*)param;
    // Recursive sum
    unsigned long long total=0;
    // Use FindFirstFile recursively via path
    wchar_t base[MAX_PATH]={0}; getFileNodePath(req->node,base);
    // iterative stack
    wchar_t stack[64][MAX_PATH]; int sp=0;
    wcscpy_s(stack[sp],MAX_PATH,base); sp++;
    while(sp>0){
        sp--; wchar_t cur[MAX_PATH]; wcscpy_s(cur,MAX_PATH,stack[sp]);
        wchar_t pattern[MAX_PATH]; swprintf_s(pattern,MAX_PATH,L"%ls\\*",cur);
        WIN32_FIND_DATAW fd; HANDLE hf=FindFirstFileW(pattern,&fd);
        if(hf==INVALID_HANDLE_VALUE)continue;
        do{
            if(wcscmp(fd.cFileName,L".")==0||wcscmp(fd.cFileName,L"..")==0)continue;
            wchar_t full[MAX_PATH]; swprintf_s(full,MAX_PATH,L"%ls\\%ls",cur,fd.cFileName);
            if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
                if(sp<64){wcscpy_s(stack[sp],MAX_PATH,full);sp++;}
            } else {
                LARGE_INTEGER sz; sz.LowPart=fd.nFileSizeLow; sz.HighPart=fd.nFileSizeHigh;
                total+=sz.QuadPart;
            }
        }while(FindNextFileW(hf,&fd));
        FindClose(hf);
    }
    PostMessage(req->hwnd,WM_FOLDERSIZE_DONE,(WPARAM)total,(LPARAM)wcsdup(base));
    free(req);
    return 0;
}
static void onMenuItemFolderSizeClick(void) {
    updateSelectedItems();
    if(numSelectedItems!=1||selectedItems[0]->type!=TYPE_DIR)return;
    struct FolderSizeReq* req=calloc(1,sizeof(struct FolderSizeReq));
    req->node=selectedItems[0]; req->hwnd=hwndMain;
    CreateThread(NULL,0,folderSizeThread,req,0,NULL);
}

// ---------- Compare panes: select items that differ ----------
void onMenuItemComparePanesClick(void) {
    struct Pane* p0=&panes[0]; struct Pane* p1=&panes[1];
    if(p0->numItems==0 && p1->numItems==0){
        MessageBoxW(hwndMain,lc_str.no_recent,lc_str.compare_panes,MB_OK|MB_ICONINFORMATION);
        return;
    }
    int onlyLeft=0, onlyRight=0, common=0;
    int i,j;
    // Clear selection in both panes
    ListView_SetItemState(p0->hwndList,-1,0,LVIS_SELECTED);
    ListView_SetItemState(p1->hwndList,-1,0,LVIS_SELECTED);
    // Find items in p0 not present in p1 -> select them in p0
    for(i=0;i<p0->numItems;i++){
        bool found=false;
        for(j=0;j<p1->numItems;j++){
            if(wcscmp(p0->items[i].node->name,p1->items[j].node->name)==0){found=true;break;}
        }
        if(found) common++;
        else { ListView_SetItemState(p0->hwndList,i,LVIS_SELECTED,LVIS_SELECTED); onlyLeft++; }
    }
    // Find items in p1 not present in p0 -> select them in p1
    for(j=0;j<p1->numItems;j++){
        bool found=false;
        for(i=0;i<p0->numItems;i++){
            if(wcscmp(p1->items[j].node->name,p0->items[i].node->name)==0){found=true;break;}
        }
        if(!found){ ListView_SetItemState(p1->hwndList,j,LVIS_SELECTED,LVIS_SELECTED); onlyRight++; }
    }
    wchar_t msg[256];
    swprintf_s(msg,256,L"Left only: %d   Right only: %d   Common: %d",onlyLeft,onlyRight,common);
    MessageBoxW(hwndMain,msg,(onlyLeft==0&&onlyRight==0)?lc_str.panes_same:lc_str.panes_diff,
        MB_OK|MB_ICONINFORMATION);
}

// Compare the selected file in the active pane with the first selected file in the other pane.
static void onMenuItemDiffClick(void) {
    struct Pane* cur = activePane();
    struct Pane* other = (activeIdx == 0) ? &panes[1] : &panes[0];
    int selCur = ListView_GetNextItem(cur->hwndList, -1, LVNI_SELECTED);
    int selOther = ListView_GetNextItem(other->hwndList, -1, LVNI_SELECTED);
    if (selCur < 0 || selOther < 0) return;
    wchar_t leftPath[MAX_PATH], rightPath[MAX_PATH];
    getFileNodePath(cur->items[selCur].node, leftPath);
    getFileNodePath(other->items[selOther].node, rightPath);
    diffShowDialog(hwndMain, leftPath, rightPath);
}

// ---------- Copy To / Move To ----------
static void copyOrMoveTo(bool isMove) {
    updateSelectedItems();
    if(numSelectedItems==0)return;
    BROWSEINFOW bi={0}; bi.hwndOwner=hwndMain; bi.lpszTitle=isMove?lc_str.move_to:lc_str.copy_to;
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl=SHBrowseForFolderW(&bi);
    if(!pidl)return;
    wchar_t dst[MAX_PATH]={0}; SHGetPathFromIDListW(pidl,dst); CoTaskMemFree(pidl);
    if(!dst[0])return;
    int i;
    for(i=0;i<numSelectedItems;i++){
        wchar_t src[MAX_PATH]={0}; getFileNodePath(selectedItems[i],src);
        wchar_t* nm=wcsrchr(src,L'\\'); nm=nm?nm+1:src;
        wchar_t target[MAX_PATH]; swprintf_s(target,MAX_PATH,L"%ls\\%ls",dst,nm);
        if(isMove) MoveFileW(src,target);
        else CopyFileW(src,target,FALSE);
    }
    navigateRefresh();
}
static void onMenuItemCopyToClick(void){copyOrMoveTo(false);}
static void onMenuItemMoveToClick(void){copyOrMoveTo(true);}

// ---------- Add to Favorites ----------
static void onMenuItemAddToFavClick(void) {
    updateSelectedItems();
    if (numSelectedItems == 0) return;
    int added = 0;
    for (int i = 0; i < numSelectedItems; i++) {
        wchar_t path[MAX_PATH] = {0};
        getFileNodePath(selectedItems[i], path);
        if (path[0] && favAdd(path)) added++;
    }
    if (added > 0) favRefreshTree();
}
