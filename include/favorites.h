#ifndef FAVORITES_H
#define FAVORITES_H

#include <windows.h>
#include <stdbool.h>

#define FAV_MAX 32

int  favCount(void);
int  favGetAll(wchar_t paths[FAV_MAX][MAX_PATH]);
bool favAdd(const wchar_t* path);
bool favRemoveAt(int index);
bool favContains(const wchar_t* path);
void favRefreshTree(void);  // rebuild the Favorites branch in the left tree

#endif
