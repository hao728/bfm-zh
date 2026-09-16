#include "main.h"

#define ID_EVENT_PRELOADER 100
#define PRELOADER_PERIOD 120
#define CEILING(x, y) ((x+(y-1))/y)

enum Msg {
    MSG_CLOSE = WM_APP,
    MSG_NAVIGATE_REFRESH,
    MSG_PROGRESS
};

enum FileAction {
    ACTION_NONE,
    ACTION_DELETE,
    ACTION_COPY,
    ACTION_MOVE,
    ACTION_ISO_EXTRACT
};

struct ActionData {
    enum FileAction action;
    wchar_t** srcPaths;
    int numSrcPaths;
    wchar_t* dstPath;
    bool cancel;
    uint64_t totalBytes;
    uint64_t doneBytes;
};

static HWND hwndDlg;
static HICON preloaderIcons[8] = {0};
static int preloaderIconIndex = 0;
static wchar_t** clipboard = NULL;
static int clipboardSize = 0;
static bool clipboardIsCut = false;
static struct ActionData* actionData = NULL;

// 活动文件操作的进度跟踪。

static struct ActionData* g_activeAction = NULL;
static LONGLONG g_fileLastTransferred = 0;
static int g_lastPct = -1;

static void postProgress() {
    if (!g_activeAction || g_activeAction->totalBytes == 0) return;
    int pct = (int)((g_activeAction->doneBytes * 100) / g_activeAction->totalBytes);
    if (pct > 100) pct = 100;
    if (pct != g_lastPct) {
        g_lastPct = pct;
        PostMessage(hwndDlg, MSG_PROGRESS, (WPARAM)pct, 0);
    }
}

extern HINSTANCE globalHInstance;
extern HWND hwndMain;

static void animatePreloader() {
    SendDlgItemMessage(hwndDlg, IDC_PRELOADER, STM_SETICON, (WPARAM)preloaderIcons[preloaderIconIndex], 0);
    preloaderIconIndex = (preloaderIconIndex + 1) % 8;
}

void clearClipboard() {
    if (clipboard) {
        for (int i = 0; i < clipboardSize; i++) free(clipboard[i]);
        MEMFREE(clipboard);
    }
    clipboardSize = 0;
}

static void freeActionData() {
    g_activeAction = NULL;
    if (clipboardIsCut) clearClipboard();
    
    if (actionData) {
        if (actionData->action == ACTION_DELETE || actionData->action == ACTION_ISO_EXTRACT) {
            for (int i = 0; i < actionData->numSrcPaths; i++) {
                MEMFREE(actionData->srcPaths[i]);
            }
            
            actionData->numSrcPaths = 0;
            MEMFREE(actionData->srcPaths);
        }
        
        MEMFREE(actionData->dstPath);
        MEMFREE(actionData);
    }
}

