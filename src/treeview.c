#include "main.h"

extern struct FileNode* treeFileNode;
extern HINSTANCE globalHInstance;
extern HWND hwndMain;

HWND hwndTreeview = NULL;

// Favorites branch uses sentinel lParam values (negative) so they never collide
// with a heap FileNode pointer. Root = FAV_ROOT_MARK, item i = FAV_ITEM_MARK - i.
#define FAV_ROOT_MARK  ((LONG_PTR)-100)
#define FAV_ITEM_MARK  ((LONG_PTR)-200)
static bool isFavItem(LONG_PTR p, int* outIdx) {
    if (p <= FAV_ITEM_MARK) { if (outIdx) *outIdx = (int)(FAV_ITEM_MARK - p); return true; }
    return false;
}

static HTREEITEM favRootItem = NULL;
static HIMAGELIST favCustomHiml = NULL;
static int favStarIndex = -1;

// Create a 16x16 golden star bitmap (magenta = transparent mask).
static HBITMAP createStarBitmap(void) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmp = CreateCompatibleBitmap(hdcScreen, 16, 16);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, hbmp);
    HBRUSH bg = CreateSolidBrush(RGB(255, 0, 255));
    RECT rc = {0, 0, 16, 16};
    FillRect(hdcMem, &rc, bg);
    DeleteObject(bg);
    // 10-point star polygon (concave).
    POINT pts[10] = {
        {8, 0}, {10, 6}, {16, 6}, {11, 10}, {13, 15},
        {8, 12}, {3, 15}, {5, 10}, {0, 6}, {6, 6}
    };
    HBRUSH fill = CreateSolidBrush(RGB(255, 193, 7));   // golden yellow
    HPEN border = CreatePen(PS_SOLID, 1, RGB(180, 120, 0)); // dark amber outline
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, fill);
    HPEN oldPen = (HPEN)SelectObject(hdcMem, border);
    Polygon(hdcMem, pts, 10);
    SelectObject(hdcMem, oldBrush);
    SelectObject(hdcMem, oldPen);
    DeleteObject(fill);
    DeleteObject(border);
    SelectObject(hdcMem, oldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hbmp;
}

// Build a custom image list: copy all system small icons, then append the star.
// This keeps drive/folder icon indices valid while giving favorites a distinct icon.
static void ensureFavImageList(void) {
    if (favCustomHiml) return;
    HIMAGELIST himlBig, himlSmall;
    Shell_GetImageLists(&himlBig, &himlSmall);
    if (!himlSmall) return;  // Wine without system list: keep defaults.
    int count = ImageList_GetImageCount(himlSmall);
    favCustomHiml = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, count + 1, 1);
    if (!favCustomHiml) return;
    for (int i = 0; i < count; i++) {
        HICON hIcon = ImageList_GetIcon(himlSmall, i, ILD_TRANSPARENT);
        if (hIcon) {
            ImageList_AddIcon(favCustomHiml, hIcon);
            DestroyIcon(hIcon);
        }
    }
    HBITMAP star = createStarBitmap();
    if (star) {
        favStarIndex = ImageList_AddMasked(favCustomHiml, star, RGB(255, 0, 255));
        DeleteObject(star);
    }
    TreeView_SetImageList(hwndTreeview, favCustomHiml, TVSIL_NORMAL);
}

static void insertFavoritesBranch(void) {
    ensureFavImageList();

    // Root node uses the golden star icon if available, else folder icon.
    struct FileInfo rootFi = {0};
    getFileInfo(L"C:\\", TYPE_DIR, false, &rootFi);
    int rootIcon = (favStarIndex >= 0) ? favStarIndex : rootFi.icon;

    TVINSERTSTRUCT tvis = {0};
    tvis.hParent = NULL;
    tvis.hInsertAfter = TVI_LAST;
    tvis.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tvis.itemex.pszText = (LPWSTR)L"\u6536\u85cf\u5939";  // 收藏夹
    tvis.itemex.cchTextMax = 8;
    tvis.itemex.lParam = (LPARAM)FAV_ROOT_MARK;
    tvis.itemex.iImage = rootIcon;
    tvis.itemex.iSelectedImage = rootIcon;

    wchar_t favs[FAV_MAX][MAX_PATH];
    int n = favGetAll(favs);
    tvis.itemex.cChildren = n > 0 ? 1 : 0;
    favRootItem = TreeView_InsertItem(hwndTreeview, &tvis);

    for (int i = 0; i < n; i++) {
        // Detect real type: favorites may be files, not only folders.
        DWORD attr = GetFileAttributesW(favs[i]);
        int nodeType = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                       ? TYPE_DIR : TYPE_FILE;
        struct FileInfo fi = {0};
        getFileInfo(favs[i], nodeType, false, &fi);
        TVINSERTSTRUCT ci = {0};
        ci.hParent = favRootItem;
        ci.hInsertAfter = TVI_LAST;
        ci.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        const wchar_t* name = wcsrchr(favs[i], L'\\');
        name = name ? name + 1 : favs[i];
        ci.itemex.pszText = (LPWSTR)name;
        ci.itemex.cchTextMax = wcslen(name);
        ci.itemex.lParam = (LPARAM)(FAV_ITEM_MARK - i);
        // All favorites share the star; fall back to the real file/folder icon
        // only if the custom image list was unavailable.
        int itemIcon = (favStarIndex >= 0) ? favStarIndex : fi.icon;
        ci.itemex.iImage = itemIcon;
        ci.itemex.iSelectedImage = itemIcon;
        TreeView_InsertItem(hwndTreeview, &ci);
    }

    // Expand the favorites branch by default so users see saved paths immediately.
    if (favRootItem)
        TreeView_Expand(hwndTreeview, favRootItem, TVE_EXPAND);
}

