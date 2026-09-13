#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdbool.h>

// 统一配置存储在 HKCU\Software\WFM\<section>\<key> 下

// 所有持久化设置（主题、收藏、视图状态、标签页）都经过这里。


#define CONFIG_ROOT L"Software\\WFM"

int    cfgGetInt(const wchar_t* section, const wchar_t* key, int def);
void   cfgSetInt(const wchar_t* section, const wchar_t* key, int val);
bool   cfgGetStr(const wchar_t* section, const wchar_t* key, wchar_t* buf, int bufLen, const wchar_t* def);
void   cfgSetStr(const wchar_t* section, const wchar_t* key, const wchar_t* val);
bool   cfgDeleteKey(const wchar_t* section, const wchar_t* key);
bool   cfgDeleteSection(const wchar_t* section);

#endif