INT_PTR CALLBACK FileActionDialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);

    switch (msg) {
        case WM_INITDIALOG: {
            RECT rect, rect1;
            GetWindowRect(GetParent(hwndDlg), &rect);
            GetClientRect(hwndDlg, &rect1);
            SetWindowPos(hwndDlg, NULL, (rect.right + rect.left) / 2 - (rect1.right - rect1.left) / 2, (rect.bottom + rect.top) / 2 - (rect1.bottom - rect1.top) / 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
            
            for (int i = 0; i < 8; i++) {
                preloaderIcons[i] = (HICON)LoadImage(globalHInstance, MAKEINTRESOURCE(IDI_PRELOADER_1 + i), IMAGE_ICON, 64, 64, 0);
            }
            
            SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);

            HWND hwndLabel = GetDlgItem(hwndDlg, IDC_LABEL);
            switch (actionData->action) {
                case ACTION_DELETE: {
                    SetWindowText(hwndDlg, lc_str.deleting_files);
                    SetWindowText(hwndLabel, lc_str.msg_deleting_files);
                    break;
                }
                case ACTION_COPY: {
                    SetWindowText(hwndDlg, lc_str.copying_files);
                    SetWindowText(hwndLabel, lc_str.msg_copying_files);
                    break;
                }
                case ACTION_MOVE: {
                    SetWindowText(hwndDlg, lc_str.moving_files);
                    SetWindowText(hwndLabel, lc_str.msg_moving_files);
                    break;
                }
                case ACTION_ISO_EXTRACT: {
                    SetWindowText(hwndDlg, lc_str.extracting_files);
                    SetWindowText(hwndLabel, lc_str.msg_extracting_files);
                    break;
                }
                case ACTION_NONE:
                    return (INT_PTR)FALSE;
            }
            SetWindowText(GetDlgItem(hwndDlg, IDCANCEL), lc_str.cancel);
            return (INT_PTR)TRUE;
        }
        case WM_TIMER: {
            if (wParam == ID_EVENT_PRELOADER) animatePreloader();
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDCANCEL) {
                if (showConfirmDialog(hwndDlg, lc_str.cancel, lc_str.msg_cancel_file_operation)) {
                    actionData->cancel = true;
                }
            }
            break;
        }
        case MSG_CLOSE: {
            freeActionData();
            DestroyWindow(hwndDlg);
            hwndDlg = NULL;
            navigateRefresh();
            break;
        }
        case MSG_NAVIGATE_REFRESH: {
            navigateRefresh();
            break;
        }
        case MSG_PROGRESS: {
            SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETPOS, wParam, 0);
            break;
        }
    }

    return (INT_PTR)FALSE;
}

static void extractSingleISOFile(void* handle, bool isCDImage, iso9660_stat_t* isoStat, wchar_t* dstPath) {
    char filename[MAX_PATH] = {0};
    WideCharToMultiByte(CP_ACP, 0, dstPath, -1, filename, MAX_PATH, NULL, NULL);
    
    FILE* outFile = fopen(filename, "wb");
    if (!outFile) return;
    
    const uint32_t isoBlocks = CEILING(isoStat->total_size, ISO_BLOCKSIZE);
    for (int i = 0; i < isoBlocks; i++) {
        char buffer[ISO_BLOCKSIZE] = {0};
        const lsn_t lsn = isoStat->lsn + i;

        if (isCDImage) {
            if (cdio_read_data_sectors((CdIo_t*)handle, buffer, lsn, ISO_BLOCKSIZE, 1) != 0) goto end;
        }
        else if (iso9660_iso_seek_read((iso9660_t*)handle, buffer, lsn, 1) != ISO_BLOCKSIZE) goto end;

        fwrite(buffer, ISO_BLOCKSIZE, 1, outFile);
        if (ferror(outFile)) goto end;
    }
    
    fflush(outFile);
    ftruncate(fileno(outFile), isoStat->total_size);
    
end:    
    if (outFile) fclose(outFile);
}

static void extractAllISOFiles(void* handle, bool isCDImage, char* srcPath, wchar_t* dstPath) {
    CdioISO9660FileList_t* isoFileList = isCDImage ? iso9660_fs_readdir((CdIo_t*)handle, srcPath) : 
                                                     iso9660_ifs_readdir((iso9660_t*)handle, srcPath);
    if (!isoFileList) return;
    
    CdioListNode_t* isoNode;
    char srcName[MAX_PATH] = {0};
    wchar_t dstName[MAX_PATH] = {0};
    char fullSrcPath[MAX_PATH] = {0};
    wchar_t fullDstPath[MAX_PATH] = {0};
    
    int jolietLevel = isCDImage ? cdio_get_joliet_level((CdIo_t*)handle) : iso9660_ifs_get_joliet_level((iso9660_t*)handle);
    
    _CDIO_LIST_FOREACH(isoNode, isoFileList) {
        iso9660_stat_t* isoStat = (iso9660_stat_t*)_cdio_list_node_data(isoNode);
        if (strcmp(isoStat->filename, ".") == 0 || strcmp(isoStat->filename, "..") == 0) continue;
        
        memset(srcName, 0, MAX_PATH);
        iso9660_name_translate_ext(isoStat->filename, srcName, jolietLevel);
        
        joinUnixPaths(srcPath, srcName, fullSrcPath);
        
        MultiByteToWideChar(CP_ACP, 0, srcName, -1, dstName, MAX_PATH);
        joinPaths(dstPath, dstName, fullDstPath);
        
        if (isoStat->type == _STAT_DIR) {
            CreateDirectory(fullDstPath, NULL);
            extractAllISOFiles(handle, isCDImage, fullSrcPath, fullDstPath);
        }
        else if (isoStat->type == _STAT_FILE) {
            extractSingleISOFile(handle, isCDImage, isoStat, fullDstPath);
        }
    }

    iso9660_filelist_free(isoFileList);
}

