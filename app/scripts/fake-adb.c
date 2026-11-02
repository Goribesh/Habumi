/* SPDX-License-Identifier: GPL-2.0-only */
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

static const unsigned char png[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,
    0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,
    0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,
    0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x60,0x00,
    0x02,0x00,0x00,0x05,0x00,0x01,0xa5,0xf6,0x45,0x40,
    0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};

int wmain(int argc, wchar_t **argv)
{
    wchar_t mode[32] = L"success";
    DWORD n = GetEnvironmentVariableW(L"HABUMI_SCREENSHOT_FAKE_MODE", mode,
                                      sizeof(mode) / sizeof(mode[0]));
    (void)n;
    if (argc != 6 || wcscmp(argv[1], L"-s") ||
        wcscmp(argv[2], L"127.0.0.1:17555") ||
        wcscmp(argv[3], L"exec-out") || wcscmp(argv[4], L"screencap") ||
        wcscmp(argv[5], L"-p")) return 20;
    _setmode(_fileno(stdout), _O_BINARY);
    if (wcscmp(mode, L"timeout") == 0) { Sleep(30000); return 0; }
    if (wcscmp(mode, L"oversized") == 0) {
        static unsigned char block[1024 * 1024];
        unsigned i;
        memset(block, 0, sizeof(block));
        for (i = 0; i < 65; i++) {
            if (fwrite(block, 1, sizeof(block), stdout) != sizeof(block)) return 4;
        }
        return 0;
    }
    if (wcscmp(mode, L"bad") == 0) {
        static const char bad[] = "not png";
        fwrite(bad, 1, sizeof(bad) - 1, stdout);
        return 0;
    }
    if (wcscmp(mode, L"nonzero") == 0) {
        fwrite(png, 1, sizeof(png), stdout);
        return 9;
    }
    fwrite(png, 1, sizeof(png), stdout);
    return ferror(stdout) ? 3 : 0;
}
