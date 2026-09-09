#include "main.h"

extern HINSTANCE globalHInstance;
extern HWND hwndMain;

HWND hwndStatusbar = NULL;

static WNDPROC OrigStatusProc = NULL;
static wchar_t statusParts[4][80] = {0};

static LRESULT CALLBACK StatusWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

        // 4 parts: items, size, memory, free space
        int partWidths[4];
        int total = rc.right - rc.left;
        partWidths[0] = total * 0.16;  // items
        partWidths[1] = total * 0.16;  // size
        partWidths[2] = total * 0.34;  // memory (wider)
        partWidths[3] = total - partWidths[0] - partWidths[1] - partWidths[2]; // free space

        int x = rc.left;
        for (int i = 0; i < 4; i++) {
            RECT tr = {x + 8, rc.top, x + partWidths[i] - 4, rc.bottom};
            DrawTextW(hdc, statusParts[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            // Vertical separator
            if (i < 3) {
                HPEN pen = CreatePen(PS_SOLID, 1, themeFaceLine());
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                MoveToEx(hdc, x + partWidths[i], rc.top + 3, NULL);
                LineTo(hdc, x + partWidths[i], rc.bottom - 3);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
            }
            x += partWidths[i];
        }

        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return CallWindowProc(OrigStatusProc, hwnd, msg, wParam, lParam);
}

void setStatusbarText(wchar_t* text) {
    // Backward compat: put all text in part 0
    wcscpy_s(statusParts[0], 80, text ? text : L"");
    statusParts[1][0] = statusParts[2][0] = statusParts[3][0] = L'\0';
    InvalidateRect(hwndStatusbar, NULL, TRUE);
}

void setStatusbarParts(wchar_t* p0, wchar_t* p1, wchar_t* p2, wchar_t* p3) {
    if (p0) wcscpy_s(statusParts[0], 80, p0); else statusParts[0][0] = 0;
    if (p1) wcscpy_s(statusParts[1], 80, p1); else statusParts[1][0] = 0;
    if (p2) wcscpy_s(statusParts[2], 80, p2); else statusParts[2][0] = 0;
    if (p3) wcscpy_s(statusParts[3], 80, p3); else statusParts[3][0] = 0;
    InvalidateRect(hwndStatusbar, NULL, TRUE);
}

void createStatusbar() {
    hwndStatusbar = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS,
                                   0, 0, 0, 0, hwndMain, (HMENU)NULL, globalHInstance, NULL);
    SendMessage(hwndStatusbar, WM_SETFONT, (WPARAM)getUIFont(), TRUE);
    OrigStatusProc = (WNDPROC)SetWindowLongPtr(hwndStatusbar, GWLP_WNDPROC, (LONG_PTR)StatusWndProc);
    UpdateWindow(hwndStatusbar);
}
