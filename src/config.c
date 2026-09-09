#include "config.h"
#include <string.h>

static HKEY openSection(const wchar_t* section, bool create) {
    wchar_t path[256] = {0};
    wcscpy_s(path, 256, CONFIG_ROOT);
    if (section && section[0]) {
        wcscat_s(path, 256, L"\\");
        wcscat_s(path, 256, section);
    }
    HKEY hKey = NULL;
    if (create) {
        RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    } else {
        RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hKey);
    }
    return hKey;
}

int cfgGetInt(const wchar_t* section, const wchar_t* key, int def) {
    HKEY hKey = openSection(section, false);
    if (!hKey) return def;
    DWORD val = 0, sz = sizeof(val);
    LRESULT r = RegQueryValueExW(hKey, key, NULL, NULL, (LPBYTE)&val, &sz);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS) ? (int)val : def;
}

void cfgSetInt(const wchar_t* section, const wchar_t* key, int val) {
    HKEY hKey = openSection(section, true);
    if (!hKey) return;
    RegSetValueExW(hKey, key, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
}

bool cfgGetStr(const wchar_t* section, const wchar_t* key, wchar_t* buf, int bufLen, const wchar_t* def) {
    HKEY hKey = openSection(section, false);
    if (!hKey) {
        if (def && buf) wcscpy_s(buf, bufLen, def);
        return false;
    }
    DWORD sz = (DWORD)(bufLen * sizeof(wchar_t));
    LRESULT r = RegQueryValueExW(hKey, key, NULL, NULL, (LPBYTE)buf, &sz);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS) {
        if (def && buf) wcscpy_s(buf, bufLen, def);
        return false;
    }
    return true;
}

void cfgSetStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) {
    HKEY hKey = openSection(section, true);
    if (!hKey) return;
    RegSetValueExW(hKey, key, 0, REG_SZ, (const BYTE*)val, (wcslen(val) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);
}

bool cfgDeleteKey(const wchar_t* section, const wchar_t* key) {
    HKEY hKey = openSection(section, false);
    if (!hKey) return false;
    LRESULT r = RegDeleteValueW(hKey, key);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool cfgDeleteSection(const wchar_t* section) {
    wchar_t path[256] = {0};
    wcscpy_s(path, 256, CONFIG_ROOT);
    if (section && section[0]) {
        wcscat_s(path, 256, L"\\");
        wcscat_s(path, 256, section);
    }
    return RegDeleteKeyW(HKEY_CURRENT_USER, path) == ERROR_SUCCESS;
}
