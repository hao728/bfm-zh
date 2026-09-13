#include "main.h"

extern struct FileNode* treeFileNode;
extern HINSTANCE globalHInstance;
extern HWND hwndMain;

HWND hwndTreeview = NULL;

// 收藏夹分支使用哨兵 lParam 值（负数），因此它们永远不会冲突

// 带有堆 FileNode 指针。根 = FAV_ROOT_MARK，项 i = FAV_ITEM_MARK - i。

#define FAV_ROOT_MARK  ((LONG_PTR)-100)
#define FAV_ITEM_MARK  ((LONG_PTR)-200)
static bool isFavItem(LONG_PTR p, int* outIdx) {
    if (p <= FAV_ITEM_MARK) { if (outIdx) *outIdx = (int)(FAV_ITEM_MARK - p); return true; }
    return false;
}

static HTREEITEM favRootItem = NULL;
// 收藏夹获得金色星星作为状态图像（绘制在普通图标旁边），

// 不作为普通图标。getFileInfo() 返回系统小图标列表的索引

// 列表，因此树必须直接绑定到该系统列表——复制它

// （ImageList_Duplicate 或手动 GetIcon/AddIcon）在 Wine 下会失效：部分系统

// 图标复制失败，私有列表变短，驱动器索引指向

// 超出其末尾，会渲染空白或最后一个（星星）图像。状态图像是

// 仅由收藏夹分支使用的覆盖列表，因此系统图标保持完整。

static HIMAGELIST favStateList = NULL;

// 创建 16x16 金色星星位图（品红色 = 透明掩码）。

static HBITMAP createStarBitmap(void) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmp = CreateCompatibleBitmap(hdcScreen, 16, 16);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, hbmp);
    HBRUSH bg = CreateSolidBrush(RGB(255, 0, 255));
    RECT rc = {0, 0, 16, 16};
    FillRect(hdcMem, &rc, bg);
    DeleteObject(bg);
    POINT pts[10] = {
        {8, 0}, {10, 6}, {16, 6}, {11, 10}, {13, 15},
        {8, 12}, {3, 15}, {5, 10}, {0, 6}, {6, 6}
    };
    HBRUSH fill = CreateSolidBrush(RGB(255, 193, 7));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(180, 120, 0));
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

// 将树视图绑定到系统小图标图像列表（绝不复制：见上文）。

// 同时只创建一次收藏夹状态列表：槽 0 为空，槽 1 为金色星星。

static void bindSystemImageList(void) {
    HIMAGELIST himlBig = NULL, himlSmall = NULL;
    HIMAGELIST sysList = NULL;
    if (Shell_GetImageLists(&himlBig, &himlSmall) && himlSmall) {
        sysList = himlSmall;
    } else {
        SHFILEINFO sfi = {0};
        sysList = (HIMAGELIST)SHGetFileInfo(L"", 0, &sfi, sizeof(SHFILEINFO),
                            SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    }
    if (sysList) TreeView_SetImageList(hwndTreeview, sysList, TVSIL_NORMAL);

    if (!favStateList) {
        favStateList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 2, 1);
        if (favStateList) {
            // 槽 0：完全透明（掩码颜色 = 黑色，空位图）。

            HDC hdcScreen = GetDC(NULL);
            HDC hdcMem = CreateCompatibleDC(hdcScreen);
            HBITMAP emptyBmp = CreateCompatibleBitmap(hdcScreen, 16, 16);
            HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, emptyBmp);
            RECT rc = {0, 0, 16, 16};
            HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdcMem, &rc, black);
            DeleteObject(black);
            SelectObject(hdcMem, oldBmp);
            DeleteDC(hdcMem);
            ReleaseDC(NULL, hdcScreen);
            ImageList_AddMasked(favStateList, emptyBmp, RGB(0, 0, 0));
            DeleteObject(emptyBmp);
            // 槽 1：金色星星。

            HBITMAP star = createStarBitmap();
            if (star) {
                ImageList_AddMasked(favStateList, star, RGB(255, 0, 255));
                DeleteObject(star);
            }
        }
    }
    if (favStateList) TreeView_SetImageList(hwndTreeview, favStateList, TVSIL_STATE);
}

static void insertFavoritesBranch(void) {
    // 根节点使用系统文件夹图标 + 金色星星状态图像。

    struct FileInfo rootFi = {0};
    getFileInfo(L"C:\\", TYPE_DIR, false, &rootFi);
    int rootIcon = rootFi.icon;

    TVINSERTSTRUCT tvis = {0};
    tvis.hParent = NULL;
    tvis.hInsertAfter = TVI_LAST;
    tvis.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE
                     | TVIF_SELECTEDIMAGE | TVIF_STATE;
    tvis.itemex.pszText = (LPWSTR)L"\u6536\u85cf\u5939";  // 收藏夹
    tvis.itemex.cchTextMax = 8;
    tvis.itemex.lParam = (LPARAM)FAV_ROOT_MARK;
    tvis.itemex.iImage = rootIcon;
    tvis.itemex.iSelectedImage = rootIcon;
    tvis.itemex.state = INDEXTOSTATEIMAGEMASK(1);
    tvis.itemex.stateMask = TVIS_STATEIMAGEMASK;

    wchar_t favs[FAV_MAX][MAX_PATH];
    int n = favGetAll(favs);
    tvis.itemex.cChildren = n > 0 ? 1 : 0;
    favRootItem = TreeView_InsertItem(hwndTreeview, &tvis);

    for (int i = 0; i < n; i++) {
        // 检测真实类型：收藏项可能是文件，不仅是文件夹。

        DWORD attr = GetFileAttributesW(favs[i]);
        int nodeType = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                       ? TYPE_DIR : TYPE_FILE;
        struct FileInfo fi = {0};
        getFileInfo(favs[i], nodeType, false, &fi);
        TVINSERTSTRUCT ci = {0};
        ci.hParent = favRootItem;
        ci.hInsertAfter = TVI_LAST;
        ci.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE;
        const wchar_t* name = wcsrchr(favs[i], L'\\');
        name = name ? name + 1 : favs[i];
        // 来自系统列表的真实文件/文件夹图标；金色星星状态

        // 图像（绘制在旁边）标记它为收藏项。

        ci.itemex.pszText = (LPWSTR)name;
        ci.itemex.cchTextMax = wcslen(name);
        ci.itemex.lParam = (LPARAM)(FAV_ITEM_MARK - i);
        ci.itemex.iImage = fi.icon;
        ci.itemex.iSelectedImage = fi.icon;
        ci.itemex.state = INDEXTOSTATEIMAGEMASK(1);
        ci.itemex.stateMask = TVIS_STATEIMAGEMASK;
        TreeView_InsertItem(hwndTreeview, &ci);
    }

    // 默认展开收藏夹分支，让用户立即看到已保存的路径。

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

        // 树在 updateTreeItems 中绑定到系统图像列表；

        // 不要在这里重置它，否则展开/刷新时图标会消失。


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
    // 直接绑定系统小图标列表（绝不复制它）。

    bindSystemImageList();

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
                        // 收藏的文件会被打开；收藏的文件夹会被导航进入。

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

// 树视图没有其他子类化；这存在只是为了使其非客户区

// 滚动条可以重绘为深色（Wine 将它们绘为浅色——见 themePaintScrollbars）。

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

// 重建整个树，使收藏夹分支反映注册表。

void favRefreshTree(void) {
    if (!hwndTreeview) return;
    updateTreeItems();
}
