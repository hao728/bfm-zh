#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdbool.h>

// Unified config storage under HKCU\Software\WFM\<section>\<key>
// All persistent settings (theme, favorites, view state, tabs) go through here.

#define CONFIG_ROOT L"Software\\WFM"

int    cfgGetInt(const wchar_t* section, const wchar_t* key, int def);
void   cfgSetInt(const wchar_t* section, const wchar_t* key, int val);
bool   cfgGetStr(const wchar_t* section, const wchar_t* key, wchar_t* buf, int bufLen, const wchar_t* def);
void   cfgSetStr(const wchar_t* section, const wchar_t* key, const wchar_t* val);
bool   cfgDeleteKey(const wchar_t* section, const wchar_t* key);
bool   cfgDeleteSection(const wchar_t* section);

#endif
