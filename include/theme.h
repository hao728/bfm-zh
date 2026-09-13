#ifndef THEME_H
#define THEME_H

#include <windows.h>
#include <stdbool.h>

// 主题模式：0=亮色（跟随系统），1=暗色，2=自定义

#define THEME_LIGHT   0
#define THEME_DARK    1
#define THEME_CUSTOM  2

void  themeInit(void);
int   themeGetMode(void);
void  themeSetMode(int mode);
bool  isDarkMode(void);

// 核心调色板——所有自绘代码必须通过这些颜色，绝不硬编码 RGB。

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

// 自定义颜色覆盖（仅在 mode == THEME_CUSTOM 时使用）

void themeSetCustom(COLORREF faceBg, COLORREF faceText, COLORREF accent);
void themeResetCustom(void);

#endif