static void updateTreeItemsDeep(HTREEITEM parentItem, struct FileNode* parentNode) {
    HTREEITEM child = TreeView_GetChild(hwndTreeview, parentItem);
    
    while (child != NULL) {
        HTREEITEM itemToDelete = child;
        child = TreeView_GetNextSibling(hwndTreeview, child);
        TreeView_DeleteItem(hwndTreeview, itemToDelete);
    }
    
    if (parentNode->children) {
        TVINSERTSTRUCT tvis;
        tvis.hParent = parentItem;
        tvis.hInsertAfter = TVI_LAST;
        tvis.itemex.mask = TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_TEXT | TVIF_STATE;
        
        wchar_t parentPath[MAX_PATH] = {0};
        if (getFileNodePath(parentNode, parentPath)) wcscat_s(parentPath, MAX_PATH, L"\\");
        wchar_t path[MAX_PATH] = {0};

        // NOTE: do NOT TreeView_SetImageList here. The tree uses a single custom
        // image list (system icons + favorites star) created by ensureFavImageList;
        // resetting it to the raw system list would erase the star icon.

        struct FileNode* node = parentNode->children;
        do {
            swprintf_s(path, MAX_PATH, L"%ls%ls", parentPath, node->name);
            
            struct FileInfo fi = {0};
            getFileInfo(path, node->type, false, &fi);

            tvis.itemex.cChildren = node->hasChildDirs ? 1 : 0;
            tvis.itemex.state = node->children ? TVIS_EXPANDED : 0;
            tvis.itemex.stateMask = TVIS_EXPANDED;
            tvis.itemex.pszText = node->name;
            tvis.itemex.cchTextMax = wcslen(node->name);
            tvis.itemex.iImage = fi.icon;
            tvis.itemex.iSelectedImage = fi.icon;
            tvis.itemex.lParam = (LPARAM)node;

            HTREEITEM handle = TreeView_InsertItem(hwndTreeview, &tvis);
            updateTreeItemsDeep(handle, node);
        }
        while ((node = node->sibling) != NULL);
    }
}

static void updateTreeItems() {
    TreeView_DeleteAllItems(hwndTreeview);
    // Single shared image list for the whole tree (system icons + favorites star).
    ensureFavImageList();

    TVINSERTSTRUCT tvis;
    tvis.hParent = NULL;
    tvis.hInsertAfter = TVI_ROOT;
    tvis.itemex.mask = TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_TEXT | TVIF_STATE;
    
    struct FileNode* node = treeFileNode;
    do {
        ITEMIDLIST* pidl = NULL;
        switch (node->type) {
            case TYPE_DESKTOP:
                SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidl);
                break;
            case TYPE_PERSONAL:
                SHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &pidl);
                break;
            case TYPE_COMPUTER:
                SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidl);
                break;
            default:
                break;
        }
        
        SHFILEINFO sfi = {0};
        SHGetFileInfo((LPCWSTR)pidl, 0, &sfi, sizeof(SHFILEINFO), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_PIDL);
        CoTaskMemFree(pidl);

        tvis.itemex.cChildren = node->hasChildDirs ? 1 : 0;
        tvis.itemex.state = node->children ? TVIS_EXPANDED : 0;
        tvis.itemex.stateMask = TVIS_EXPANDED;
        tvis.itemex.pszText = node->name;
        tvis.itemex.cchTextMax = wcslen(node->name);
        tvis.itemex.iImage = sfi.iIcon;
        tvis.itemex.iSelectedImage = sfi.iIcon;
        tvis.itemex.lParam = (LPARAM)node;

        HTREEITEM handle = TreeView_InsertItem(hwndTreeview, &tvis);
        updateTreeItemsDeep(handle, node);      
    }
    while ((node = node->sibling) != NULL);

    insertFavoritesBranch();
}

static void treeItemExpand(HTREEITEM treeItem, struct FileNode* node) {
    buildChildNodes(node, true);
    checkIfNodesHasChildDirs(node->children, false);
    updateTreeItemsDeep(treeItem, node);
}

static void treeItemCollapse(HTREEITEM treeItem, struct FileNode* node) {
    UNREFERENCED_PARAMETER(treeItem);
    freeChildNodes(node);
}

