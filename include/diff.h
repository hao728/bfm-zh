#ifndef DIFF_H
#define DIFF_H

#include <windows.h>
#include <stdbool.h>

// LCS diff 产生的行分类。

typedef enum {
    DIFF_EQUAL = 0,
    DIFF_LEFT_ONLY = 1,   // line exists only in the left file
    DIFF_RIGHT_ONLY = 2   // line exists only in the right file
} DiffLineType;

typedef struct {
    wchar_t* text;        // owned; freed by diffFree
    DiffLineType type;
    int leftIdx;          // 0-based line number in left file, -1 if absent
    int rightIdx;         // 0-based line number in right file, -1 if absent
} DiffLine;

typedef struct {
    DiffLine* lines;
    int count;
    int leftOnly;
    int rightOnly;
    int common;
} DiffResult;

// 释放 diffFiles 产生的 DiffResult。

void diffFree(DiffResult* r);

// 使用 LCS（最长公共子序列）计算两个文本文件的逐行差异。

// 读取错误或任一文件超过 DIFF_MAX_LINES 时返回 false。

bool diffFiles(const wchar_t* leftPath, const wchar_t* rightPath, DiffResult* out);

// 为两个文本文件显示模态 diff 查看器对话框。

void diffShowDialog(HWND parent, const wchar_t* leftPath, const wchar_t* rightPath);

#endif
