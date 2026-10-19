/* SPDX-License-Identifier: GPL-2.0-only */
/* GUI entry point: prepare Vulkan through the existing script without a console. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#ifndef HABUMI_CHILD_WINDOW
#define HABUMI_CHILD_WINDOW 0
#endif

static int fail(const wchar_t *message, BOOL validate)
{
    if (!validate) { MessageBoxW(NULL, message, L"Habumi startup", MB_OK | MB_ICONERROR); }
    return 1;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show)
{
    wchar_t root[32768], host[32768], script[32768], command[65536];
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD result = 1;
    int argc = 0;
    BOOL validate = FALSE, single = HABUMI_CHILD_WINDOW;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    (void)instance; (void)previous; (void)command_line; (void)show;
    if (!argv) { return fail(L"Unable to read the launcher arguments.", FALSE); }
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--validate")) { validate = TRUE; }
        else if (!wcscmp(argv[i], L"--single-window")) { single = TRUE; }
        else { LocalFree(argv); return fail(L"Unknown launcher argument.", validate); }
    }
    LocalFree(argv);
    DWORD length = GetModuleFileNameW(NULL, root, 32768);
    if (!length || length >= 32768) { return fail(L"Unable to locate the Habumi folder.", validate); }
    wchar_t *slash = wcsrchr(root, L'\\');
    if (!slash) { return fail(L"Unable to locate the Habumi folder.", validate); }
    *slash = L'\0';
    if (swprintf(script, 32768, L"%ls\\Start-Habumi.ps1", root) < 0 ||
        GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
        return fail(L"Start-Habumi.ps1 is missing. Extract the complete Habumi package.", validate);
    }
    length = GetSystemDirectoryW(host, 32768);
    if (!length || length >= 32768 ||
        length + wcslen(L"\\WindowsPowerShell\\v1.0\\powershell.exe") >= 32768) {
        return fail(L"Unable to locate Windows PowerShell.", validate);
    }
    wcscat(host, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    if (swprintf(command, 65536,
        L"\"%ls\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\"%ls",
        host, script, validate ? L" -Validate" : L"") < 0 || wcslen(command) >= 32767) {
        return fail(L"The Habumi folder path is too long.", validate);
    }
    if (!SetEnvironmentVariableW(L"HABUMI_SINGLE_WINDOW", single ? L"1" : NULL)) {
        return fail(L"Unable to prepare the Habumi environment.", validate);
    }
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(host, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, root, &startup, &process)) {
        return fail(L"Unable to start Windows PowerShell for Habumi.", validate);
    }
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &result);
    }
    CloseHandle(process.hProcess);
    return (int)result;
}
