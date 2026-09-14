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

// 每面板状态。两个列表视图同时活动（各自触发自己的

// 重绘时的 LVN_GETDISPINFO），因此后备数据必须能按 HWND 解析。

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
static void showIconInspector(const wchar_t* filePath);
static void onMenuItemManageAssocClick(void);
static void faGetFileExt(const wchar_t* path, wchar_t* outExt, int maxLen);
static bool faGetAssociation(const wchar_t* ext, wchar_t* outExe, int maxLen);
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
void onMenuItemRunAdaptiveWClick(void);
void onMenuItemRunAdaptiveFClick(void);
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
static struct ContextMenuItem cmiManageAssoc = {NULL, &onMenuItemManageAssocClick, NULL};
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
// 启动器（RamBooster 风格）：释放内存后启动目标，可选择通过外部启动器 exe。

// 启动器 / 启动参数菜单处理函数的前向声明（在下面定义）。

void onMenuItemLauncherBoostAggressiveClick(void);
void onMenuItemRunDX11Click(void);
void onMenuItemRunD3D9Click(void);
void onMenuItemRunNoDebugClick(void);
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
static struct ContextMenuItem cmiRunAdaptiveW = {NULL, &onMenuItemRunAdaptiveWClick, NULL};
static struct ContextMenuItem cmiRunAdaptiveF = {NULL, &onMenuItemRunAdaptiveFClick, NULL};
static struct ContextMenuItem cmiRunCustom = {NULL, &onMenuItemRunCustomClick, NULL};
static struct ContextMenuItem cmiHashSHA1 = {NULL, &onMenuItemHashSHA1Click, NULL};
static struct ContextMenuItem cmiHashSHA256 = {NULL, &onMenuItemHashSHA256Click, NULL};
static struct ContextMenuItem cmiLauncherRunWith = {NULL, &onMenuItemLauncherRunWithClick, NULL};
static struct ContextMenuItem cmiLauncherChoose = {NULL, &onMenuItemLauncherChooseClick, NULL};
static struct ContextMenuItem cmiDiff = {NULL, &onMenuItemDiffClick, NULL};

static WNDPROC OrigWndProc;

// OLE 拖放状态

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

// 追加一个新的、单独分配的右键菜单项。返回稳定指针

// （而非 &menuItems[i]）使菜单 dwItemData 在后续重分配中保持有效。

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

// 前向声明（在本文件后面 / main.c 中定义）

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

// 点击了某个面板的路径栏 -> 将该面板设为活动面板。如果 h 是标签则返回 true。

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
// 解析图标 + 类型名通过 SHGetFileInfo，这是完整的 shell/注册表查询

// Wine。没有缓存，一个包含 200 个 .txt 文件的文件夹会进行 200 次相同查询，每次

// 重新进入文件夹时。这些缓存使每个不同扩展名只花费一次查询，持久化

// 跨导航持久化。它们被有意从不清除。系统图标列表索引是

// 大图和小图视图相同，因此缓存的索引无论视图样式如何都有效。


static int folderIconCached = 0;
static int folderIconIndex = 0;

#define EXT_ICON_CACHE_SIZE 256
static struct { wchar_t ext[24]; int icon; wchar_t typeName[64]; } extIconCache[EXT_ICON_CACHE_SIZE];
static int extIconCacheCount = 0;

// .exe/.lnk 携带每个文件的内嵌图标，因此不能共享扩展条目——按路径缓存。

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

// 大小 + 修改时间在枚举期间（buildChildNodes）直接从

// WIN32_FIND_DATA，因此填充列表项现在是纯复制——无需每文件 GetFileAttributesEx。

// 这是大文件夹的关键胜利：5k 文件的文件夹不再进行 5k 次同步 stat 往返

// 在加载时通过 Wine 到 FUSE 存储。

static void fillFileInfo(struct FileNode* node, struct ListItem* item) {
    item->size = node->size;
    memcpy(&item->modifiedTime, &node->modifiedTime, sizeof(FILETIME));
}

