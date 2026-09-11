#ifndef DIFF_H
#define DIFF_H

#include <windows.h>
#include <stdbool.h>

// Line classification produced by the LCS diff.
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

// Free a DiffResult produced by diffFiles.
void diffFree(DiffResult* r);

// Compute a line-by-line diff of two text files using LCS.
// Returns false on read error or if either file exceeds DIFF_MAX_LINES.
bool diffFiles(const wchar_t* leftPath, const wchar_t* rightPath, DiffResult* out);

// Show a modal diff viewer dialog for two text files.
void diffShowDialog(HWND parent, const wchar_t* leftPath, const wchar_t* rightPath);

#endif
