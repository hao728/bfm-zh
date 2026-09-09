#include "theme.h"
#include "config.h"

static int g_mode = THEME_LIGHT;
static COLORREF g_customFaceBg = 0;
static COLORREF g_customFaceText = 0;
static COLORREF g_customAccent = 0;
static bool g_customSet = false;

void themeInit(void) {
    g_mode = cfgGetInt(L"Theme", L"Mode", THEME_LIGHT);
    if (g_mode == THEME_CUSTOM) {
        g_customFaceBg = (COLORREF)cfgGetInt(L"Theme", L"FaceBg", RGB(245, 245, 245));
        g_customFaceText = (COLORREF)cfgGetInt(L"Theme", L"FaceText", RGB(20, 20, 20));
        g_customAccent = (COLORREF)cfgGetInt(L"Theme", L"Accent", RGB(0, 120, 215));
        g_customSet = true;
    }
}

int themeGetMode(void) { return g_mode; }

void themeSetMode(int mode) {
    g_mode = mode;
    cfgSetInt(L"Theme", L"Mode", mode);
}

bool isDarkMode(void) {
    if (g_mode == THEME_DARK) return true;
    if (g_mode == THEME_CUSTOM && g_customSet) {
        // Heuristic: if custom face bg is dark, treat as dark
        int r = GetRValue(g_customFaceBg);
        int g = GetGValue(g_customFaceBg);
        int b = GetBValue(g_customFaceBg);
        int luminance = (r * 299 + g * 587 + b * 114) / 1000;
        return luminance < 128;
    }
    // Light mode: check Windows system dark theme (Wine usually returns light)
    HKEY hKey;
    DWORD val = 0, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return val == 0;
        }
        RegCloseKey(hKey);
    }
    return false;
}

COLORREF themeFaceBg(void) {
    if (g_mode == THEME_CUSTOM && g_customSet) return g_customFaceBg;
    return isDarkMode() ? RGB(45, 45, 45) : GetSysColor(COLOR_BTNFACE);
}

COLORREF themeFaceText(void) {
    if (g_mode == THEME_CUSTOM && g_customSet) return g_customFaceText;
    return isDarkMode() ? RGB(225, 225, 225) : GetSysColor(COLOR_BTNTEXT);
}

COLORREF themeFaceLine(void) {
    return isDarkMode() ? RGB(70, 70, 70) : GetSysColor(COLOR_BTNSHADOW);
}

COLORREF themeFieldBg(void) {
    if (g_mode == THEME_CUSTOM && g_customSet) return g_customFaceBg;
    return isDarkMode() ? RGB(45, 45, 45) : GetSysColor(COLOR_WINDOW);
}

COLORREF themeFieldText(void) {
    if (g_mode == THEME_CUSTOM && g_customSet) return g_customFaceText;
    return isDarkMode() ? RGB(230, 230, 230) : GetSysColor(COLOR_WINDOWTEXT);
}

COLORREF themePlaceholder(void) {
    return isDarkMode() ? RGB(150, 150, 150) : GetSysColor(COLOR_GRAYTEXT);
}

COLORREF themeAccent(void) {
    if (g_mode == THEME_CUSTOM && g_customSet) return g_customAccent;
    return RGB(0, 120, 215);
}

COLORREF themeAccentText(void) {
    return RGB(255, 255, 255);
}

COLORREF themeHover(void) {
    return isDarkMode() ? RGB(60, 60, 60) : RGB(229, 241, 251);
}

COLORREF themeAltRow(void) {
    return isDarkMode() ? RGB(50, 50, 50) : RGB(248, 248, 248);
}

void themeSetCustom(COLORREF faceBg, COLORREF faceText, COLORREF accent) {
    g_mode = THEME_CUSTOM;
    g_customFaceBg = faceBg;
    g_customFaceText = faceText;
    g_customAccent = accent;
    g_customSet = true;
    cfgSetInt(L"Theme", L"Mode", THEME_CUSTOM);
    cfgSetInt(L"Theme", L"FaceBg", (int)faceBg);
    cfgSetInt(L"Theme", L"FaceText", (int)faceText);
    cfgSetInt(L"Theme", L"Accent", (int)accent);
}

void themeResetCustom(void) {
    g_customSet = false;
    g_mode = THEME_LIGHT;
    cfgSetInt(L"Theme", L"Mode", THEME_LIGHT);
}