LRESULT treeviewNotify(NMHDR* nmhdr) {
    switch (nmhdr->code) {
        case TVN_ITEMEXPANDING: {
            NMTREEVIEW* nmtv = (NMTREEVIEW*)nmhdr;
            LONG_PTR lp = nmtv->itemNew.lParam;
            if (lp == FAV_ROOT_MARK) break;  // favorites are static, no lazy load
            struct FileNode* node = (struct FileNode*)lp;
            if (nmtv->action == TVE_EXPAND) treeItemExpand(nmtv->itemNew.hItem, node);
            break;
        }
        case TVN_ITEMEXPANDED: {
            NMTREEVIEW* nmtv = (NMTREEVIEW*)nmhdr;
            LONG_PTR lp = nmtv->itemNew.lParam;
            if (lp == FAV_ROOT_MARK) break;
            struct FileNode* node = (struct FileNode*)lp;
            if (nmtv->action == TVE_COLLAPSE) {
                treeItemCollapse(nmtv->itemNew.hItem, node);
            }
            break;
        }
        case NM_CLICK: {
            TVHITTESTINFO tvhti;
            GetCursorPos(&tvhti.pt);
            ScreenToClient(hwndTreeview, &tvhti.pt);
            TreeView_HitTest(hwndTreeview, &tvhti);

            if (tvhti.hItem != NULL && (tvhti.flags & TVHT_ONITEM)) {
                TVITEM item;
                item.hItem = tvhti.hItem;
                item.mask = TVIF_PARAM;
                TreeView_GetItem(hwndTreeview, &item);
                LONG_PTR lp = item.lParam;
                int favIdx;
                if (isFavItem(lp, &favIdx)) {
                    wchar_t favs[FAV_MAX][MAX_PATH];
                    int n = favGetAll(favs);
                    if (favIdx >= 0 && favIdx < n && favs[favIdx][0]) {
                        // A favorited file is opened; a favorited folder is navigated to.
                        DWORD attr = GetFileAttributesW(favs[favIdx]);
                        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                            navigateToPath(favs[favIdx]);
                        } else {
                            wchar_t workDir[MAX_PATH] = {0};
                            wcscpy_s(workDir, MAX_PATH, favs[favIdx]);
                            wchar_t* slash = wcsrchr(workDir, L'\\');
                            if (slash) *slash = L'\0';
                            ShellExecuteW(hwndMain, L"open", favs[favIdx], NULL,
                                          workDir[0] ? workDir : NULL, SW_SHOWNORMAL);
                        }
                    }
                }
                else if (lp != FAV_ROOT_MARK) {
                    struct FileNode* node = (struct FileNode*)lp;
                    navigateToFileNode(node);
                }
            }
            break;
        }
        case NM_RCLICK: {
            TVHITTESTINFO tvhti;
            GetCursorPos(&tvhti.pt);
            ScreenToClient(hwndTreeview, &tvhti.pt);
            TreeView_HitTest(hwndTreeview, &tvhti);
            if (tvhti.hItem != NULL && (tvhti.flags & TVHT_ONITEM)) {
                TVITEM item;
                item.hItem = tvhti.hItem;
                item.mask = TVIF_PARAM;
                TreeView_GetItem(hwndTreeview, &item);
                int favIdx;
                if (isFavItem(item.lParam, &favIdx)) {
                    HMENU m = CreatePopupMenu();
                    AppendMenuW(m, MF_STRING, 1, L"\u79fb\u9664\u6536\u85cf");  // 移除收藏
                    POINT pt; GetCursorPos(&pt);
                    int cmd = TrackPopupMenu(m, TPM_RETURNCMD, pt.x, pt.y, 0, hwndTreeview, NULL);
                    DestroyMenu(m);
                    if (cmd == 1) favRemoveAt(favIdx);
                }
            }
            break;
        }
    }

    return 0;   
}

// The tree view is not otherwise subclassed; this exists only so its non-client
// scrollbars can be repainted dark (Wine draws them light — see themePaintScrollbars).
static WNDPROC OrigTreeviewProc = NULL;

static LRESULT CALLBACK TreeviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (themeScrollbarsHookBefore(hwnd, msg, wParam)) return 0;
    LRESULT result = CallWindowProc(OrigTreeviewProc, hwnd, msg, wParam, lParam);
    themeScrollbarsHookAfter(hwnd, msg);
    return result;
}

void createTreeview() {
    hwndTreeview = CreateWindowEx(0, WC_TREEVIEW, NULL, WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS |                              TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwndMain, (HMENU)NULL, globalHInstance, NULL);

    SendMessage(hwndTreeview, WM_SETFONT, (WPARAM)getUIFont(), TRUE);
    OrigTreeviewProc = (WNDPROC)SetWindowLongPtr(hwndTreeview, GWLP_WNDPROC, (LONG_PTR)TreeviewWndProc);

    updateTreeItems();
    UpdateWindow(hwndTreeview);
}

// Rebuild the whole tree so the Favorites branch reflects the registry.
void favRefreshTree(void) {
    if (!hwndTreeview) return;
    updateTreeItems();
}