static void updateStatusbar(struct Pane* p) {
    if (p != activePane()) return;
    wchar_t sizeStr[32] = {0};
    formatFileSize(p->totalSize, sizeStr);

    // 内存使用

    wchar_t memStr[48] = {0};
    MEMORYSTATUSEX msx = {0};
    msx.dwLength = sizeof(msx);
    if (GlobalMemoryStatusEx(&msx)) {
        wchar_t used[16], total[16];
        formatFileSize(msx.ullTotalPhys - msx.ullAvailPhys, used);
        formatFileSize(msx.ullTotalPhys, total);
        swprintf_s(memStr, 48, L"  |  %ls: %ls/%ls", lc_str.memory, used, total);
    }

    // 当前驱动器的可用空间

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

    // 四段式状态栏：项目数 | 大小 | 内存 | 可用空间

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
            // 先让 ListView 更新选择，然后启动拖动检测

            OrigWndProc(hwnd, msg, wParam, lParam);
            dragStartPt.x = (short)LOWORD(lParam);
            dragStartPt.y = (short)HIWORD(lParam);
            dragPending = true;
            updateSelectedItems();
            return 0;
        }
        case WM_MOUSEMOVE: {
            // 行高亮的悬停跟踪

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
            // 拖动检测

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
    // Wine 将列表视图自身的滚动条绘为浅色；在上面重绘为深色。

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

// 公共：填充活动面板中第一个选中项的路径/类型。

// 由预览面板使用。如果没有选中任何内容则返回且不修改输出。

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

// 从已注册的 "shell\open\command" 值中提取可执行文件路径，例如

// 示例："C:\Program Files\App\app.exe" "%1"   ->   C:\Program Files\App\app.exe
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

// “打开方式”子菜单：尽力列出已注册的应用程序（HKCR\Applications）

// 加上一个“选择其他程序...”条目，打开 Wine 的打开方式对话框。在以下情况时健壮

// 注册表列表稀疏（仍提供对话框）。

static void createOpenWithMenu(int* id) {
    wchar_t filePath[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], filePath);

    HMENU hSubmenu = CreatePopupMenu();
    int count = 0;

    // ===== 顶部：内部关联程序（原创功能）=====
    wchar_t fileExt[32] = {0};
    faGetFileExt(filePath, fileExt, 32);
    wchar_t assocExe[MAX_PATH] = {0};
    if (fileExt[0] && faGetAssociation(fileExt, assocExe, MAX_PATH)) {
        wchar_t* exeName = wcsrchr(assocExe, L'\\');
        exeName = exeName ? exeName + 1 : assocExe;
        wchar_t menuText[256];
        swprintf_s(menuText, 256, L"用 %ls 打开", exeName);
        struct ContextMenuItem* cmItem = addMenuItemSlot();
        cmItem->text = wcsdup(menuText);
        cmItem->proc = NULL;
        cmItem->cmdData = NULL;
        cmItem->openExe = wcsdup(assocExe);
        cmItem->openFile = wcsdup(filePath);
        addContextMenuItem(hSubmenu, (*id)++, cmItem, false);
        count++;
        // 分隔线
        MENUITEMINFO sep = {0};
        sep.cbSize = sizeof(MENUITEMINFO);
        sep.fMask = MIIM_TYPE;
        sep.fType = MFT_SEPARATOR;
        InsertMenuItem(hSubmenu, -1, TRUE, &sep);
    }

    // ===== 中部：系统已注册程序（原有功能）=====
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
    // 将外部启动器操作合并到此子菜单，使每个“通过

    // “其他程序”选项集中在一处，避免重复菜单项。

    {
        wchar_t savedLauncher[MAX_PATH] = {0};
        if (launcherGetSaved(savedLauncher))
            addContextMenuItem(hSubmenu, (*id)++, &cmiLauncherRunWith, false);
        addContextMenuItem(hSubmenu, (*id)++, &cmiLauncherChoose, count > 0);
    }
    addContextMenuItem(hSubmenu, (*id)++, &cmiChooseProgram, false);
    // 底部：管理文件关联（原创功能）
    addContextMenuItem(hSubmenu, (*id)++, &cmiManageAssoc, false);

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
                // 压缩包解压（7z）。仅对识别的压缩包类型显示。

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
                    // “带参数运行”子菜单：通用预设 + 自定义输入。

                    // 无引擎标签——参数可跨引擎使用，其中

                    // 支持；自定义输入涵盖其他所有内容。

                    HMENU hArgs = CreatePopupMenu();
                    addContextMenuItem(hArgs, id++, &cmiRunCustom, true);
                    addContextMenuItem(hArgs, id++, &cmiRunAdaptiveW, false);
                    addContextMenuItem(hArgs, id++, &cmiRunAdaptiveF, false);
                    addContextMenuItem(hArgs, id++, &cmiRunDX11, false);
                    addContextMenuItem(hArgs, id++, &cmiRunD3D9, false);
                    addContextMenuItem(hArgs, id++, &cmiRunNoDebug, true);
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
            // 自绘仅用于报表/详细信息视图。图标/列表视图使用默认渲染。

            if (p->viewStyle != STYLE_DETAILS) return CDRF_DODEFAULT;
            if (lpcd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (lpcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                HDC hdc = lpcd->nmcd.hdc;
                int itemIdx = (int)lpcd->nmcd.dwItemSpec;
                if (itemIdx < 0 || itemIdx >= p->numItems) break;
                struct ListItem* item = &p->items[itemIdx];
                RECT rc = lpcd->nmcd.rc;
                int rowH = rc.bottom - rc.top;

                // 必要时强制加载（LVS_OWNERDATA 可能在 getdispinfo 之前调用 customdraw）

                if (!item->loaded) fillFileInfo(item->node, item);

                BOOL selected = (ListView_GetItemState(p->hwndList, itemIdx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                BOOL hovered = (itemIdx == hoveredItem);

                // 使用系统颜色以正确适配深色/浅色主题

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
                        // 悬停：将高亮颜色与窗口背景混合（30% 高亮）

                        COLORREF hl = GetSysColor(COLOR_HIGHLIGHT);
                        bgColor = RGB(
                            (GetRValue(winBg)*7 + GetRValue(hl)*3)/10,
                            (GetGValue(winBg)*7 + GetGValue(hl)*3)/10,
                            (GetBValue(winBg)*7 + GetBValue(hl)*3)/10);
                    } else if (itemIdx % 2 == 1) {
                        // 交替行：比窗口背景稍深/稍浅

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
                    // Windows 资源管理器风格的容量表：健康时蓝色，

                    // 仅在可用空间确实很低（<10%）时显示红色。

                    double pct = (totalGB > 0) ? usedGB / totalGB : 0;
                    if (pct > 1.0) pct = 1.0;
                    COLORREF barColor = (pct < 0.90) ? RGB(0,120,215) : RGB(210,50,50);
                    // 容量条占大小列约 42%；其余部分显示文本。

                    int barX = sizeX + 4;
                    int barW = (w2 - 8) * 42 / 100;
                    if (barW < 46) barW = 46;
                    int barY = rc.top + (rowH - 12) / 2;
                    int barH = 12;
                    // 轨道（内凹槽）+ 1px 边框。

                    RECT trackR = {barX, barY, barX + barW, barY + barH};
                    HBRUSH trackBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
                    FillRect(hdc, &trackR, trackBrush);
                    DeleteObject(trackBrush);
                    HPEN borderPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
                    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
                    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, barX, barY, barX + barW, barY + barH);
                    SelectObject(hdc, oldBrush);
                    SelectObject(hdc, oldPen);
                    DeleteObject(borderPen);
                    // 已填充部分（在 1px 边框内）。

                    int fillW = (int)((barW - 2) * pct);
                    if (fillW > 0) {
                        HBRUSH fillBrush = CreateSolidBrush(barColor);
                        RECT fillR = {barX + 1, barY + 1, barX + 1 + fillW, barY + barH - 1};
                        FillRect(hdc, &fillR, fillBrush);
                        DeleteObject(fillBrush);
                    }
                    // 容量条上的百分比（白色）。

                    wchar_t pctText[16];
                    swprintf_s(pctText, 16, L"%d%%", (int)(pct * 100));
                    SetTextColor(hdc, RGB(255,255,255));
                    SetBkMode(hdc, TRANSPARENT);
                    RECT pctR = {barX + 1, barY + 1, barX + barW - 1, barY + barH - 1};
                    DrawTextW(hdc, pctText, -1, &pctR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    // 容量条右侧显示完整的 已用/总 GB。

                    wchar_t capText[40];
                    swprintf_s(capText, 40, L"%.1f / %.0f GB", usedGB, totalGB);
                    SetTextColor(hdc, textColor);
                    RECT textR = {barX + barW + 6, rc.top, sizeX + w2 - 2, rc.bottom};
                    DrawTextW(hdc, capText, -1, &textR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
                    // 每个文件夹共享一个系统图标——只解析一次。

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
                        // exe/lnk 携带每文件图标：按完整路径缓存，而非扩展名。

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
                        // 其他所有内容按扩展名索引（每个不同扩展名一次 shell 查询）。

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
                    // 驱动器 / 桌面 / 电脑 / 个人——只有少数几个，不值得缓存。

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
                            // 在图标/列表视图中，内联显示驱动器号和容量

                            static wchar_t driveLabel[48];
                            wchar_t dp[MAX_PATH] = {0};
                            getFileNodePath(item->node, dp);
                            if (wcslen(dp) == 2 && dp[1] == L':') wcscat_s(dp, MAX_PATH, L"\\");
                            ULARGE_INTEGER fb, tb, tf;
                            if (GetDiskFreeSpaceExW(dp, &fb, &tb, &tf) && tb.QuadPart > 0) {
                                double usedGB = (double)(tb.QuadPart - fb.QuadPart) / 1073741824.0;
                                double totalGB = (double)tb.QuadPart / 1073741824.0;
                                swprintf_s(driveLabel, 48, L"%ls  %.1f/%.0f GB", item->node->name, usedGB, totalGB);
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
                            // 驱动器节点名称是不带尾斜杠的 "C:"；GetDiskFreeSpaceExW 需要 "C:\"

                            if (wcslen(dp) == 2 && dp[1] == L':') wcscat_s(dp, MAX_PATH, L"\\");
                            ULARGE_INTEGER fb, tb, tf;
                            if (GetDiskFreeSpaceExW(dp, &fb, &tb, &tf)) {
                                double usedGB = (double)(tb.QuadPart - fb.QuadPart) / 1073741824.0;
                                double totalGB = (double)tb.QuadPart / 1073741824.0;
                                swprintf_s(capStr, 32, L"%.1f/%.0f GB", usedGB, totalGB);
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
                // 资源管理器行为：右键单击不属于以下内容的项目时

                // 当前选择将选择移动到命中项

                // 首先。没有这个，updateSelectedItems() 会保留之前的

                // 选择，因此对文件 B 的“加速运行”可能启动之前的

                // 已选择程序 A。在已有的多选区域内右键单击

                // 选择会保留该选择。

                UINT hitState = ListView_GetItemState(nmhdr->hwndFrom, nmia->iItem, LVIS_SELECTED);
                if (!(hitState & LVIS_SELECTED)) {
                    ListView_SetItemState(nmhdr->hwndFrom, -1, 0, LVIS_SELECTED);
                    ListView_SetItemState(nmhdr->hwndFrom, nmia->iItem,
                                          LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                }
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
    // 将视图样式持久化到注册表（重启后保留）

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

    // 列宽经过调整，确保大小列有足够空间显示完整容量条

    // 加上“已用/总 GB”文本（不截断）。名称列是灵活的。

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

// 列表视图列标题（SysHeader32）在深色下渲染为浅色带

// 容器主题，导致标题不可读。改为自绘深色。

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

// 调整列宽以适应面板，确保四列永不溢出（无水平滚动条）：

// 名称列吸收剩余宽度。

void cvFitColumns(HWND list, int totalWidth) {
    int typeW = 96, sizeW = 196, dateW = 120;
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
    // 注册为 OLE 放置目标（接受从 Linux 文件管理器 / 其他应用拖入的文件）

    if (!g_dropTarget) g_dropTarget = createDropTarget();
    if (g_dropTarget) RegisterDragDrop(hwnd, g_dropTarget);
    // 现代列表行为：整行选择、无闪烁滚动、整洁标签提示。

    // 注意：不要在这里调用 SetWindowTheme("Explorer")——在深色容器主题下它会强制

    // 浅色标题栏导致列标题不可读。

    ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    createLVColumns(hwnd);

    // 自绘列标题为深色（见 HeaderWndProc）。两个列表视图共享

    // 相同的标题类，因此只捕获一次原始过程。

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
    cmiManageAssoc.text = L"管理文件关联...";
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
    cmiRunAdaptiveW.text = lc_str.adaptive_windowed;
    cmiRunAdaptiveF.text = lc_str.adaptive_fullscreen;
    cmiRunCustom.text = lc_str.arg_custom;
    cmiHashSHA1.text = lc_str.hash_sha1;
    cmiHashSHA256.text = lc_str.hash_sha256;
    cmiLauncherRunWith.text = lc_str.launcher_run_with;
    cmiLauncherChoose.text = lc_str.launcher_choose;
    cmiDiff.text = lc_str.diff_files;

    // 从注册表恢复已保存的视图样式

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

    // 面板 1 + 两个路径栏开始时隐藏，直到启用分屏视图。

    ShowWindow(panes[1].hwndList, SW_HIDE);
    ShowWindow(panes[0].hwndPathLabel, SW_HIDE);
    ShowWindow(panes[1].hwndPathLabel, SW_HIDE);
    activeIdx = 0;
}

// 为每个面板提供独立的路径链。在 initFileNodes() 之后调用

// （这使全局 currPathFileNode 指向电脑）。

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

    // 将活动面板的活动路径交换到其槽位，引入新路径。

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
        // 填充面板 1（它在隐藏时从未刷新）。

        buildChildNodes(panes[1].currPath, false);
        refreshPane(&panes[1]);
        updatePaneLabel(&panes[0]);
    }
    else {
        // 折叠：将面板 0 设为活动并隐藏面板 1。

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

// 双面板同步：当活动面板导航到子目录时，

// 如果另一面板中存在同名子目录，则在该面板中镜像导航。

void cvSyncOtherPane(const wchar_t* targetName) {
    if (!splitOn || !targetName || !targetName[0]) return;
    struct Pane* other = (activeIdx == 0) ? &panes[1] : &panes[0];
    if (!other->currPath) return;
    // 在另一面板的子项中搜索匹配的目录名。

    for (int i = 0; i < other->numItems; i++) {
        if (other->items[i].node->type == TYPE_DIR &&
            wcscmp(other->items[i].node->name, targetName) == 0) {
            // 将另一面板导航到匹配的子目录。

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
            // 运行时本地化所有标签（RC 文件保持英文以避免编码问题）

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
        // "runas" 动词请求提升权限。Wine 的提升权限主要是装饰性的，但

        // 依赖动词 / 提升标志的应用能得到它们期望的结果。

        ShellExecuteW(hwndMain, L"runas", path, NULL, parentPath, SW_SHOW);
    }
}

// ============================================================================
// 内部文件关联管理器（原创设计）
// 配置保存在 BFM 自己的注册表，不修改 Wine 全局文件关联
// 注册表路径：HKCU\Software\Winlator\WFM\FileAssociations\<.ext> = 程序路径
// ============================================================================

#define FA_REG_ROOT L"Software\\Winlator\\WFM\\FileAssociations"

// 从文件路径获取扩展名（含点，如 ".txt"）
static void faGetFileExt(const wchar_t* path, wchar_t* outExt, int maxLen) {
    outExt[0] = L'\0';
    if (!path || !path[0]) return;
    const wchar_t* dot = wcsrchr(path, L'.');
    const wchar_t* slash = wcsrchr(path, L'\\');
    if (dot && (!slash || dot > slash)) {
        wcsncpy_s(outExt, maxLen, dot, _TRUNCATE);
        for (wchar_t* p = outExt; *p; p++) *p = towlower(*p);
    }
}

// 获取文件类型关联的程序路径，返回是否找到
static bool faGetAssociation(const wchar_t* ext, wchar_t* outExe, int maxLen) {
    outExe[0] = L'\0';
    if (!ext || !ext[0]) return false;
    HKEY hKey;
    wchar_t subKey[512];
    swprintf_s(subKey, 512, L"%ls\\%ls", FA_REG_ROOT, ext);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD cb = (DWORD)(maxLen * sizeof(wchar_t));
    LRESULT r = RegQueryValueExW(hKey, L"Program", NULL, NULL, (LPBYTE)outExe, &cb);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS || !outExe[0]) return false;
    return isPathExists(outExe);
}

// 设置文件类型关联
static bool faSetAssociation(const wchar_t* ext, const wchar_t* exePath) {
    if (!ext || !ext[0] || !exePath || !exePath[0]) return false;
    HKEY hKey;
    wchar_t subKey[512];
    swprintf_s(subKey, 512, L"%ls\\%ls", FA_REG_ROOT, ext);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    LRESULT r = RegSetValueExW(hKey, L"Program", 0, REG_SZ, (const BYTE*)exePath, (wcslen(exePath) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

// 删除文件类型关联
static bool faRemoveAssociation(const wchar_t* ext) {
    if (!ext || !ext[0]) return false;
    wchar_t subKey[512];
    swprintf_s(subKey, 512, L"%ls\\%ls", FA_REG_ROOT, ext);
    return RegDeleteKeyW(HKEY_CURRENT_USER, subKey) == ERROR_SUCCESS;
}

// 用关联程序打开文件
static void faOpenWithAssociation(const wchar_t* filePath, const wchar_t* exePath) {
    wchar_t params[MAX_PATH + 8] = {0};
    swprintf_s(params, MAX_PATH + 8, L"\"%ls\"", filePath);
    wchar_t workDir[MAX_PATH] = {0};
    getParentDirFromPath(exePath, workDir);
    ShellExecuteW(hwndMain, L"open", exePath, params, workDir[0] ? workDir : NULL, SW_SHOW);
}

// 文件关联管理器窗口状态
typedef struct {
    HWND hwnd;
    HWND hwndList;
    HWND hwndPathLabel;
    HWND hwndBrowse;
    HWND hwndSet;
    HWND hwndRemove;
    HWND hwndClose;
    wchar_t selectedExt[32];
} FaManagerState;

static FaManagerState* g_faManager = NULL;

// 刷新关联列表
static void faManagerRefreshList(FaManagerState* s) {
    if (!s || !s->hwndList) return;
    SendMessageW(s->hwndList, LB_RESETCONTENT, 0, 0);
    HKEY hRoot;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, FA_REG_ROOT, 0, KEY_READ, &hRoot) != ERROR_SUCCESS) return;
    wchar_t extName[64];
    DWORD i = 0, len;
    while (1) {
        len = 64;
        if (RegEnumKeyExW(hRoot, i++, extName, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        wchar_t exePath[MAX_PATH] = {0};
        if (faGetAssociation(extName, exePath, MAX_PATH)) {
            wchar_t* exeName = wcsrchr(exePath, L'\\');
            exeName = exeName ? exeName + 1 : exePath;
            wchar_t item[512];
            swprintf_s(item, 512, L"%ls  →  %ls", extName, exeName);
            int idx = (int)SendMessageW(s->hwndList, LB_ADDSTRING, 0, (LPARAM)item);
            SendMessageW(s->hwndList, LB_SETITEMDATA, idx, (LPARAM)wcsdup(extName));
        }
    }
    RegCloseKey(hRoot);
}

// 文件关联管理器窗口过程
static LRESULT CALLBACK faManagerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FaManagerState* s = g_faManager;
    if (!s) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            HWND ctrl = (HWND)lParam;
            if (ctrl == s->hwndList && HIWORD(wParam) == LBN_SELCHANGE) {
                int sel = (int)SendMessageW(s->hwndList, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    wchar_t* ext = (wchar_t*)SendMessageW(s->hwndList, LB_GETITEMDATA, sel, 0);
                    if (ext) {
                        wcsncpy_s(s->selectedExt, 32, ext, _TRUNCATE);
                        wchar_t exePath[MAX_PATH] = {0};
                        if (faGetAssociation(ext, exePath, MAX_PATH)) {
                            SetWindowTextW(s->hwndPathLabel, exePath);
                        }
                    }
                }
            } else if (id == 1001) {  // 浏览
                OPENFILENAMEW ofn = {0};
                wchar_t exePath[MAX_PATH] = {0};
                ofn.lStructSize = sizeof(OPENFILENAMEW);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = exePath;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                ofn.lpstrTitle = L"选择程序";
                typedef BOOL (WINAPI *PFN_GetOpenFileNameW)(LPOPENFILENAMEW);
                HMODULE hCd = LoadLibraryW(L"comdlg32.dll");
                if (hCd) {
                    PFN_GetOpenFileNameW pfn = (PFN_GetOpenFileNameW)GetProcAddress(hCd, "GetOpenFileNameW");
                    if (pfn && pfn(&ofn)) {
                        SetWindowTextW(s->hwndPathLabel, exePath);
                    }
                    FreeLibrary(hCd);
                }
            } else if (id == 1002) {  // 设置/更新关联
                if (!s->selectedExt[0]) {
                    MessageBoxW(hwnd, L"请先在列表中选择一个文件类型，或点击添加新关联", L"提示", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                wchar_t exePath[MAX_PATH] = {0};
                GetWindowTextW(s->hwndPathLabel, exePath, MAX_PATH);
                if (!exePath[0] || !isPathExists(exePath)) {
                    MessageBoxW(hwnd, L"请先选择一个有效的程序文件", L"错误", MB_OK | MB_ICONERROR);
                    break;
                }
                if (faSetAssociation(s->selectedExt, exePath)) {
                    MessageBoxW(hwnd, L"关联已保存", L"成功", MB_OK | MB_ICONINFORMATION);
                    faManagerRefreshList(s);
                } else {
                    MessageBoxW(hwnd, L"保存失败", L"错误", MB_OK | MB_ICONERROR);
                }
            } else if (id == 1003) {  // 删除关联
                if (!s->selectedExt[0]) {
                    MessageBoxW(hwnd, L"请先选择要删除的文件类型", L"提示", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (MessageBoxW(hwnd, L"确定要删除该文件类型的关联吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    faRemoveAssociation(s->selectedExt);
                    s->selectedExt[0] = L'\0';
                    SetWindowTextW(s->hwndPathLabel, L"");
                    faManagerRefreshList(s);
                }
            } else if (id == 1004) {  // 添加新关联
                wchar_t* input = InputDialog(L"添加新文件关联", L"输入扩展名（如 .txt）：", L"", false);
                if (input && input[0]) {
                    wchar_t ext[32] = {0};
                    wcsncpy_s(ext, 32, input, _TRUNCATE);
                    for (wchar_t* p = ext; *p; p++) *p = towlower(*p);
                    if (ext[0] != L'.') {
                        wchar_t tmp[32];
                        swprintf_s(tmp, 32, L".%ls", ext);
                        wcsncpy_s(ext, 32, tmp, _TRUNCATE);
                    }
                    wcsncpy_s(s->selectedExt, 32, ext, _TRUNCATE);
                    SetWindowTextW(s->hwndPathLabel, L"");
                    faManagerRefreshList(s);
                    MessageBoxW(hwnd, L"已添加文件类型，请在右侧选择程序后点击保存", L"提示", MB_OK | MB_ICONINFORMATION);
                }
                if (input) free(input);
            } else if (id == 1005) {  // 关闭
                DestroyWindow(hwnd);
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            if (g_faManager) {
                free(g_faManager);
                g_faManager = NULL;
            }
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 显示文件关联管理器
static void showAssociationManager(void) {
    if (g_faManager) {
        SetForegroundWindow(g_faManager->hwnd);
        return;
    }
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = faManagerWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"BFM_AssocManager";
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }
    g_faManager = (FaManagerState*)calloc(1, sizeof(FaManagerState));
    if (!g_faManager) return;
    int winW = 520, winH = 420;
    g_faManager->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"BFM_AssocManager",
        L"文件关联管理器", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        hwndMain, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_faManager->hwnd) { free(g_faManager); g_faManager = NULL; return; }
    // 左侧列表
    CreateWindowW(L"STATIC", L"已关联的文件类型：", WS_CHILD | WS_VISIBLE,
        12, 10, 240, 20, g_faManager->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    g_faManager->hwndList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        12, 35, 240, 300, g_faManager->hwnd, (HMENU)1000, GetModuleHandleW(NULL), NULL);
    // 右侧
    CreateWindowW(L"STATIC", L"关联程序路径：", WS_CHILD | WS_VISIBLE,
        270, 10, 220, 20, g_faManager->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    g_faManager->hwndPathLabel = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE, 270, 35, 220, 25, g_faManager->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    g_faManager->hwndBrowse = CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        270, 70, 100, 28, g_faManager->hwnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    g_faManager->hwndSet = CreateWindowW(L"BUTTON", L"保存关联", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        380, 70, 110, 28, g_faManager->hwnd, (HMENU)1002, GetModuleHandleW(NULL), NULL);
    g_faManager->hwndRemove = CreateWindowW(L"BUTTON", L"删除选中", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        270, 108, 100, 28, g_faManager->hwnd, (HMENU)1003, GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"添加新关联", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        380, 108, 110, 28, g_faManager->hwnd, (HMENU)1004, GetModuleHandleW(NULL), NULL);
    // 说明文字
    CreateWindowW(L"STATIC", L"说明：\n1. 点击「添加新关联」输入扩展名\n2. 点击「浏览」选择程序\n3. 点击「保存关联」生效\n4. 关联仅在 BFM 内部生效，不修改系统全局设置",
        WS_CHILD | WS_VISIBLE, 270, 150, 220, 120, g_faManager->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    // 关闭按钮
    g_faManager->hwndClose = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        420, 345, 70, 28, g_faManager->hwnd, (HMENU)1005, GetModuleHandleW(NULL), NULL);
    faManagerRefreshList(g_faManager);
    ShowWindow(g_faManager->hwnd, SW_SHOW);
    UpdateWindow(g_faManager->hwnd);
}

// 右键菜单：打开文件关联管理器
static void onMenuItemManageAssocClick(void) {
    showAssociationManager();
}

void onMenuItemOpenWithClick() {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t filePath[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], filePath);
    // 浏览选择一个 .exe 来打开该文件（rundll32 OpenAs_RunDLL 在 Wine 下不可用）

    OPENFILENAMEW ofn = {0};
    wchar_t exePath[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hwndMain;
    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = exePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = lc_str.choose_program;
    // 动态加载 comdlg32，避免在链接行中添加 -lcomdlg32

    typedef BOOL (WINAPI *PFN_GetOpenFileNameW)(LPOPENFILENAMEW);
    HMODULE hCd = LoadLibraryW(L"comdlg32.dll");
    if (hCd) {
        PFN_GetOpenFileNameW pfn = (PFN_GetOpenFileNameW)GetProcAddress(hCd, "GetOpenFileNameW");
        if (pfn && pfn(&ofn)) {
            // 先用选中的程序打开文件
            faOpenWithAssociation(filePath, exePath);
            // 询问是否始终用此程序打开该类型文件
            wchar_t fileExt[32] = {0};
            faGetFileExt(filePath, fileExt, 32);
            if (fileExt[0]) {
                wchar_t* exeName = wcsrchr(exePath, L'\\');
                exeName = exeName ? exeName + 1 : exePath;
                wchar_t msg[512];
                swprintf_s(msg, 512, L"是否始终用 %ls 打开 %ls 类型的文件？\n\n（关联仅在 BFM 内部生效，不修改系统全局设置）", exeName, fileExt);
                if (MessageBoxW(hwndMain, msg, L"设置默认打开方式", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (faSetAssociation(fileExt, exePath)) {
                        MessageBoxW(hwndMain, L"文件关联已保存", L"成功", MB_OK | MB_ICONINFORMATION);
                    }
                }
            }
        }
        FreeLibrary(hCd);
    }
}

// ===================== Launcher (RamBooster-style) =====================
// 已保存的外部启动器 exe（可选）。未设置时，目标本身在以下之后启动

// 内存加速。存储在 HKCU\SOFTWARE\Winlator\WFM\LauncherPath（REG_SZ）下。

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

// 施加内存压力，让 Android 的 LMK（低内存杀手）在以下时机之前回收后台进程：

// 游戏启动。参数遵循 Noysz/RamBooster-Winlator v1.2.2（调优后的

// Winlator fork），而不是 5.5GB 上游单文件版本：8MB 块，

// 失败时减半，触摸每个真实页面，动态安全底线（保留 12% 的

// 物理内存空闲，使容器不会杀死自己），以及硬绝对

// 上限（均衡 1GB / 激进 1.5GB）——8GB 手机的 45% = 3.6GB 会

// 停滞约 13 秒，fork 通过设置上限明确修复了此问题。

// 模式 0 = 均衡（35%，上限 1GB）；模式 1 = 激进（45%，上限 1.5GB + 裁剪）。

// 内存加速启动（重写版 v2）：
// 提高内存分配上限（均衡50%/激进70%，移除固定GB上限），
// 增加块大小（均衡16MB/激进32MB）加快分配速度，
// 增加等待时间（均衡1.5s/激进2.5s）让LMK充分回收，
// 记录清理前后可用内存，清理后等待500ms让内存稳定再启动游戏。
// 安全底线：保留15%物理内存空闲，防止wine进程组自身被OOM杀死。

static SIZE_T g_memBefore = 0;
static SIZE_T g_memAfter = 0;

static void launcherBoostMemory(int mode) {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms) || ms.ullTotalPhys == 0) return;

    g_memBefore = ms.ullAvailPhys;

    // 均衡模式：50%物理内存；激进模式：70%物理内存
    // 移除固定GB上限，让大内存手机能真正触发LMK
    int targetPct = (mode == 1) ? 70 : 50;
    SIZE_T totalToAlloc = (SIZE_T)(ms.ullTotalPhys * targetPct / 100);

    // 安全底线：保留15%物理内存空闲（比之前的12%更保守）
    SIZE_T safetyFloor = (SIZE_T)(ms.ullTotalPhys * 15 / 100);

    SYSTEM_INFO si; GetSystemInfo(&si);
    SIZE_T pageStep = si.dwPageSize ? si.dwPageSize : 4096;

    // 增大块大小，加快分配速度（减少VirtualAlloc调用次数）
    SIZE_T bSize = (mode == 1) ? 32 * 1024 * 1024 : 16 * 1024 * 1024;
    SIZE_T maxBlocks = totalToAlloc / (1024 * 1024) + 8;
    void** blocks = (void**)malloc(sizeof(void*) * maxBlocks);
    if (!blocks) return;

    int count = 0;
    SIZE_T allocated = 0;
    while (allocated < totalToAlloc && count < (int)maxBlocks) {
        // 每2个块检查一次安全底线（更频繁检查，防止OOM）
        if (count % 2 == 0) {
            MEMORYSTATUSEX chk; chk.dwLength = sizeof(chk);
            if (GlobalMemoryStatusEx(&chk) && chk.ullAvailPhys < safetyFloor)
                break;
        }
        void* m = VirtualAlloc(NULL, bSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!m) {
            bSize /= 2;
            if (bSize < 4 * 1024 * 1024) break;
            continue;
        }
        // 触摸每个真实页面，使物理内存实际提交
        for (SIZE_T off = 0; off < bSize; off += pageStep)
            ((volatile char*)m)[off] = 1;
        blocks[count++] = m;
        allocated += bSize;
        Sleep(15);  // 缩短块间隔，加快整体分配速度
    }

    // 增加等待时间，让Android LMK有充分时间回收后台进程
    if (count > 0) Sleep((mode == 1) ? 2500 : 1500);

    // 释放所有分配的内存
    for (int i = 0; i < count; i++) VirtualFree(blocks[i], 0, MEM_RELEASE);
    free(blocks);

    // 裁剪WFM自身工作集（安全：游戏启动期间WFM处于空闲状态）
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    // 等待500ms让内存稳定，再记录清理后的可用内存
    Sleep(500);
    MEMORYSTATUSEX after;
    after.dwLength = sizeof(after);
    if (GlobalMemoryStatusEx(&after)) {
        g_memAfter = after.ullAvailPhys;
    }
}

struct LauncherArg {
    wchar_t target[MAX_PATH];
    wchar_t launcher[MAX_PATH];
    bool useExternal;   // true = run via external launcher; false = always run target
    int boostMode;      // 0 = balanced, 1 = aggressive, -1 = no boost
    wchar_t extraArgs[256];  // command-line args appended to the game (Unity/UE flags)
    bool useWineDesktop;     // true = wrap target in a Wine virtual desktop (generic windowed mode)
    int deskW, deskH;        // virtual desktop resolution (ignored unless useWineDesktop)
};

static DWORD WINAPI launcherThread(LPVOID param) {
    struct LauncherArg* a = (struct LauncherArg*)param;
    if (a->boostMode >= 0) {
        PostMessage(hwndMain, WM_USER_BOOST_START, 0, 0);
        launcherBoostMemory(a->boostMode);
        // 计算释放的内存量（MB），发送给主线程显示
        SIZE_T freed = 0;
        if (g_memAfter > g_memBefore) freed = (g_memAfter - g_memBefore) / (1024 * 1024);
        PostMessage(hwndMain, WM_USER_BOOST_RESULT, (WPARAM)freed, (LPARAM)a->boostMode);
        PostMessage(hwndMain, WM_USER_BOOST_DONE, 0, 0);
        // 清理后再等待300ms，确保内存完全稳定后再启动游戏
        Sleep(300);
    }

    // 工作目录必须是目标的文件夹（游戏加载同级文件

    // 相对于它们自己的 exe），绝不是启动器的文件夹。

    wchar_t targetDir[MAX_PATH] = {0};
    getParentDirFromPath(a->target, targetDir);

    bool useExternal = a->useExternal && a->launcher[0] && isPathExists(a->launcher);

    // 命令行。两种形式：

    //  1) 普通模式："<exe>" <额外参数>

    //  2) Wine 虚拟桌面（未知引擎的通用窗口模式）：

    //     explorer.exe /desktop=WFM-ADAPTIVE,WxH "<exe>" <额外参数>

    // /desktop 语法是 Wine 标准的“在桌面内运行”形式（这是

    // 这正是 Winlator 自身虚拟桌面选项内部使用的方式）。

    wchar_t cmdLine[MAX_PATH * 2 + 256] = {0};
    wchar_t* app;
    if (a->useWineDesktop) {
        app = L"C:\\windows\\system32\\explorer.exe";
        swprintf_s(cmdLine, _countof(cmdLine),
                   L"explorer.exe /desktop=WFM-ADAPTIVE,%dx%d \"%ls\" %ls",
                   a->deskW > 0 ? a->deskW : 1280,
                   a->deskH > 0 ? a->deskH : 720,
                   a->target, a->extraArgs);
    }
    else if (useExternal) {
        app = a->launcher;
        swprintf_s(cmdLine, _countof(cmdLine), L"\"%ls\" \"%ls\" %ls",
                   a->launcher, a->target, a->extraArgs);
    }
    else {
        app = a->target;
        swprintf_s(cmdLine, _countof(cmdLine), L"\"%ls\" %ls",
                   a->target, a->extraArgs);
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessW(app, cmdLine, NULL, NULL, FALSE, 0, NULL,
                             targetDir[0] ? targetDir : NULL, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        // 内存释放必须在游戏启动之前完成，这样释放的空间

        // 可用于游戏自身的分配突发。裁剪游戏的

        // 启动后裁剪工作集是无意义的（Wine 页面会被重新缺页载入

        // 立即（只会导致卡顿），因此我们在这里从不触碰它。

        CloseHandle(pi.hProcess);
    }
    else {
        // 非 PE 目标 / 基于关联打开的回退。

        ShellExecuteW(hwndMain, L"open", a->target, NULL,
                      targetDir[0] ? targetDir : NULL, SW_SHOW);
    }
    free(a);
    return 0;
}

// 右键菜单项：释放内存（均衡模式）后直接运行选中的文件。

// 这从不使用外部启动器——那是一个单独的显式操作。

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

// 激进加速：更强的内存压力 + 工作集裁剪，适用于以下游戏：

// 接近设备内存限制，在战斗/过场时有 OOM 杀死风险。

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

// 辅助函数：带额外命令行参数启动（不进行内存加速）。

// 这些镜像 Winlator 快捷方式的“执行参数”：直接传递给游戏。

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

// ============================================================================
// 自适应引擎检测与启动（窗口化 / 带分辨率的全屏）。

// 不使用带引擎标签的菜单项，而是用一个条目自动检测引擎

// 从 PE 中检测并应用该引擎的官方显示标志；未知

// 引擎回退到 Wine 虚拟桌面以实现通用窗口模式。

// ============================================================================
enum EngineKind { ENGINE_UNKNOWN = 0, ENGINE_UNITY = 1, ENGINE_UNREAL = 2 };

// 许多发布的游戏将引擎标记放在同级文件中，而不是在

// 小型启动器 exe（UnityPlayer.dll / GameAssembly.dll 位于 exe 旁边；

// Unreal 自带 Engine\\Binaries 目录树）。首先检查这些，以便小型 exe 不会
// 不会被误分类为“未知”而失去其正确的启动标志。

static enum EngineKind detectEngineBySiblings(const wchar_t* exePath) {
    wchar_t dir[MAX_PATH] = {0};
    getParentDirFromPath(exePath, dir);
    if (!dir[0]) return ENGINE_UNKNOWN;
    wchar_t probe[MAX_PATH * 2];
    // Unity：Mono 构建自带 UnityPlayer.dll；IL2CPP 构建自带 GameAssembly.dll

    // 加上一个 "<Game>_Data" 文件夹。任何一个都是决定性的。

    swprintf_s(probe, _countof(probe), L"%ls\\UnityPlayer.dll", dir);
    if (isPathExists(probe)) return ENGINE_UNITY;
    swprintf_s(probe, _countof(probe), L"%ls\\GameAssembly.dll", dir);
    if (isPathExists(probe)) return ENGINE_UNITY;
// Unreal：打包构建在 exe 旁边包含 Engine\\Binaries 目录树。
    swprintf_s(probe, _countof(probe), L"%ls\\Engine\\Binaries", dir);
    if (isPathExists(probe)) return ENGINE_UNREAL;
    return ENGINE_UNKNOWN;
}

// 扫描可执行文件的前 16 MiB 以查找引擎标记字符串，两者都作为

// 纯 ASCII 和 UTF-16LE（PE 字符串表通常存储宽字符串）。

// 缓冲区在堆上分配：1 MiB 读取块保持工作集较小，并且

// 避免了默认 1 MiB 线程栈上的 4 MiB 占用（之前会导致崩溃）。

static enum EngineKind detectGameEngine(const wchar_t* path) {
    enum EngineKind sib = detectEngineBySiblings(path);
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return sib;
    BYTE* buf = (BYTE*)malloc(1 * 1024 * 1024);
    if (!buf) { CloseHandle(hFile); return sib; }
    DWORD rd;
    ULONGLONG total = 0;
    int flags = (sib == ENGINE_UNITY) ? 1 : (sib == ENGINE_UNREAL) ? 2 : 0;
    while (ReadFile(hFile, buf, 1 * 1024 * 1024, &rd, NULL) && rd > 0) {
        total += rd;
        DWORD i;
        for (i = 0; i + 15 < rd; i++) {
            if (!(flags & 1)) {
                if (memcmp(buf + i, "GameAssembly.dll", 16) == 0 ||
                    memcmp(buf + i, "UnityPlayer.dll", 15) == 0 ||
                    memcmp(buf + i, "Unity", 5) == 0 ||
                    memcmp(buf + i, "U\0n\0i\0t\0y\0", 10) == 0)
                    flags |= 1;
            }
            if (!(flags & 2)) {
                if (memcmp(buf + i, "Unreal", 6) == 0 ||
                    memcmp(buf + i, "U\0n\0r\0e\0a\0l\0", 12) == 0)
                    flags |= 2;
            }
            if (flags == 3) break;
        }
        if (flags == 3 || total >= 16ULL * 1024 * 1024) break;
    }
    free(buf);
    CloseHandle(hFile);
    return flags == 2 ? ENGINE_UNREAL : (flags == 1 ? ENGINE_UNITY : ENGINE_UNKNOWN);
}

// 解析 "WxH"（如 "1280x720"）；0/0 表示原生分辨率。

static void parseResolution(const wchar_t* s, int* w, int* h) {
    *w = 0; *h = 0;
    if (!s || !*s) return;
    const wchar_t* x = wcschr(s, L'x');
    if (!x) x = wcschr(s, L'X');
    if (!x) return;
    *w = _wtoi(s);
    *h = _wtoi(x + 1);
    if (*w <= 0 || *h <= 0) { *w = 0; *h = 0; }
}

// Wine 虚拟桌面必须保持小于容器屏幕，否则

// 游戏窗口（大小为容器分辨率）会被裁剪 / 与以下内容重叠

// 容器自身的桌面。GetSystemMetrics 返回的容器分辨率单位为

// Wine；我们默认使用其 80%，并将任何用户输入钳制到最多 90%。

static void getSafeDesktopSize(int* w, int* h) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw <= 0) sw = 1280;
    if (sh <= 0) sh = 720;
    // 默认使用容器分辨率的65%（比之前的80%更小，确保虚拟桌面有边框）
    if (*w <= 0 || *h <= 0) { *w = sw * 65 / 100; *h = sh * 65 / 100; }
    // 最大限制为容器分辨率的80%（比之前的90%更保守）
    int maxW = sw * 80 / 100;
    int maxH = sh * 80 / 100;
    if (*w > maxW) *w = maxW;
    if (*h > maxH) *h = maxH;
    // 确保宽高至少为640x480
    if (*w < 640) *w = 640;
    if (*h < 480) *h = 480;
}

// 使用引擎自适应显示标志启动。fullscreen=false -> 窗口化。

// 自适应引擎检测与启动（窗口化 / 带分辨率的全屏）- 重写版 v2
// 窗口化模式：所有引擎统一使用Wine虚拟桌面（最可靠的窗口化方式）
// 全屏模式：Unity/Unreal使用官方命令行参数，未知引擎普通启动
// 同时支持自定义分辨率输入

static void launchAdaptive(bool fullscreen) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t path[MAX_PATH] = {0};
    getFileNodePath(selectedItems[0], path);
    enum EngineKind eng = detectGameEngine(path);

    // 获取安全的桌面分辨率（小于容器分辨率）
    wchar_t defaultRes[24];
    int dW = 0, dH = 0;
    getSafeDesktopSize(&dW, &dH);
    swprintf_s(defaultRes, _countof(defaultRes), L"%dx%d", dW, dH);

    // 弹出输入对话框，让用户确认/修改分辨率
    wchar_t* input = InputDialog(fullscreen ? lc_str.adaptive_fullscreen
                                            : lc_str.adaptive_windowed,
                                 lc_str.res_hint, defaultRes, false);
    int rw = 0, rh = 0;
    if (input) { parseResolution(input, &rw, &rh); free(input); }
    getSafeDesktopSize(&rw, &rh);  // 填充0值并钳制到容器大小以内

    if (!fullscreen) {
        // ===== 窗口化模式：所有引擎统一使用Wine虚拟桌面 =====
        // Wine虚拟桌面是最可靠的窗口化方式，不依赖游戏是否支持命令行参数
        struct LauncherArg* a = (struct LauncherArg*)calloc(1, sizeof(struct LauncherArg));
        if (!a) return;
        wcscpy_s(a->target, MAX_PATH, path);
        a->useExternal = false;
        a->boostMode = -1;  // 不进行内存加速
        a->useWineDesktop = true;
        a->deskW = rw;
        a->deskH = rh;
        // 对于Unity/Unreal引擎，同时附加官方显示参数（双保险）
        if (eng == ENGINE_UNITY) {
            swprintf_s(a->extraArgs, 256, L"-screen-fullscreen 0 -screen-width %d -screen-height %d", rw, rh);
        } else if (eng == ENGINE_UNREAL) {
            swprintf_s(a->extraArgs, 256, L"-windowed -ResX=%d -ResY=%d", rw, rh);
        }
        HANDLE h = CreateThread(NULL, 0, launcherThread, a, 0, NULL);
        if (h) CloseHandle(h); else free(a);
    } else {
        // ===== 全屏模式 =====
        if (eng == ENGINE_UNITY) {
            // Unity全屏：使用官方参数
            wchar_t args[256] = {0};
            wcscpy_s(args, 256, L"-screen-fullscreen 1");
            if (rw > 0 && rh > 0) {
                wchar_t res[64];
                swprintf_s(res, 64, L" -screen-width %d -screen-height %d", rw, rh);
                wcscat_s(args, 256, res);
            }
            launchWithArgs(args);
        } else if (eng == ENGINE_UNREAL) {
            // Unreal全屏：使用官方参数
            wchar_t args[256] = {0};
            wcscpy_s(args, 256, L"-fullscreen");
            if (rw > 0 && rh > 0) {
                wchar_t res[64];
                swprintf_s(res, 64, L" -ResX=%d -ResY=%d", rw, rh);
                wcscat_s(args, 256, res);
            }
            launchWithArgs(args);
        } else {
            // 未知引擎全屏：普通启动（原生行为）
            launchWithArgs(L"");
        }
    }
}

void onMenuItemRunAdaptiveWClick(void) { launchAdaptive(false); }
void onMenuItemRunAdaptiveFClick(void) { launchAdaptive(true); }

// 自定义：提示用户输入任意命令行参数（如 -screen-width 1920

// -screen-height 1080 用于自定义分辨率，或任何引擎特定标志）。

void onMenuItemRunCustomClick(void) {
    if (numSelectedItems != 1 || selectedItems[0]->type != TYPE_FILE) return;
    wchar_t* input = InputDialog(lc_str.arg_custom,
        lc_str.res_hint, L"", false);
    if (input && input[0]) {
        launchWithArgs(input);
        free(input);
    }
}

// ============================================================================
// 文件哈希：通过 CryptoAPI 计算 SHA1/SHA256，复制到剪贴板。

// （MD5 使用现有的 computeFileMD5 辅助函数。）

// ============================================================================
static void copyFileHash(const wchar_t* path, ALG_ID algId, const wchar_t* algName) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hwndMain, lc_str.err_open_file, algName, MB_OK | MB_ICONERROR);
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

    // 复制到剪贴板。

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
    swprintf_s(msg, 300, lc_str.hash_copied_fmt, algName, hex);
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
// 进程管理器：通过 Toolhelp32 列出运行中的进程，允许终止。

// 带列表框和“终止”按钮的简单模态对话框。

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
    // 在内存中构建简单的对话框模板：列表框 + 3 个按钮。

    // 使用轻量级方法：手动创建弹出窗口。

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", lc_str.process_manager,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 380, hwndMain, NULL, globalHInstance, NULL);
    if (!hwnd) return;
    // 列表框

    CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        10, 10, 390, 290, hwnd, (HMENU)1001, globalHInstance, NULL);
    // 按钮

    CreateWindowExW(0, L"BUTTON", lc_str.proc_kill, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 310, 100, 30, hwnd, (HMENU)1002, globalHInstance, NULL);
    CreateWindowExW(0, L"BUTTON", lc_str.proc_refresh, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 310, 80, 30, hwnd, (HMENU)1003, globalHInstance, NULL);
    CreateWindowExW(0, L"BUTTON", lc_str.proc_close, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        300, 310, 100, 30, hwnd, (HMENU)IDOK, globalHInstance, NULL);

    hProcDlg = hwnd;
    hProcList = GetDlgItem(hwnd, 1001);
    refreshProcessList();

    // 模态消息循环。

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

// 右键菜单项：释放内存后通过已保存的外部启动器启动目标

// 启动器（接收目标路径作为第一个参数）。仅在以下情况显示

// 当配置了启动器时。

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

// 右键菜单项：选择外部启动器 exe（如 RamBooster）并记住它。

void onMenuItemLauncherChooseClick(void) {
    // 如果已配置启动器，让用户替换或清除它。

    // 指向另一个 exe 的过期启动器就是导致“加速运行”打开错误程序的原因

    // 错误的程序，因此必须能够在不编辑注册表的情况下清除。

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

    // 动态加载 comdlg32（与 onMenuItemOpenWithClick 一致，避免额外链接库）。

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

static void onMenuItemNewTxtClick() { createFileWithExt(lc_str.new_txt_name, NULL); }
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

    // X: 必须是在 winecfg 中配置的真实光盘驱动器（路径：../drive_x，类型：cdrom）。

    // 虚拟目录映射无法被游戏识别为光盘驱动器。

    if (GetFileAttributesW(L"X:\\") == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwndMain, lc_str.x_drive_missing, lc_str.alert, MB_OK | MB_ICONWARNING);
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
        // 游戏模式：仅显示文件夹和 .exe

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
    // 保持活动面板的存储路径与全局光标同步，然后刷新它。

    activePane()->currPath = currPathFileNode;
    refreshPane(activePane());
}


// 运行时语言切换后刷新列标题和状态栏

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

// 将选中文件的完整路径复制到剪贴板

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

// 在当前目录打开 cmd.exe

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
        MessageBoxW(hwndMain, lc_str.err_7z_missing, L"7z", MB_OK | MB_ICONERROR);
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
    // 输出文件夹 = 当前目录\basename（不含扩展名）。

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
// OLE 拖放：将文件拖出到其他应用（AlphaRom 等），接受文件拖入

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
    // 使用 GetAsyncKeyState 作为回退：Wine 可能不会可靠地更新 grfKey

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
// CF_HDROP 的简单 IEnumFORMATETC

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

// 从选中的文件路径构建 HDROP 全局内存块

static HGLOBAL buildHDropFromSelection(void) {
    // 收集路径

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
    // GMEM_DDESHARE 使内存块跨进程可见（在以下情况时需要

    // 在 Wine 下通过 WM_DROPFILES 将相同的 HDROP 传递给另一个应用）。

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

// 开始拖动选中的文件

// 回退：当目标拒绝 OLE 拖放时（在 Wine/X11 下常见），

// 将文件传递给光标下的窗口。经典 Win32 程序

// （如破解/补丁工具）通过 WM_DROPFILES 读取拖放并忽略命令

// 行参数，因此我们向已运行的窗口发送共享 HDROP；我们

// 仅在拖放到桌面/任务栏时使用 ShellExecute（无应用窗口）。

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
        // 真实应用窗口已打开：向它发送 WM_DROPFILES。

        // 仅发送到顶层窗口（DragAcceptFiles 在那里注册）

        // 并使用定时发送，这样挂起/易崩溃的目标不会冻结我们。

        HGLOBAL hDrop = buildHDropFromSelection();
        if (hDrop) {
            LRESULT result = 0;
            SendMessageTimeoutW(top, WM_DROPFILES, (WPARAM)hDrop, 0,
                                SMTO_ABORTIFHUNG, 2000, (PDWORD_PTR)&result);
            // 成功时所有权转移给接收者；如果目标没有

            // 不处理它（没有 DragAcceptFiles），我们必须自己释放它。

            // 我们无法可靠地检测处理，因此泄漏小块而不是

            // 而非双重释放。这对于一次性拖放是可接受的。

            return;
        }
    }

    // Shell/桌面或分配失败：回退到启动程序。

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
    // 如果没有 OLE 目标接受拖放（Wine/X11 跨进程限制），

    // 回退到用光标下的任何程序打开文件。

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
// 解析屏幕点位于哪个列表面板上。WindowFromPoint 不可靠

// 在 DoDragDrop 模态循环期间（鼠标捕获 / 子控件），因此首先

// 对每个面板的窗口矩形进行命中测试，然后回退到父窗口遍历。

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
    // 向上遍历父窗口直到找到我们的列表面板之一

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
        // 确定目标：检查是否拖放到文件夹项目上

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
            // 拖放到空白区域：使用目标面板的当前目录（支持双面板）

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
        // 刷新两个面板（拖放可能目标是非活动面板）

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
// 功能包：MD5、提取图标、导航历史、文本查看器、文件夹大小、

// 游戏模式、批量重命名、最近位置、比较面板、复制到/移动到

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
    // 当前在顶部，将其移到前进栈，导航到上一个

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
// PrivateExtractIcons 按请求的大小提取最接近的内嵌图标

// （最大 256px），远比 SHGetFileInfo 固定的 32px 大图标清晰。

// 该符号位于 user32 中；用 dllimport 声明以匹配 mingw

// 头文件自身的声明（普通重声明会触发 -Wattributes）。

__declspec(dllimport) UINT WINAPI PrivateExtractIconsW(LPCWSTR, int, int, int, HICON*, UINT*, UINT, UINT);

// 返回文件可用的最清晰图标及其真实像素大小。

// 对没有内嵌图标的文件回退到 shell 关联图标。

static HICON getBestFileIcon(const wchar_t* path, int* outW, int* outH) {
    HICON hIcon = NULL;
    static const int want[2] = { 256, 48 };
    for (int k = 0; k < 2 && !hIcon; k++) {
        HICON cand = NULL;
        UINT got = PrivateExtractIconsW(path, 0, want[k], want[k], &cand, NULL, 1, 0);
        // 0 / 0xFFFFFFFF 表示该尺寸下没有内嵌图标。

        if (got != 0 && got != 0xFFFFFFFFu && cand) hIcon = cand;
        else if (cand) DestroyIcon(cand);
    }
    bool fromShell = false;
    if (!hIcon) {
        SHFILEINFOW sfi = {0};
        if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi),
                           SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) {
            hIcon = sfi.hIcon;
            fromShell = true;
        }
    }
    int w = 32, h = 32;
    if (hIcon) {
        ICONINFO ii = {0};
        if (GetIconInfo(hIcon, &ii)) {
            BITMAP bm = {0};
            GetObject(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
            if (bm.bmWidth > 0) w = bm.bmWidth;
            if (bm.bmHeight > 0) {
                h = bm.bmHeight;
                // 仅掩码图标存储堆叠的 AND + XOR 掩码，因此高度翻倍。

                if (!ii.hbmColor) h /= 2;
            }
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
        }
    }
    (void)fromShell;
    if (w <= 0) w = 32;
    if (h <= 0) h = 32;
    *outW = w; *outH = h;
    return hIcon;
}

static void onMenuItemExtractIconClick(void) {
    updateSelectedItems();
    if(numSelectedItems!=1)return;
    wchar_t srcPath[MAX_PATH]={0}; getFileNodePath(selectedItems[0],srcPath);
    // 调用图标检查器（多尺寸浏览+透明棋盘格+ICO/BMP保存）
    showIconInspector(srcPath);
}

// ============================================================================
// 图标检查器（Icon Inspector）- 原创设计
// 多尺寸网格预览 + 透明棋盘格背景 + 点击选中 + ICO/BMP保存
// ============================================================================

#define ICON_INSPECTOR_SIZES 6
static const int g_iconSizes[ICON_INSPECTOR_SIZES] = {16, 32, 48, 64, 128, 256};

typedef struct {
    wchar_t filePath[MAX_PATH];
    HICON icons[ICON_INSPECTOR_SIZES];
    int iconRealW[ICON_INSPECTOR_SIZES];
    int iconRealH[ICON_INSPECTOR_SIZES];
    int selectedSize;  // index into g_iconSizes
    HWND hwnd;
    HWND hwndPreview;
    HWND hwndInfo;
    HWND hwndSaveIco;
    HWND hwndSaveBmp;
    HWND hwndClose;
} IconInspectorState;

static IconInspectorState* g_inspector = NULL;

// 绘制透明棋盘格背景（专业图标编辑器风格）
static void drawCheckerboard(HDC hdc, RECT* rect, int cellSize) {
    for (int y = rect->top; y < rect->bottom; y += cellSize) {
        for (int x = rect->left; x < rect->right; x += cellSize) {
            BOOL isLight = ((x / cellSize + y / cellSize) % 2 == 0);
            HBRUSH brush = CreateSolidBrush(isLight ? RGB(240, 240, 240) : RGB(200, 200, 200));
            RECT cell = {x, y, min(x + cellSize, rect->right), min(y + cellSize, rect->bottom)};
            FillRect(hdc, &cell, brush);
            DeleteObject(brush);
        }
    }
}

// 提取指定尺寸的图标，返回真实尺寸
static HICON extractIconAtSize(const wchar_t* path, int size, int* outW, int* outH) {
    HICON hIcon = NULL;
    HICON cand = NULL;
    UINT got = PrivateExtractIconsW(path, 0, size, size, &cand, NULL, 1, 0);
    if (got != 0 && got != 0xFFFFFFFFu && cand) {
        hIcon = cand;
    } else if (cand) {
        DestroyIcon(cand);
    }
    if (!hIcon) {
        SHFILEINFOW sfi = {0};
        if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) {
            hIcon = sfi.hIcon;
        }
    }
    int w = size, h = size;
    if (hIcon) {
        ICONINFO ii = {0};
        if (GetIconInfo(hIcon, &ii)) {
            BITMAP bm = {0};
            GetObject(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
            if (bm.bmWidth > 0) w = bm.bmWidth;
            if (bm.bmHeight > 0) {
                h = bm.bmHeight;
                if (!ii.hbmColor) h /= 2;
            }
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
        }
    }
    if (outW) *outW = w;
    if (outH) *outH = h;
    return hIcon;
}

// 保存为BMP（透明棋盘格背景，修复黑边问题）
static BOOL saveIconAsBmpCheckerboard(HICON hIcon, int w, int h, const wchar_t* outPath) {
    if (!hIcon || !outPath) return FALSE;
    HDC hdc = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP hDib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hDib) { DeleteDC(memDC); ReleaseDC(NULL, hdc); return FALSE; }
    HGDIOBJ old = SelectObject(memDC, hDib);
    // 绘制棋盘格背景
    RECT fullRect = {0, 0, w, h};
    int cellSize = max(4, w / 16);
    drawCheckerboard(memDC, &fullRect, cellSize);
    // 绘制图标
    DrawIconEx(memDC, 0, 0, hIcon, w, h, 0, NULL, DI_NORMAL);
    SelectObject(memDC, old);
    // 写入BMP文件
    HANDLE hf = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        DeleteObject(hDib); DeleteDC(memDC); ReleaseDC(NULL, hdc); return FALSE;
    }
    DWORD imgSize = (DWORD)w * (DWORD)h * 4, wr;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    DWORD fileSize = 54 + imgSize;
    memcpy(hdr + 2, &fileSize, 4); hdr[10] = 54;
    DWORD biSize = 40; memcpy(hdr + 14, &biSize, 4);
    memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    short planes = 1, bpp = 32;
    memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &imgSize, 4);
    WriteFile(hf, hdr, 54, &wr, NULL);
    WriteFile(hf, bits, imgSize, &wr, NULL);
    CloseHandle(hf);
    DeleteObject(hDib); DeleteDC(memDC); ReleaseDC(NULL, hdc);
    return TRUE;
}

// 保存为ICO（多尺寸打包，原创实现）
static BOOL saveIconsAsIco(HICON* icons, int* widths, int* heights, int count, const wchar_t* outPath) {
    if (!icons || !outPath || count <= 0) return FALSE;

    // 收集每个图标的原始数据
    typedef struct { BYTE* data; DWORD size; int w; int h; } IconRaw;
    IconRaw* raws = (IconRaw*)calloc(count, sizeof(IconRaw));
    if (!raws) return FALSE;

    int validCount = 0;
    for (int i = 0; i < count; i++) {
        if (!icons[i]) continue;
        ICONINFO ii = {0};
        if (!GetIconInfo(icons[i], &ii)) continue;
        int w = widths[i], h = heights[i];
        // 获取颜色位图数据
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        BYTE* colorBits = (BYTE*)malloc((size_t)w * h * 4);
        if (!colorBits) { if (ii.hbmColor) DeleteObject(ii.hbmColor); if (ii.hbmMask) DeleteObject(ii.hbmMask); continue; }
        HDC hdc = GetDC(NULL);
        int gotLines = GetDIBits(hdc, ii.hbmColor, 0, h, colorBits, &bmi, DIB_RGB_COLORS);
        ReleaseDC(NULL, hdc);
        // 获取掩码位图数据（AND掩码，高度是图标的2倍：上半XOR，下半AND）
        BYTE* maskBits = NULL;
        DWORD maskSize = 0;
        if (ii.hbmMask) {
            BITMAP bmMask = {0};
            GetObject(ii.hbmMask, sizeof(BITMAP), &bmMask);
            maskSize = (DWORD)bmMask.bmWidthBytes * bmMask.bmHeight;
            maskBits = (BYTE*)malloc(maskSize);
            if (maskBits) {
                BITMAPINFO bmiMask = {0};
                bmiMask.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmiMask.bmiHeader.biWidth = bmMask.bmWidth;
                bmiMask.bmiHeader.biHeight = bmMask.bmHeight;
                bmiMask.bmiHeader.biPlanes = 1;
                bmiMask.bmiHeader.biBitCount = 1;
                HDC hdc2 = GetDC(NULL);
                GetDIBits(hdc2, ii.hbmMask, 0, bmMask.bmHeight, maskBits, &bmiMask, DIB_RGB_COLORS);
                ReleaseDC(NULL, hdc2);
            }
        }
        // 构建ICO中的图标数据：BITMAPINFOHEADER + XOR颜色 + AND掩码
        DWORD xorSize = (DWORD)w * h * 4;
        DWORD andSize = maskSize ? maskSize / 2 : 0;  // 只取下半部分（AND掩码）
        DWORD totalSize = 40 + xorSize + andSize;
        BYTE* iconData = (BYTE*)malloc(totalSize);
        if (iconData) {
            memset(iconData, 0, 40);
            BITMAPINFOHEADER* biHdr = (BITMAPINFOHEADER*)iconData;
            biHdr->biSize = 40;
            biHdr->biWidth = w;
            biHdr->biHeight = h * 2;  // ICO格式：高度是XOR+AND的总高度
            biHdr->biPlanes = 1;
            biHdr->biBitCount = 32;
            biHdr->biCompression = BI_RGB;
            biHdr->biSizeImage = xorSize + andSize;
            memcpy(iconData + 40, colorBits, xorSize);
            if (maskBits && andSize > 0) {
                memcpy(iconData + 40 + xorSize, maskBits + (maskSize / 2), andSize);
            }
            raws[validCount].data = iconData;
            raws[validCount].size = totalSize;
            raws[validCount].w = w;
            raws[validCount].h = h;
            validCount++;
        }
        free(colorBits);
        free(maskBits);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }

    if (validCount == 0) { free(raws); return FALSE; }

    // 写入ICO文件
    HANDLE hf = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        for (int i = 0; i < validCount; i++) free(raws[i].data);
        free(raws); return FALSE;
    }
    DWORD wr;
    // ICONDIR
    WORD reserved = 0, type = 1, count16 = (WORD)validCount;
    WriteFile(hf, &reserved, 2, &wr, NULL);
    WriteFile(hf, &type, 2, &wr, NULL);
    WriteFile(hf, &count16, 2, &wr, NULL);
    // ICONDIRENTRY数组
    DWORD dataOffset = 6 + (DWORD)validCount * 16;
    for (int i = 0; i < validCount; i++) {
        BYTE bWidth = (raws[i].w >= 256) ? 0 : (BYTE)raws[i].w;
        BYTE bHeight = (raws[i].h >= 256) ? 0 : (BYTE)raws[i].h;
        BYTE bColorCount = 0;
        BYTE bReserved = 0;
        WORD wPlanes = 1;
        WORD wBitCount = 32;
        DWORD dwBytesInRes = raws[i].size;
        DWORD dwImageOffset = dataOffset;
        WriteFile(hf, &bWidth, 1, &wr, NULL);
        WriteFile(hf, &bHeight, 1, &wr, NULL);
        WriteFile(hf, &bColorCount, 1, &wr, NULL);
        WriteFile(hf, &bReserved, 1, &wr, NULL);
        WriteFile(hf, &wPlanes, 2, &wr, NULL);
        WriteFile(hf, &wBitCount, 2, &wr, NULL);
        WriteFile(hf, &dwBytesInRes, 4, &wr, NULL);
        WriteFile(hf, &dwImageOffset, 4, &wr, NULL);
        dataOffset += raws[i].size;
    }
    // 图标数据
    for (int i = 0; i < validCount; i++) {
        WriteFile(hf, raws[i].data, raws[i].size, &wr, NULL);
        free(raws[i].data);
    }
    CloseHandle(hf);
    free(raws);
    return TRUE;
}

// 图标检查器窗口过程
static LRESULT CALLBACK iconInspectorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_inspector) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            // 背景
            HBRUSH bgBrush = CreateSolidBrush(RGB(245, 245, 245));
            FillRect(hdc, &clientRect, bgBrush);
            DeleteObject(bgBrush);
            // 标题
            HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            HGDIOBJ oldFont = SelectObject(hdc, titleFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(30, 30, 30));
            wchar_t title[256];
            wchar_t* fileName = wcsrchr(g_inspector->filePath, L'\\');
            fileName = fileName ? fileName + 1 : g_inspector->filePath;
            swprintf_s(title, 256, L"图标检查器 - %ls", fileName);
            TextOutW(hdc, 15, 12, title, (int)wcslen(title));
            SelectObject(hdc, oldFont);
            DeleteObject(titleFont);
            // 网格：6个尺寸图标
            int gridX = 15, gridY = 50;
            int cellW = 100, cellH = 100;
            int cols = 3, rows = 2;
            for (int i = 0; i < ICON_INSPECTOR_SIZES; i++) {
                int col = i % cols;
                int row = i / cols;
                int x = gridX + col * cellW;
                int y = gridY + row * cellH;
                RECT cellRect = {x + 5, y + 5, x + cellW - 5, y + cellH - 25};
                // 选中高亮
                if (i == g_inspector->selectedSize) {
                    HBRUSH selBrush = CreateSolidBrush(RGB(200, 220, 255));
                    HPEN selPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
                    HGDIOBJ oldBrush = SelectObject(hdc, selBrush);
                    HGDIOBJ oldPen = SelectObject(hdc, selPen);
                    Rectangle(hdc, cellRect.left - 3, cellRect.top - 3, cellRect.right + 3, cellRect.bottom + 3);
                    SelectObject(hdc, oldBrush);
                    SelectObject(hdc, oldPen);
                    DeleteObject(selBrush);
                    DeleteObject(selPen);
                }
                // 棋盘格背景
                drawCheckerboard(hdc, &cellRect, 6);
                // 绘制图标（居中）
                if (g_inspector->icons[i]) {
                    int iconSize = g_iconSizes[i];
                    int drawSize = min(iconSize, min(cellRect.right - cellRect.left - 10, cellRect.bottom - cellRect.top - 10));
                    int drawX = cellRect.left + (cellRect.right - cellRect.left - drawSize) / 2;
                    int drawY = cellRect.top + (cellRect.bottom - cellRect.top - drawSize) / 2;
                    DrawIconEx(hdc, drawX, drawY, g_inspector->icons[i], drawSize, drawSize, 0, NULL, DI_NORMAL);
                }
                // 尺寸标签
                HFONT labelFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                oldFont = SelectObject(hdc, labelFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(60, 60, 60));
                wchar_t label[32];
                swprintf_s(label, 32, L"%dx%d", g_iconSizes[i], g_iconSizes[i]);
                int labelX = cellRect.left + (cellRect.right - cellRect.left - (int)wcslen(label) * 7) / 2;
                TextOutW(hdc, labelX, cellRect.bottom + 3, label, (int)wcslen(label));
                SelectObject(hdc, oldFont);
                DeleteObject(labelFont);
            }
            // 信息栏
            RECT infoRect = {15, gridY + rows * cellH + 10, clientRect.right - 15, gridY + rows * cellH + 40};
            HBRUSH infoBrush = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &infoRect, infoBrush);
            DeleteObject(infoBrush);
            HFONT infoFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            oldFont = SelectObject(hdc, infoFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(50, 50, 50));
            int sel = g_inspector->selectedSize;
            wchar_t info[256];
            swprintf_s(info, 256, L"选中: %dx%d  实际: %dx%d  点击网格切换尺寸",
                g_iconSizes[sel], g_iconSizes[sel],
                g_inspector->iconRealW[sel], g_inspector->iconRealH[sel]);
            TextOutW(hdc, infoRect.left + 10, infoRect.top + 8, info, (int)wcslen(info));
            SelectObject(hdc, oldFont);
            DeleteObject(infoFont);
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_LBUTTONDOWN: {
            int gridX = 15, gridY = 50;
            int cellW = 100, cellH = 100;
            int cols = 3;
            int x = (int)(short)LOWORD(lParam);
            int y = (int)(short)HIWORD(lParam);
            for (int i = 0; i < ICON_INSPECTOR_SIZES; i++) {
                int col = i % cols;
                int row = i / cols;
                int cellX = gridX + col * cellW;
                int cellY = gridY + row * cellH;
                if (x >= cellX && x < cellX + cellW && y >= cellY && y < cellY + cellH) {
                    g_inspector->selectedSize = i;
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;
                }
            }
            break;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == 1001) {  // 保存ICO
                wchar_t* name = wcsrchr(g_inspector->filePath, L'\\');
                name = name ? name + 1 : g_inspector->filePath;
                wchar_t* dot = wcsrchr(name, L'.');
                wchar_t baseName[MAX_PATH];
                wcsncpy_s(baseName, MAX_PATH, name, dot ? (size_t)(dot - name) : wcslen(name));
                wchar_t dir[MAX_PATH] = {0};
                wcsncpy_s(dir, MAX_PATH, g_inspector->filePath, wcslen(g_inspector->filePath) - wcslen(name));
                wchar_t outPath[MAX_PATH];
                swprintf_s(outPath, MAX_PATH, L"%ls%ls_icons.ico", dir, baseName);
                if (saveIconsAsIco(g_inspector->icons, g_inspector->iconRealW, g_inspector->iconRealH, ICON_INSPECTOR_SIZES, outPath)) {
                    MessageBoxW(hwnd, outPath, L"保存ICO成功", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"保存ICO失败", L"错误", MB_OK | MB_ICONERROR);
                }
            } else if (id == 1002) {  // 保存BMP
                int sel = g_inspector->selectedSize;
                wchar_t* name = wcsrchr(g_inspector->filePath, L'\\');
                name = name ? name + 1 : g_inspector->filePath;
                wchar_t* dot = wcsrchr(name, L'.');
                wchar_t baseName[MAX_PATH];
                wcsncpy_s(baseName, MAX_PATH, name, dot ? (size_t)(dot - name) : wcslen(name));
                wchar_t dir[MAX_PATH] = {0};
                wcsncpy_s(dir, MAX_PATH, g_inspector->filePath, wcslen(g_inspector->filePath) - wcslen(name));
                wchar_t outPath[MAX_PATH];
                swprintf_s(outPath, MAX_PATH, L"%ls%ls_icon_%d.bmp", dir, baseName, g_iconSizes[sel]);
                if (saveIconAsBmpCheckerboard(g_inspector->icons[sel], g_inspector->iconRealW[sel], g_inspector->iconRealH[sel], outPath)) {
                    MessageBoxW(hwnd, outPath, L"保存BMP成功", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"保存BMP失败", L"错误", MB_OK | MB_ICONERROR);
                }
            } else if (id == 1003) {  // 关闭
                DestroyWindow(hwnd);
            }
            break;
        }
        case WM_DESTROY: {
            if (g_inspector) {
                for (int i = 0; i < ICON_INSPECTOR_SIZES; i++) {
                    if (g_inspector->icons[i]) DestroyIcon(g_inspector->icons[i]);
                }
                free(g_inspector);
                g_inspector = NULL;
            }
            break;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 显示图标检查器窗口
static void showIconInspector(const wchar_t* filePath) {
    if (g_inspector) {
        SetForegroundWindow(g_inspector->hwnd);
        return;
    }
    // 注册窗口类（只注册一次）
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = iconInspectorWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"BFM_IconInspector";
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    g_inspector = (IconInspectorState*)calloc(1, sizeof(IconInspectorState));
    if (!g_inspector) return;
    wcsncpy_s(g_inspector->filePath, MAX_PATH, filePath, MAX_PATH - 1);
    g_inspector->selectedSize = 2;  // 默认选中48px

    // 提取所有尺寸的图标
    for (int i = 0; i < ICON_INSPECTOR_SIZES; i++) {
        g_inspector->icons[i] = extractIconAtSize(filePath, g_iconSizes[i],
            &g_inspector->iconRealW[i], &g_inspector->iconRealH[i]);
    }

    // 创建窗口
    int winW = 360, winH = 380;
    g_inspector->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"BFM_IconInspector",
        L"图标检查器", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        hwndMain, NULL, GetModuleHandleW(NULL), NULL);

    if (!g_inspector->hwnd) {
        for (int i = 0; i < ICON_INSPECTOR_SIZES; i++) {
            if (g_inspector->icons[i]) DestroyIcon(g_inspector->icons[i]);
        }
        free(g_inspector);
        g_inspector = NULL;
        return;
    }

    // 创建按钮
    int btnY = winH - 70;
    CreateWindowW(L"BUTTON", L"保存全部为ICO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        15, btnY, 120, 30, g_inspector->hwnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"保存选中为BMP", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        145, btnY, 120, 30, g_inspector->hwnd, (HMENU)1002, GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        275, btnY, 65, 30, g_inspector->hwnd, (HMENU)1003, GetModuleHandleW(NULL), NULL);

    ShowWindow(g_inspector->hwnd, SW_SHOW);
    UpdateWindow(g_inspector->hwnd);
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
    // 首先尝试 UTF-8，回退到 ANSI 代码页

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
    // 保持当前视图样式；仅过滤为文件夹 + .exe，避免 Wine LVS_ICON 渲染问题

    navigateRefresh();
}

// ---------- Folder size (background thread) ----------
#define WM_FOLDERSIZE_DONE (WM_APP+77)
struct FolderSizeReq { struct FileNode* node; HWND hwnd; };
static unsigned long __stdcall folderSizeThread(void* param) {
    struct FolderSizeReq* req=(struct FolderSizeReq*)param;
    // 递归求和

    unsigned long long total=0;
    // 通过路径递归使用 FindFirstFile

    wchar_t base[MAX_PATH]={0}; getFileNodePath(req->node,base);
    // 迭代栈

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
// ============================================================================
// 递归文件夹比较。完整遍历两个面板根（不仅是第一个

// 级别），收集相对路径 + 大小，不区分大小写排序并合并到

// 查找仅存在于一侧或大小/类型不同的条目。结果

// 显示在可滚动的只读对话框中。

// ============================================================================
struct RelEntry { wchar_t rel[MAX_PATH]; ULONGLONG size; bool isDir; };

#define CMP_BUDGET 60000   // max files visited per side (guards huge trees)
#define CMP_OUT_MAX 1200   // max diff lines shown in the result dialog

static void collectTree(const wchar_t* base, const wchar_t* rel,
                        struct RelEntry** arr, int* n, int* cap, int* budget) {
    if (*budget <= 0) return;
    wchar_t pattern[MAX_PATH * 2 + 8];
    if (rel[0]) swprintf_s(pattern, _countof(pattern), L"%ls\\%ls\\*", base, rel);
    else        swprintf_s(pattern, _countof(pattern), L"%ls\\*", base);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t child[MAX_PATH];
        if (rel[0]) swprintf_s(child, _countof(child), L"%ls\\%ls", rel, fd.cFileName);
        else        wcscpy_s(child, _countof(child), fd.cFileName);
        bool isDir = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        if (*n >= *cap) {
            *cap = *cap ? *cap * 2 : 256;
            *arr = (struct RelEntry*)realloc(*arr, sizeof(struct RelEntry) * (*cap));
            if (!*arr) { FindClose(h); return; }
        }
        struct RelEntry* e = &(*arr)[(*n)++];
        ZeroMemory(e, sizeof(*e));
        wcsncpy_s(e->rel, _countof(e->rel), child, _TRUNCATE);
        e->isDir = isDir;
        LARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
        e->size = (ULONGLONG)sz.QuadPart;
        (*budget)--;
        // 仅递归到真实子文件夹（跳过联接/符号链接以避免循环）。

        if (isDir && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            collectTree(base, child, arr, n, cap, budget);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int cmpRelEntry(const void* a, const void* b) {
    return _wcsicmp(((const struct RelEntry*)a)->rel, ((const struct RelEntry*)b)->rel);
}

static void cmpAppend(wchar_t* out, size_t cap, size_t* used,
                      const wchar_t* tag, const wchar_t* rel) {
    wchar_t line[MAX_PATH + 32];
    swprintf_s(line, _countof(line), L"%ls%ls\r\n", tag, rel);
    size_t need = wcslen(line);
    if (*used + need + 1 < cap) { wcscat_s(out + *used, cap - *used, line); *used += need; }
}

// 模态只读结果对话框：多行等宽编辑框 + 关闭按钮。

static void showTextResultDialog(const wchar_t* title, const wchar_t* body) {
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 460, hwndMain, NULL, globalHInstance, NULL);
    if (!hwnd) { MessageBoxW(hwndMain, body, title, MB_OK); return; }
    HFONT mono = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", body,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY
        | ES_AUTOVSCROLL | ES_NOHIDESEL,
        10, 10, 532, 388, hwnd, (HMENU)2001, globalHInstance, NULL);
    if (hEdit) {
        SendMessageW(hEdit, EM_LIMITTEXT, 0x7FFFFFFF, 0);  // allow long result text
        if (mono) SendMessageW(hEdit, WM_SETFONT, (WPARAM)mono, TRUE);
    }
    HWND hBtn = CreateWindowExW(0, L"BUTTON", lc_str.proc_close,
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        230, 406, 100, 32, hwnd, (HMENU)IDOK, globalHInstance, NULL);
    HFONT ui = getUIFont();
    if (hBtn && ui) SendMessageW(hBtn, WM_SETFONT, (WPARAM)ui, TRUE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // 按钮通知的 msg.hwnd 是父窗口，因此需要匹配

        // 通过控件 id（IDOK），同时按 Esc（IDCANCEL）/WM_CLOSE 也关闭。

        if (msg.message == WM_COMMAND &&
            (LOWORD(msg.wParam) == IDOK || LOWORD(msg.wParam) == IDCANCEL))
            DestroyWindow(hwnd);
        if (!IsWindow(hwnd)) break;
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!IsWindow(hwnd)) break;
    }
}

void onMenuItemComparePanesClick(void) {
    struct Pane* p0=&panes[0]; struct Pane* p1=&panes[1];
    if(p0->numItems==0 && p1->numItems==0){
        MessageBoxW(hwndMain,lc_str.no_recent,lc_str.compare_panes,MB_OK|MB_ICONINFORMATION);
        return;
    }
    // 同时仅选中列表中的顶层差异，便于快速定位。

    int onlyLeft=0, onlyRight=0;
    int i,j;
    ListView_SetItemState(p0->hwndList,-1,0,LVIS_SELECTED);
    ListView_SetItemState(p1->hwndList,-1,0,LVIS_SELECTED);
    for(i=0;i<p0->numItems;i++){
        bool found=false;
        for(j=0;j<p1->numItems;j++)
            if(wcscmp(p0->items[i].node->name,p1->items[j].node->name)==0){found=true;break;}
        if(!found){ListView_SetItemState(p0->hwndList,i,LVIS_SELECTED,LVIS_SELECTED);onlyLeft++;}
    }
    for(j=0;j<p1->numItems;j++){
        bool found=false;
        for(i=0;i<p0->numItems;i++)
            if(wcscmp(p1->items[j].node->name,p0->items[i].node->name)==0){found=true;break;}
        if(!found){ListView_SetItemState(p1->hwndList,j,LVIS_SELECTED,LVIS_SELECTED);onlyRight++;}
    }

    // 两个面板根的递归比较。

    wchar_t root0[MAX_PATH]={0}, root1[MAX_PATH]={0};
    getFileNodePath(p0->currPath, root0);
    getFileNodePath(p1->currPath, root1);
    struct RelEntry *a=NULL, *b=NULL;
    int na=0, ca=0, nb=0, cb=0, budget=CMP_BUDGET;
    collectTree(root0, L"", &a, &na, &ca, &budget);
    budget = CMP_BUDGET;
    collectTree(root1, L"", &b, &nb, &cb, &budget);
    if (a) qsort(a, na, sizeof(struct RelEntry), cmpRelEntry);
    if (b) qsort(b, nb, sizeof(struct RelEntry), cmpRelEntry);

    int cLeft=0, cRight=0, cChanged=0, shown=0;
    // 堆上构建结果，避免大树导致栈缓冲区溢出。大小设为

    // 最多容纳 CMP_OUT_MAX 条完整路径行（每行最多 MAX_PATH+32 字符）。

    size_t cap = (size_t)CMP_OUT_MAX * (MAX_PATH + 40);
    wchar_t* out = (wchar_t*)malloc(cap * sizeof(wchar_t));
    if (!out) { free(a); free(b); return; }
    out[0]=L'\0'; size_t used=0;
    i=0; j=0;
    while (i < na || j < nb) {
        int cmp;
        if (i >= na) cmp = 1;
        else if (j >= nb) cmp = -1;
        else cmp = _wcsicmp(a[i].rel, b[j].rel);
        if (cmp < 0) {
            cLeft++;
            if (shown < CMP_OUT_MAX) { cmpAppend(out, cap, &used, lc_str.cmp_tag_left, a[i].rel); shown++; }
            i++;
        } else if (cmp > 0) {
            cRight++;
            if (shown < CMP_OUT_MAX) { cmpAppend(out, cap, &used, lc_str.cmp_tag_right, b[j].rel); shown++; }
            j++;
        } else {
            // 相同相对路径：当类型不同或两个都是大小不同的文件时标记。

            if (a[i].isDir != b[j].isDir ||
                (!a[i].isDir && a[i].size != b[j].size)) {
                cChanged++;
                if (shown < CMP_OUT_MAX) { cmpAppend(out, cap, &used, lc_str.cmp_tag_changed, a[i].rel); shown++; }
            }
            i++; j++;
        }
    }
    int totalDiff = cLeft + cRight + cChanged;
    wchar_t head[256];
    swprintf_s(head, _countof(head), lc_str.cmp_summary_fmt, cLeft, cRight, cChanged);
    size_t hl = wcslen(head);
    wchar_t* final = (wchar_t*)malloc((hl + wcslen(out) + 16) * sizeof(wchar_t));
    if (final) {
        swprintf_s(final, hl + wcslen(out) + 16, L"%ls\r\n\r\n%ls", head,
                   totalDiff == 0 ? lc_str.cmp_no_diff : out);
        showTextResultDialog(lc_str.compare_panes, final);
        free(final);
    } else {
        showTextResultDialog(lc_str.compare_panes, head);
    }
    free(out); free(a); free(b);
}

// 将活动面板中选中的文件与另一面板中第一个选中的文件进行比较。

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
