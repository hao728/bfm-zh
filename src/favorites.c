#include "main.h"
#include "favorites.h"

#define FAV_SECTION L"Favorites"
#define FAV_COUNT_KEY L"Count"

int favCount(void) {
    int n = cfgGetInt(FAV_SECTION, FAV_COUNT_KEY, 0);
    if (n < 0) n = 0;
    if (n > FAV_MAX) n = FAV_MAX;
    return n;
}

int favGetAll(wchar_t paths[FAV_MAX][MAX_PATH]) {
    int n = favCount();
    wchar_t key[16];
    for (int i = 0; i < n; i++) {
        swprintf_s(key, 16, L"%d", i);
        paths[i][0] = L'\0';
        cfgGetStr(FAV_SECTION, key, paths[i], MAX_PATH, L"");
    }
    return n;
}

bool favContains(const wchar_t* path) {
    if (!path) return false;
    wchar_t paths[FAV_MAX][MAX_PATH];
    int n = favGetAll(paths);
    for (int i = 0; i < n; i++) {
        if (_wcsicmp(paths[i], path) == 0) return true;
    }
    return false;
}

bool favAdd(const wchar_t* path) {
    if (!path || !path[0]) return false;
    if (favContains(path)) return false;  // no duplicates
    int n = favCount();
    if (n >= FAV_MAX) return false;
    wchar_t key[16];
    swprintf_s(key, 16, L"%d", n);
    cfgSetStr(FAV_SECTION, key, path);
    cfgSetInt(FAV_SECTION, FAV_COUNT_KEY, n + 1);
    favRefreshTree();
    return true;
}

bool favRemoveAt(int index) {
    int n = favCount();
    if (index < 0 || index >= n) return false;
    wchar_t paths[FAV_MAX][MAX_PATH];
    favGetAll(paths);
    // shift later entries down
    for (int i = index; i < n - 1; i++) {
        wcscpy_s(paths[i], MAX_PATH, paths[i + 1]);
    }
    // rewrite
    for (int i = 0; i < n - 1; i++) {
        wchar_t key[16];
        swprintf_s(key, 16, L"%d", i);
        cfgSetStr(FAV_SECTION, key, paths[i]);
    }
    wchar_t lastKey[16];
    swprintf_s(lastKey, 16, L"%d", n - 1);
    cfgDeleteKey(FAV_SECTION, lastKey);
    cfgSetInt(FAV_SECTION, FAV_COUNT_KEY, n - 1);
    favRefreshTree();
    return true;
}