// ---- Banner File Manager: native copy/move/delete (replaces shell32 SHFileOperation) ----
// Wine 的 shell32 SHFileOperation 复制/移动/删除在 Proton 10.0-4 下会崩溃。WFM 已经

// 跟踪自己的源路径剪贴板 + 目标目录，因此我们不需要 shell

// 语义——直接在 Win32 文件 API 上实现操作以绕过 shell32。


static bool bfmDeletePath(const wchar_t* path);
static bool bfmCopyPath(const wchar_t* src, const wchar_t* dst);

// CopyFileEx 进度回调：累积当前复制的字节数并将百分比发送到

// 对话框。同时支持取消。（移动/删除在任务循环中报告基于计数的进度。）

static DWORD CALLBACK copyProgress(LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
        LARGE_INTEGER StreamSize, LARGE_INTEGER StreamBytesTransferred, DWORD dwStreamNumber,
        DWORD dwCallbackReason, HANDLE hSourceFile, HANDLE hDestinationFile, LPVOID lpData) {
    (void)TotalFileSize; (void)StreamSize; (void)StreamBytesTransferred; (void)dwStreamNumber;
    (void)dwCallbackReason; (void)hSourceFile; (void)hDestinationFile; (void)lpData;

    if (g_activeAction && g_activeAction->action == ACTION_COPY) {
        g_activeAction->doneBytes += (TotalBytesTransferred.QuadPart - g_fileLastTransferred);
        g_fileLastTransferred = TotalBytesTransferred.QuadPart;
        postProgress();
    }
    if (g_activeAction && g_activeAction->cancel) return PROGRESS_CANCEL;
    return PROGRESS_CONTINUE;
}

static uint64_t computeFileOrDirSize(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        WIN32_FILE_ATTRIBUTE_DATA info = {0};
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
            LARGE_INTEGER sz;
            sz.LowPart = info.nFileSizeLow;
            sz.HighPart = info.nFileSizeHigh;
            return (uint64_t)sz.QuadPart;
        }
        return 0;
    }

    uint64_t total = 0;
    wchar_t pattern[MAX_PATH] = {0};
    swprintf_s(pattern, MAX_PATH, L"%ls\\*", path);
    WIN32_FIND_DATAW wfd = {0};
    HANDLE h = FindFirstFileW(pattern, &wfd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
            wchar_t child[MAX_PATH] = {0};
            swprintf_s(child, MAX_PATH, L"%ls\\%ls", path, wfd.cFileName);
            total += computeFileOrDirSize(child);
        }
        while (FindNextFileW(h, &wfd));
        FindClose(h);
    }
    return total;
}

static uint64_t computeTotalBytes(wchar_t** paths, int count) {
    uint64_t total = 0;
    for (int i = 0; i < count; i++) total += computeFileOrDirSize(paths[i]);
    return total;
}

static bool bfmDeleteDirectory(const wchar_t* dir) {
    wchar_t pattern[MAX_PATH] = {0};
    swprintf_s(pattern, MAX_PATH, L"%ls\\*", dir);

    WIN32_FIND_DATAW wfd = {0};
    HANDLE h = FindFirstFileW(pattern, &wfd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
            wchar_t child[MAX_PATH] = {0};
            swprintf_s(child, MAX_PATH, L"%ls\\%ls", dir, wfd.cFileName);
            bfmDeletePath(child);
        }
        while (FindNextFileW(h, &wfd));
        FindClose(h);
    }

    SetFileAttributesW(dir, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(dir);
}

