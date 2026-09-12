#include "main.h"
#include "diff.h"

#define DIFF_MAX_LINES 2000

// ---------- file -> line array ----------
static wchar_t** readLines(const wchar_t* path, int* outCount) {
    *outCount = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); return NULL; }
    char* buf = (char*)malloc(sz + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD rd = 0;
    ReadFile(h, buf, sz, &rd, NULL);
    CloseHandle(h);
    buf[rd] = 0;
    // Detect UTF-8 BOM or plain; treat as UTF-8 (Wine text files are usually UTF-8 or ASCII).
    int skip = 0;
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) skip = 3;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf + skip, rd - skip, NULL, 0);
    if (wlen <= 0) { free(buf); return NULL; }
    wchar_t* wbuf = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
    if (!wbuf) { free(buf); return NULL; }
    MultiByteToWideChar(CP_UTF8, 0, buf + skip, rd - skip, wbuf, wlen);
    wbuf[wlen] = 0;
    free(buf);

    // Split into lines (handle \r\n, \n, \r)
    int cap = 256, cnt = 0;
    wchar_t** lines = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!lines) { free(wbuf); return NULL; }
    wchar_t* p = wbuf;
    while (*p) {
        wchar_t* start = p;
        while (*p && *p != L'\n' && *p != L'\r') p++;
        int len = (int)(p - start);
        wchar_t* line = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
        if (line) {
            memcpy(line, start, len * sizeof(wchar_t));
            line[len] = 0;
            if (cnt >= cap) { cap *= 2; lines = (wchar_t**)realloc(lines, cap * sizeof(wchar_t*)); }
            lines[cnt++] = line;
        }
        if (*p == L'\r' && *(p + 1) == L'\n') p += 2;
        else if (*p) p++;
    }
    free(wbuf);
    *outCount = cnt;
    return lines;
}

static void freeLines(wchar_t** lines, int n) {
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
}

// ---------- LCS diff ----------
void diffFree(DiffResult* r) {
    if (!r || !r->lines) return;
    for (int i = 0; i < r->count; i++) free(r->lines[i].text);
    free(r->lines);
    r->lines = NULL;
    r->count = 0;
}

bool diffFiles(const wchar_t* leftPath, const wchar_t* rightPath, DiffResult* out) {
    memset(out, 0, sizeof(*out));
    int ln = 0, rn = 0;
    wchar_t** L = readLines(leftPath, &ln);
    wchar_t** R = readLines(rightPath, &rn);
    if (!L || !R) { freeLines(L, ln); freeLines(R, rn); return false; }
    if (ln > DIFF_MAX_LINES || rn > DIFF_MAX_LINES) {
        freeLines(L, ln); freeLines(R, rn); return false;
    }

    // DP table: dp[i][j] = LCS length of L[0..i-1] and R[0..j-1]
    int* dp = (int*)calloc((ln + 1) * (rn + 1), sizeof(int));
    if (!dp) { freeLines(L, ln); freeLines(R, rn); return false; }
    for (int i = 1; i <= ln; i++) {
        for (int j = 1; j <= rn; j++) {
            if (wcscmp(L[i - 1], R[j - 1]) == 0)
                dp[i * (rn + 1) + j] = dp[(i - 1) * (rn + 1) + (j - 1)] + 1;
            else {
                int a = dp[(i - 1) * (rn + 1) + j];
                int b = dp[i * (rn + 1) + (j - 1)];
                dp[i * (rn + 1) + j] = a > b ? a : b;
            }
        }
    }

    // Backtrack to build aligned sequence (in reverse, then reverse)
    int cap = ln + rn + 2;
    DiffLine* rev = (DiffLine*)malloc(cap * sizeof(DiffLine));
    int rc = 0;
    int i = ln, j = rn;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && wcscmp(L[i - 1], R[j - 1]) == 0) {
            rev[rc].text = _wcsdup(L[i - 1]);
            rev[rc].type = DIFF_EQUAL;
            rev[rc].leftIdx = i - 1;
            rev[rc].rightIdx = j - 1;
            rc++; i--; j--;
        } else if (j > 0 && (i == 0 || dp[i * (rn + 1) + (j - 1)] >= dp[(i - 1) * (rn + 1) + j])) {
            rev[rc].text = _wcsdup(R[j - 1]);
            rev[rc].type = DIFF_RIGHT_ONLY;
            rev[rc].leftIdx = -1;
            rev[rc].rightIdx = j - 1;
            rc++; j--;
        } else {
            rev[rc].text = _wcsdup(L[i - 1]);
            rev[rc].type = DIFF_LEFT_ONLY;
            rev[rc].leftIdx = i - 1;
            rev[rc].rightIdx = -1;
            rc++; i--;
        }
    }
    free(dp);
    freeLines(L, ln);
    freeLines(R, rn);

    // Reverse into final order
    out->lines = (DiffLine*)malloc(rc * sizeof(DiffLine));
    out->count = rc;
    for (int k = 0; k < rc; k++) {
        out->lines[k] = rev[rc - 1 - k];
        if (out->lines[k].type == DIFF_EQUAL) out->common++;
        else if (out->lines[k].type == DIFF_LEFT_ONLY) out->leftOnly++;
        else out->rightOnly++;
    }
    free(rev);
    return true;
}

