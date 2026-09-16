#ifndef CONTENT_VIEW_H
#define CONTENT_VIEW_H

enum ViewStyle {
    STYLE_LARGE_ICON,
    STYLE_SMALL_ICON,
    STYLE_LIST,
    STYLE_DETAILS
};

LRESULT contentViewNotify(NMHDR* nmhdr);
void createContentView();
void clearContentView();
void refreshContentView();
void setViewStyle(enum ViewStyle newViewStyle);
void searchFor(wchar_t* keyword);

// 双面板 API（在 content_view.c 中实现，由 main.c / navbar.c 使用）

void cvInitPanePaths();
void cvToggleSplit();
bool cvIsContentView(HWND h);
HWND cvActiveHwnd();
HWND cvPaneHwnd(int i);
HWND cvPaneLabel(int i);
bool cvActivatePaneByLabel(HWND h);
void cvFitColumns(HWND list, int totalWidth);
int cvActiveIdx();
bool cvSplitOn();
void cvToggleMemoryDisplay(void);
bool cvMemoryVisible(void);
bool showConfirmDialog(HWND parent, const wchar_t* title, const wchar_t* msg);
HBITMAP cvGetFileIconBitmap(const wchar_t* path, int w, int h);
void cvSetSort(int colIdx);  // 查看菜单排序入口：0=名称 1=类型 2=大小 3=日期
void cvEnsureLocaleFallback(void);  // 启动时回退转区残留的Locale

void onMenuItemUpClick();
void onMenuItemOpenClick();
void onMenuItemOpenAsAdminClick();
void onMenuItemOpenWithClick();
void onMenuItemEditClick();
void onMenuItemCutClick();
void onMenuItemCopyClick();
void onMenuItemCreateShortcutClick();
void onMenuItemDeleteClick();
void onMenuItemRenameClick();
void onMenuItemPasteClick();
void onMenuItemPasteShortcutClick();
void onMenuItemNewFolderClick();
void onMenuItemNewFileClick();
void onMenuItemSelectAllClick();
void onMenuItemPropertiesClick();

#endif