static bool bfmDeletePath(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return bfmDeleteDirectory(path);

    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(path);
}

static bool bfmCopyDirectory(const wchar_t* src, const wchar_t* dst) {
    if (!CreateDirectoryW(dst, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;

    wchar_t pattern[MAX_PATH] = {0};
    swprintf_s(pattern, MAX_PATH, L"%ls\\*", src);

    WIN32_FIND_DATAW wfd = {0};
    HANDLE h = FindFirstFileW(pattern, &wfd);
    bool ok = true;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
            wchar_t s[MAX_PATH] = {0}, d[MAX_PATH] = {0};
            swprintf_s(s, MAX_PATH, L"%ls\\%ls", src, wfd.cFileName);
            swprintf_s(d, MAX_PATH, L"%ls\\%ls", dst, wfd.cFileName);
            if (!bfmCopyPath(s, d)) ok = false;
        }
        while (FindNextFileW(h, &wfd));
        FindClose(h);
    }
    return ok;
}

static bool bfmCopyPath(const wchar_t* src, const wchar_t* dst) {
    DWORD attr = GetFileAttributesW(src);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return bfmCopyDirectory(src, dst);

    // 带字节级进度 + 取消的文件复制（FALSE 标志 = 覆盖已有文件）。

    g_fileLastTransferred = 0;
    return CopyFileExW(src, dst, copyProgress, NULL, NULL, 0);
}