// ---------- diff viewer window (dynamically created, no resource needed) ----------
#define IDC_DIFF_LIST 500
#define IDC_DIFF_CLOSE 501

static DiffResult* g_diffResult = NULL;
static HFONT g_diffFont = NULL;
static HWND g_diffList = NULL;

static LRESULT CALLBACK DiffWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_diffList = CreateWindowEx(0, WC_LISTVIEWW, NULL,
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_OWNERDRAWFIXED | WS_BORDER | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_DIFF_LIST, globalHInstance, NULL);
        ListView_SetExtendedListViewStyle(g_diffList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = lc_str.diff_left_label;
        col.cx = 380;
        ListView_InsertColumn(g_diffList, 0, &col);
        col.pszText = lc_str.diff_right_label;
        ListView_InsertColumn(g_diffList, 1, &col);

        if (g_diffResult) {
            for (int i = 0; i < g_diffResult->count; i++) {
                LVITEMW item = {0};
                item.mask = LVIF_TEXT | LVIF_PARAM;
                item.iItem = i;
                item.iSubItem = 0;
                item.pszText = (g_diffResult->lines[i].type == DIFF_RIGHT_ONLY) ? L"" : g_diffResult->lines[i].text;
                item.lParam = (LPARAM)g_diffResult->lines[i].type;
                ListView_InsertItem(g_diffList, &item);
                if (g_diffResult->lines[i].type != DIFF_LEFT_ONLY)
                    ListView_SetItemText(g_diffList, i, 1, g_diffResult->lines[i].text);
            }
        }
        g_diffFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);
        SendMessage(g_diffList, WM_SETFONT, (WPARAM)g_diffFont, TRUE);

        CreateWindowEx(0, L"BUTTON", lc_str.proc_close, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                       0, 0, 0, 0, hwnd, (HMENU)IDC_DIFF_CLOSE, globalHInstance, NULL);
        return 0;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int btnH = 30;
        SetWindowPos(g_diffList, NULL, 8, 8, rc.right - 16, rc.bottom - btnH - 20, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, IDC_DIFF_CLOSE), NULL,
                     rc.right - 100, rc.bottom - btnH - 8, 88, btnH, SWP_NOZORDER);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType != ODT_LISTVIEW) break;
        DiffLineType t = (DiffLineType)dis->itemData;
        COLORREF bg = RGB(255, 255, 255);
        COLORREF fg = RGB(0, 0, 0);
        if (t == DIFF_LEFT_ONLY) { bg = RGB(255, 220, 220); fg = RGB(120, 0, 0); }
        else if (t == DIFF_RIGHT_ONLY) { bg = RGB(220, 255, 220); fg = RGB(0, 80, 0); }
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);
        SetTextColor(dis->hDC, fg);
        SetBkMode(dis->hDC, TRANSPARENT);
        for (int sub = 0; sub < 2; sub++) {
            RECT rc;
            ListView_GetSubItemRect(dis->hwndItem, dis->itemID, sub, LVIR_BOUNDS, &rc);
            rc.left += 4;
            wchar_t buf[1024] = {0};
            LVITEMW li = {0};
            li.mask = LVIF_TEXT;
            li.iItem = dis->itemID;
            li.iSubItem = sub;
            li.pszText = buf;
            li.cchTextMax = 1023;
            ListView_GetItem(dis->hwndItem, &li);
            HFONT old = (HFONT)SelectObject(dis->hDC, g_diffFont);
            DrawTextW(dis->hDC, buf, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dis->hDC, old);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_DIFF_CLOSE) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_diffList = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static const wchar_t DIFF_CLASS[] = L"WfmDiffViewer";

void diffShowDialog(HWND parent, const wchar_t* leftPath, const wchar_t* rightPath) {
    DiffResult r;
    if (!diffFiles(leftPath, rightPath, &r)) {
        MessageBoxW(parent, lc_str.err_diff_read,
                    lc_str.diff_files, MB_OK | MB_ICONWARNING);
        return;
    }
    g_diffResult = &r;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DiffWndProc;
    wc.hInstance = globalHInstance;
    wc.lpszClassName = DIFF_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    wchar_t title[256];
    swprintf_s(title, 256, L"Diff  -  Same: %d   Left only: %d   Right only: %d",
               r.common, r.leftOnly, r.rightOnly);

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, DIFF_CLASS, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 560,
        parent, NULL, globalHInstance, NULL);

    // Modal loop
    EnableWindow(parent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hwnd)) break;
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    g_diffResult = NULL;
    diffFree(&r);
    UnregisterClassW(DIFF_CLASS, globalHInstance);
}
