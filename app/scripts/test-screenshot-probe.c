/* SPDX-License-Identifier: GPL-2.0-only */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../guscio/screenshot.h"

static char *utf8(const wchar_t *s)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL,
                                0, NULL, NULL);
    char *p;
    if (!n) return NULL;
    p = (char *)malloc((size_t)n);
    if (!p || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, p,
                                   n, NULL, NULL)) { free(p); return NULL; }
    return p;
}

int wmain(int argc, wchar_t **argv)
{
    char *adb;
    wchar_t message[256];
    ULONGLONG deadline;
    int result, port = 17555;
    if (argc != 4 && argc != 5) return 2;
    if (argc == 5) {
        wchar_t *end;
        long value = wcstol(argv[4], &end, 10);
        if (!argv[4][0] || *end || value < 1 || value > 65535) return 2;
        port = (int)value;
    }
    adb = utf8(argv[2]);
    if (!adb) return 2;
    if (wcscmp(argv[1], L"existing") == 0) {
        result = screenshot_start(adb, port, argv[3]);
        free(adb);
        return result ? 3 : 0;
    }
    if (!screenshot_start(adb, port, argv[3])) { free(adb); return 3; }
    free(adb);
    if (screenshot_start("invalid", port, argv[3])) return 4;
    if (wcscmp(argv[1], L"cancel") == 0) {
        Sleep(500);
        if (screenshot_stop()) return 5;
    }
    deadline = GetTickCount64() + 25000;
    do {
        result = screenshot_poll(message, 256);
        if (result != 0) break;
        Sleep(20);
    } while (GetTickCount64() < deadline);
    if (result == 0) { screenshot_stop(); return 6; }
    if (wcscmp(argv[1], L"saved") == 0) return result == 1 ? 0 : 7;
    if (wcscmp(argv[1], L"cancel") == 0) return result < 0 ? 0 : 8;
    return result < 0 ? 0 : 9;
}
