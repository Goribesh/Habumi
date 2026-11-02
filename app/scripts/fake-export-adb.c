/* SPDX-License-Identifier: GPL-2.0-only */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static bool environment(const wchar_t *name, wchar_t *value, DWORD capacity)
{
    DWORD length = GetEnvironmentVariableW(name, value, capacity);
    return length > 0 && length < capacity;
}

static bool append_utf8(const wchar_t *path, const wchar_t *text)
{
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    int bytes;
    char *buffer;
    DWORD written = 0;
    bool ok;
    if (file == INVALID_HANDLE_VALUE) return false;
    bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) { CloseHandle(file); return false; }
    buffer = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)bytes);
    if (!buffer) { CloseHandle(file); return false; }
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, bytes, NULL, NULL);
    ok = WriteFile(file, buffer, (DWORD)(bytes - 1), &written, NULL) && written == (DWORD)(bytes - 1);
    HeapFree(GetProcessHeap(), 0, buffer);
    CloseHandle(file);
    return ok;
}

static int next_shell_call(const wchar_t *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    char buffer[32] = {0};
    DWORD count = 0, written = 0;
    int call = 0;
    if (file == INVALID_HANDLE_VALUE) return -1;
    ReadFile(file, buffer, sizeof(buffer) - 1, &count, NULL);
    if (count) call = atoi(buffer);
    call++;
    sprintf(buffer, "%d\n", call);
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    SetEndOfFile(file);
    if (!WriteFile(file, buffer, (DWORD)strlen(buffer), &written, NULL) || written != strlen(buffer)) call = -1;
    CloseHandle(file);
    return call;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t mode[64], hash[80], size[40], source[MAX_PATH], log[MAX_PATH], calls[MAX_PATH];
    wchar_t log_line[8192];
    size_t used = 0;
    int i, call;
    if (argc < 4 || wcscmp(argv[1], L"-s") != 0 ||
        wcsncmp(argv[2], L"127.0.0.1:", 10) != 0) return 20;
    if (!environment(L"HABUMI_FAKE_EXPORT_MODE", mode, 64) ||
        !environment(L"HABUMI_FAKE_EXPORT_LOG", log, MAX_PATH) ||
        !environment(L"HABUMI_FAKE_EXPORT_CALLS", calls, MAX_PATH)) return 21;
    used += (size_t)swprintf(log_line + used, 8192 - used, L"CALL");
    for (i = 1; i < argc && used < 8000; ++i) {
        int added = swprintf(log_line + used, 8192 - used, L"\t%ls", argv[i]);
        if (added < 0) return 22;
        used += (size_t)added;
    }
    if (used + 2 < 8192) { log_line[used++] = L'\r'; log_line[used++] = L'\n'; log_line[used] = L'\0'; }
    if (!append_utf8(log, log_line)) return 23;
    if (wcscmp(argv[3], L"shell") == 0) {
        if (argc < 5 || !environment(L"HABUMI_FAKE_EXPORT_HASH", hash, 80) ||
            !environment(L"HABUMI_FAKE_EXPORT_BYTES", size, 40)) return 24;
        call = next_shell_call(calls);
        if (call < 0) return 25;
        if (wcscmp(mode, L"shellerror") == 0) return 26;
        if (wcscmp(mode, L"changed") == 0 && call > 1) wcscpy(hash, L"0000000000000000000000000000000000000000000000000000000000000000");
        wprintf(L"%ls  /sdcard/fake.bin\n%ls\n", hash, size);
        return 0;
    }
    if (wcscmp(argv[3], L"pull") == 0) {
        if (argc < 6 || !environment(L"HABUMI_FAKE_EXPORT_SOURCE", source, MAX_PATH)) return 27;
        if (wcscmp(mode, L"pullerror") == 0) return 28;
        if (!CopyFileW(source, argv[5], FALSE)) return 29;
        return 0;
    }
    return 30;
}
