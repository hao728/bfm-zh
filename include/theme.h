#ifndef THEME_H
#define THEME_H

#include <windows.h>
#include <stdbool.h>

// Theme mode: 0=light (follow system), 1=dark, 2=custom
#define THEME_LIGHT   0
#define THEME_DARK    1
#define THEME_CUSTOM  2

void  themeInit(void);
int   themeGetMode(void);
void  themeSetMode(int mode);
bool  isDarkMode(void);

// Core palette — all custom-drawing code must go through these, never hardcode RGB.
COLORREF themeFaceBg(void);      // window / panel background
COLORREF themeFaceText(void);    // primary text on face bg
COLORREF themeFaceLine(void);    // borders / separators
COLORREF themeFieldBg(void);     // input fields / list background
COLORREF themeFieldText(void);   // text inside fields
COLORREF themePlaceholder(void); // grayed hint text
COLORREF themeAccent(void);      // highlight / selection / active pane
COLORREF themeAccentText(void);  // text on accent background
COLORREF themeHover(void);       // hover row background
COLORREF themeAltRow(void);      // alternating row background

// Custom color overrides (only used when mode == THEME_CUSTOM)
void themeSetCustom(COLORREF faceBg, COLORREF faceText, COLORREF accent);
void themeResetCustom(void);

#endif