static bool bfmMovePath(const wchar_t* src, const wchar_t* dst) {
    // 快速路径：同卷重命名同时处理文件和整个目录树。

    if (MoveFileExW(src, dst, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) return true;
    // 回退（跨卷目录，或目标目录已存在）：复制目录树然后删除源。

    if (!bfmCopyPath(src, dst)) return false;
    return bfmDeletePath(src);
}

// 复制/移动项目的目标位置 = dstDir\basename(src)，与 shell32 的

// SHFileOperation(pTo=dir) 提供的“复制到目标目录”行为。

static void bfmJoinDest(const wchar_t* dstDir, const wchar_t* src, wchar_t* out) {
    const wchar_t* base = wcsrchr(src, L'\\');
    base = base ? base + 1 : src;
    swprintf_s(out, MAX_PATH, L"%ls\\%ls", dstDir, base);
}

static DWORD WINAPI fileActionTask(void* param) {
    struct ActionData* actionData = (struct ActionData*)param;

    g_activeAction = actionData;
    g_lastPct = -1;

    if (actionData->action == ACTION_ISO_EXTRACT) {
        wchar_t* srcPath = actionData->srcPaths[0];
        bool isCDImage = !hasFileExtension(srcPath, L"iso");
        
        char filename[MAX_PATH] = {0};
        WideCharToMultiByte(CP_ACP, 0, srcPath, -1, filename, MAX_PATH, NULL, NULL);

        if (isCDImage) {
            CdIo_t* cdio = cdio_open(filename, DRIVER_UNKNOWN);
            cdio_set_arg(cdio, "joliet-level", "1");
            extractAllISOFiles(cdio, true, "/", actionData->dstPath);
            cdio_destroy(cdio);
        }
        else {
            iso9660_t* iso = iso9660_open_ext(filename, ISO_EXTENSION_JOLIET);
            extractAllISOFiles(iso, false, "/", actionData->dstPath);
            iso9660_close(iso);
        }
    }
    else {
        DWORD lastTime = GetTickCount();

        // 复制通过 CopyFileEx 回调报告精确到字节的进度；预先计算总大小。

        if (actionData->action == ACTION_COPY) {
            actionData->totalBytes = computeTotalBytes(actionData->srcPaths, actionData->numSrcPaths);
            actionData->doneBytes = 0;
        }

        for (int i = 0; i < actionData->numSrcPaths && !actionData->cancel; i++) {
            wchar_t* src = actionData->srcPaths[i];
            if (actionData->action == ACTION_DELETE) {
                if (!bfmDeletePath(src)) break;
            }
            else if (actionData->action == ACTION_COPY || actionData->action == ACTION_MOVE) {
                wchar_t dst[MAX_PATH] = {0};
                bfmJoinDest(actionData->dstPath, src, dst);

                // 粘贴到同一目录在这里是空操作（shell 会创建“副本”）；直接跳过。

                if (_wcsicmp(src, dst) == 0) continue;

                bool ok = (actionData->action == ACTION_COPY) ? bfmCopyPath(src, dst)
                                                              : bfmMovePath(src, dst);
                if (!ok) break;
            }

            // 移动/删除：基于计数的进度（复制通过回调基于字节）。

            if (actionData->action != ACTION_COPY) {
                PostMessage(hwndDlg, MSG_PROGRESS, (WPARAM)((i + 1) * 100 / actionData->numSrcPaths), 0);
            }

            DWORD currTime = GetTickCount();
            if ((currTime - lastTime) >= 3000) {
                SendMessage(hwndDlg, MSG_NAVIGATE_REFRESH, 0, 0);
                lastTime = currTime;
            }
        }
    }
    
    SendMessage(hwndDlg, MSG_CLOSE, 0, 0);
    return 0;
}

static wchar_t** createPathsFromFileNodes(struct FileNode** nodes, int count) {
    wchar_t** paths = calloc(count, sizeof(wchar_t*));
    
    wchar_t tmp[MAX_PATH] = {0};
    for (int i = 0; i < count; i++) {
        getFileNodePath(nodes[i], tmp);
        int len = wcslen(tmp);
        wchar_t* path = calloc(len + 2, sizeof(wchar_t));
        wcscpy_s(path, len + 1, tmp);
        path[len+0] = L'\0';
        path[len+1] = L'\0';         
        paths[i] = path;
    }
    
    return paths;
}

void deleteFiles(struct FileNode** nodes, int count) {
    wchar_t msg[128] = {0};
    if (count == 1) {
        swprintf_s(msg, 128, lc_str.msg_confirm_delete_item, nodes[0]->name);
    }
    else swprintf_s(msg, 128, lc_str.msg_confirm_delete_multiple_items, count);

    if (showConfirmDialog(hwndMain, lc_str.confirm_delete, msg)) {
        actionData = calloc(1, sizeof(struct ActionData));
        
        actionData->srcPaths = createPathsFromFileNodes(nodes, count);
        actionData->numSrcPaths = count;
        actionData->action = ACTION_DELETE;
        
        hwndDlg = CreateDialogParam(globalHInstance, MAKEINTRESOURCE(IDD_FILE_ACTION), hwndMain, &FileActionDialogProc, 0);     
        SetTimer(hwndDlg, ID_EVENT_PRELOADER, PRELOADER_PERIOD, NULL);
        CreateThread(NULL, 0, fileActionTask, actionData, 0, NULL);
        ShowWindow(hwndDlg, SW_SHOW);
    }
}

void copyFiles(struct FileNode** nodes, int count) {
    clearClipboard();
    clipboard = createPathsFromFileNodes(nodes, count);
    clipboardSize = count;
    clipboardIsCut = false;
}

void cutFiles(struct FileNode** nodes, int count) {
    clearClipboard();
    clipboard = createPathsFromFileNodes(nodes, count);
    clipboardSize = count;
    clipboardIsCut = true;
}

void pasteFiles(wchar_t* dstDir) {
    if (clipboardSize == 0) return; 
    
    actionData = calloc(1, sizeof(struct ActionData));
    
    int len = wcslen(dstDir);
    actionData->dstPath = calloc(len + 2, sizeof(wchar_t));
    wcscpy_s(actionData->dstPath, len + 1, dstDir);
    actionData->dstPath[len+0] = L'\0';
    actionData->dstPath[len+1] = L'\0';
    
    actionData->action = clipboardIsCut ? ACTION_MOVE : ACTION_COPY;
    actionData->srcPaths = clipboard;
    actionData->numSrcPaths = clipboardSize;
    
    hwndDlg = CreateDialogParam(globalHInstance, MAKEINTRESOURCE(IDD_FILE_ACTION), hwndMain, &FileActionDialogProc, 0);     
    SetTimer(hwndDlg, ID_EVENT_PRELOADER, PRELOADER_PERIOD, NULL);
    CreateThread(NULL, 0, fileActionTask, actionData, 0, NULL);
    ShowWindow(hwndDlg, SW_SHOW);   
}

static void createShortcut(wchar_t* srcPath, wchar_t* dstPath) {
    HRESULT hres;

    IShellLinkW* isl;
    hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (LPVOID*)&isl);
    if (SUCCEEDED(hres)) {
        wchar_t workingDir[MAX_PATH] = {0};
        getParentDirFromPath(srcPath, workingDir);
        
        IShellLinkW_SetPath(isl, srcPath);
        IShellLinkW_SetWorkingDirectory(isl, workingDir);
        IShellLinkW_SetDescription(isl, L"");
    
        IPersistFile* ipf;
        hres = IShellLinkW_QueryInterface(isl, &IID_IPersistFile, (void **)&ipf);

        if (SUCCEEDED(hres)) {
            IPersistFile_Save(ipf, dstPath, TRUE);
            IPersistFile_Release(ipf);
        }
        
        IShellLinkW_Release(isl);
    }
}

