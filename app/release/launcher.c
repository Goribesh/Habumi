/* SPDX-License-Identifier: GPL-2.0-only */
/* GUI entry point: prepare Vulkan through the existing script without a console. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include "../guscio/app-target.h"
#include "data-ui.h"

#ifndef HABUMI_CHILD_WINDOW
#define HABUMI_CHILD_WINDOW 0
#endif

static int fail(const wchar_t *message, BOOL validate)
{
    if (!validate) { MessageBoxW(NULL, message, L"Habumi startup", MB_OK | MB_ICONERROR); }
    return 1;
}

static int update_start(const wchar_t *root,BOOL validate)
{
    wchar_t host[32768],command[65536];STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};
    if(validate)return fail(L"Software recovery is required before starting Habumi.",TRUE);
    DWORD length=GetSystemDirectoryW(host,32768);
    if(!length || length+48>=32768)return fail(L"Cannot locate Windows PowerShell.",FALSE);
    wcscat(host,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    if(swprintf(command,65536,L"\"%ls\" -NoProfile -ExecutionPolicy Bypass -STA -File \"%ls\\Update-Habumi.ps1\" -ProfileRoot \"%ls\"",host,root,root)<0 || wcslen(command)>=32767)return 1;
    startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    if(!CreateProcessW(host,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,root,&startup,&process))return fail(L"The update panel could not be opened.",FALSE);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    /* Exit the launcher so an external helper can replace this executable. */
    return 0;
}

static BOOL app_component_from_wide(const wchar_t *wide, char *ascii,
                                    wchar_t *copy)
{
    size_t i;

    if (!wide || !ascii || !copy) { return FALSE; }
    for (i = 0; i < APP_COMPONENT_MAX; i++) {
        wchar_t c = wide[i];
        if (!c) {
            if (!i) { return FALSE; }
            ascii[i] = '\0';
            copy[i] = L'\0';
            return app_component_valid(ascii) ? TRUE : FALSE;
        }
        if (c > 0x7f) { return FALSE; }
        ascii[i] = (char)c;
        copy[i] = c;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show)
{
    wchar_t root[32768], host[32768], script[32768], command[65536];
    wchar_t app_wide[APP_COMPONENT_MAX] = {0};
    char app_ascii[APP_COMPONENT_MAX];
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD result = 1;
    int argc = 0;
    BOOL validate = FALSE, single = HABUMI_CHILD_WINDOW, data = FALSE;
    BOOL has_app = FALSE,update=FALSE;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    (void)instance; (void)previous; (void)command_line; (void)show;
    if (!argv) { return fail(L"Unable to read the launcher arguments.", FALSE); }
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--validate")) { validate = TRUE; }
    }
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--validate")) { continue; }
        else if (!wcscmp(argv[i], L"--data")) { data = TRUE; }
        else if (!wcscmp(argv[i], L"--update")) { update = TRUE; }
        else if (!wcscmp(argv[i], L"--single-window")) { single = TRUE; }
        else if (!wcscmp(argv[i], L"--app")) {
            if (has_app || i + 1 >= argc ||
                !app_component_from_wide(argv[i + 1], app_ascii, app_wide)) {
                LocalFree(argv);
                return fail(L"The application component is missing or invalid.", validate);
            }
            has_app = TRUE;
            i++;
        }
        else { LocalFree(argv); return fail(L"Unknown launcher argument.", validate); }
    }
    LocalFree(argv);
    DWORD length = GetModuleFileNameW(NULL, root, 32768);
    if (!length || length >= 32768) { return fail(L"Unable to locate the Habumi folder.", validate); }
    wchar_t *slash = wcsrchr(root, L'\\');
    if (!slash) { return fail(L"Unable to locate the Habumi folder.", validate); }
    *slash = L'\0';
    if (has_app && (validate || data || update)) {
        return fail(L"Application launch cannot be combined with validation or data management.", validate);
    }
    if (validate && data) return fail(L"Choose either validation or data management.", TRUE);
    if(update && (validate || data))return fail(L"Choose either updates, validation or data management.",validate);
    if (!SetEnvironmentVariableW(L"HABUMI_LAUNCH_COMPONENT",
                                 has_app ? app_wide : NULL)) {
        return fail(L"Unable to prepare the Habumi environment.", validate);
    }
    wchar_t marker[32768];
    if(swprintf(marker,32768,L"%ls\\software-update.lock",root)<0)return 1;
    if(update || GetFileAttributesW(marker)!=INVALID_FILE_ATTRIBUTES)return update_start(root,validate);
    if (swprintf(marker,32768,L"%ls\\storage-maintenance.lock",root)<0) return 1;
    if (data || (!validate && GetFileAttributesW(marker)!=INVALID_FILE_ATTRIBUTES)) {
        return data_ui_run(root);
    }
    if (!validate) {
        HWND manager=data_ui_existing(root);
        if(manager){ShowWindow(manager,SW_RESTORE);SetForegroundWindow(manager);return 0;}
    }
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