void pasteShortcuts(wchar_t* dstDir) {
    if (clipboardSize == 0) return;
    
    wchar_t dstPath[MAX_PATH] = {0};
    wchar_t basename[80] = {0};
    
    for (int i = 0; i < clipboardSize; i++) {
        wchar_t* srcPath = clipboard[i];
        if (wcscmp(srcPath, L".lnk") != 0) {
            getBasenameFromPath(srcPath, basename, true);
            swprintf_s(dstPath, MAX_PATH, L"%ls\\%ls.lnk", dstDir, basename);
            createShortcut(srcPath, dstPath);           
        }
    }
    
    if (clipboardIsCut) clearClipboard();
    navigateRefresh();
}

void createDesktopShortcuts(struct FileNode** nodes, int count) {
    wchar_t** srcPaths = createPathsFromFileNodes(nodes, count);
    wchar_t* desktopPath = getDesktopPath();
    wchar_t dstPath[MAX_PATH] = {0};
    wchar_t basename[80] = {0};
    
    for (int i = 0; i < count; i++) {
        wchar_t* srcPath = srcPaths[i];
        if (wcscmp(srcPath, L".lnk") != 0) {
            getBasenameFromPath(srcPath, basename, true);
            swprintf_s(dstPath, MAX_PATH, L"%ls\\%ls.lnk", desktopPath, basename);
            createShortcut(srcPath, dstPath);           
        }
        free(srcPaths[i]);
    }
    
    free(srcPaths); 
}

void extractFilesFromISOImage(wchar_t* isoPath, wchar_t* dstPath) {
    actionData = calloc(1, sizeof(struct ActionData));
    
    actionData->srcPaths = calloc(1, sizeof(wchar_t*));
    actionData->srcPaths[0] = wcsdup(isoPath);

    actionData->dstPath = wcsdup(dstPath);
    actionData->action = ACTION_ISO_EXTRACT;
    
    hwndDlg = CreateDialogParam(globalHInstance, MAKEINTRESOURCE(IDD_FILE_ACTION), hwndMain, &FileActionDialogProc, 0);     
    SetTimer(hwndDlg, ID_EVENT_PRELOADER, PRELOADER_PERIOD, NULL);
    CreateThread(NULL, 0, fileActionTask, actionData, 0, NULL);
    ShowWindow(hwndDlg, SW_SHOW);
}