/* SPDX-License-Identifier: GPL-2.0-only */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app-shortcut.h"
#include "app-target.h"

static void set_error(wchar_t *error, size_t capacity, const wchar_t *message)
{
    if (error && capacity) {
        wcsncpy(error, message, capacity - 1);
        error[capacity - 1] = L'\0';
    }
}

static bool full_path(const wchar_t *input, wchar_t **output)
{
    DWORD needed;
    wchar_t *path;
    if (!input || !input[0]) return false;
    needed = GetFullPathNameW(input, 0, NULL, NULL);
    if (!needed) return false;
    path = (wchar_t *)malloc((size_t)(needed + 1) * sizeof(*path));
    if (!path) return false;
    if (!GetFullPathNameW(input, needed + 1, path, NULL)) {
        free(path);
        return false;
    }
    *output = path;
    return true;
}

static bool plain_directory(const wchar_t *directory)
{
    wchar_t *path = NULL;
    wchar_t *cursor;
    DWORD attributes;
    bool valid = false;
    if (!full_path(directory, &path)) return false;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) goto done;
    for (cursor = path; *cursor; ++cursor) {
        if (*cursor != L'\\' && *cursor != L'/') continue;
        if (cursor == path || (cursor == path + 2 && path[1] == L':')) continue;
        {
            wchar_t saved = *cursor;
            *cursor = L'\0';
            attributes = GetFileAttributesW(path);
            *cursor = saved;
        }
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) goto done;
    }
    valid = true;
done:
    free(path);
    return valid;
}

static bool ascii_component(const wchar_t *component, char output[512])
{
    size_t i, length;
    if (!component) return false;
    length = wcslen(component);
    if (!length || length > 511) return false;
    for (i = 0; i < length; ++i) {
        if ((unsigned)component[i] > 0x7f || component[i] == L'\0') return false;
        output[i] = (char)component[i];
    }
    output[length] = '\0';
    return app_component_valid(output);
}

static uint32_t component_hash(const char *component)
{
    uint32_t hash = UINT32_C(2166136261);
    const unsigned char *cursor = (const unsigned char *)component;
    while (*cursor) {
        hash ^= *cursor++;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool append_path(const wchar_t *directory, const wchar_t *name,
                        wchar_t **output)
{
    size_t directory_length = wcslen(directory);
    size_t name_length = wcslen(name);
    bool separator = directory_length && directory[directory_length - 1] != L'\\' &&
                     directory[directory_length - 1] != L'/';
    wchar_t *path = (wchar_t *)malloc((directory_length + name_length +
                                      (separator ? 2 : 1)) * sizeof(*path));
    if (!path) return false;
    wcscpy(path, directory);
    if (separator) wcscat(path, L"\\");
    wcscat(path, name);
    *output = path;
    return true;
}

static bool launcher_parent(const wchar_t *launcher, wchar_t **full_launcher,
                            wchar_t **parent)
{
    DWORD attributes;
    wchar_t *path = NULL;
    wchar_t *slash;
    if (!full_path(launcher, &path)) return false;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        free(path);
        return false;
    }
    slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    if (!slash) {
        free(path);
        return false;
    }
    if (slash == path + 2 && path[1] == L':') slash[1] = L'\0';
    else *slash = L'\0';
    *full_launcher = NULL;
    if (!full_path(launcher, full_launcher)) {
        free(path);
        return false;
    }
    *parent = path;
    return true;
}

bool app_shortcut_create_named(const wchar_t *launcher, const wchar_t *component,
                         const wchar_t *label,const wchar_t *directory, wchar_t *saved,
                         size_t capacity, wchar_t *error,
                         size_t error_capacity)
{
    char ascii[512];
    wchar_t *full_launcher = NULL, *working = NULL;
    wchar_t *temporary = NULL;
    wchar_t target_name[160], temporary_name[80], arguments[520],clean[81];
    wchar_t *target_full = NULL, *directory_full = NULL;
    HANDLE reservation = INVALID_HANDLE_VALUE;
    IShellLinkW *link = NULL;
    IPersistFile *persist = NULL;
    HRESULT hr, co_result;
    bool uninitialize = false, success = false, temporary_owned=false;
    uint32_t hash;

    if (saved && capacity) saved[0] = L'\0';
    set_error(error, error_capacity, L"Could not create the shortcut.");
    if (!saved || !capacity || !ascii_component(component, ascii)) {
        set_error(error, error_capacity, L"Invalid application component.");
        goto done;
    }
    if (!plain_directory(directory)) {
        set_error(error, error_capacity, L"Shortcut directory must already exist and contain no links.");
        goto done;
    }
    if (!launcher_parent(launcher, &full_launcher, &working)) {
        set_error(error, error_capacity, L"Habumi launcher file was not found or is not a plain file.");
        goto done;
    }
    if (!plain_directory(working)) {
        set_error(error, error_capacity, L"Habumi launcher directory contains a link or is unavailable.");
        goto done;
    }
    if (!full_path(directory, &directory_full)) {
        set_error(error, error_capacity, L"Could not resolve the shortcut directory.");
        goto done;
    }
    hash = component_hash(ascii);
    size_t used=0;
    if(label)for(const wchar_t *p=label;*p && used<80;p++){
        if(*p>=32 && !wcschr(L"<>:\"/\\|?*",*p))clean[used++]=*p;
    }
    while(used && (clean[used-1]==L' ' || clean[used-1]==L'.' || (clean[used-1]>=0xd800 && clean[used-1]<=0xdbff)))used--;
    clean[used]=0;
    if(used)swprintf(target_name,160,L"Habumi - %ls - %08lX.lnk",clean,(unsigned long)hash);
    else swprintf(target_name,160,L"Habumi-app-%08lX.lnk",(unsigned long)hash);
    if (!append_path(directory_full, target_name, &target_full) ||
        wcslen(target_full) + 1 > capacity) {
        set_error(error, error_capacity, L"Saved shortcut path does not fit the output buffer.");
        goto done;
    }
    swprintf(arguments, sizeof(arguments) / sizeof(arguments[0]), L"--app \"%ls\"", component);
    co_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(co_result)) uninitialize = true;
    else if (co_result != RPC_E_CHANGED_MODE) {
        set_error(error, error_capacity, L"Could not initialize Windows shortcut services.");
        goto done;
    }
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkW, (void **)&link);
    if (FAILED(hr)) goto com_error;
    hr = IShellLinkW_SetPath(link, full_launcher);
    if (FAILED(hr)) goto com_error;
    hr = IShellLinkW_SetArguments(link, arguments);
    if (FAILED(hr)) goto com_error;
    hr = IShellLinkW_SetWorkingDirectory(link, working);
    if (FAILED(hr)) goto com_error;
    hr = IShellLinkW_SetIconLocation(link, full_launcher, 0);
    if (FAILED(hr)) goto com_error;
    hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (FAILED(hr)) goto com_error;

    swprintf(temporary_name, sizeof(temporary_name) / sizeof(temporary_name[0]),
             L".Habumi-app-%08lX-%08lX.tmp", (unsigned long)hash,
             (unsigned long)GetCurrentProcessId());
    if (!append_path(directory_full, temporary_name, &temporary)) goto com_error;
    {
        unsigned attempt;
        for (attempt = 0; attempt < 128; ++attempt) {
            wchar_t attempt_name[96];
            free(temporary);
            temporary = NULL;
            swprintf(attempt_name, sizeof(attempt_name) / sizeof(attempt_name[0]),
                     L".Habumi-app-%08lX-%08lX-%03u.tmp", (unsigned long)hash,
                     (unsigned long)GetCurrentProcessId(), attempt);
            if (!append_path(directory_full, attempt_name, &temporary)) goto com_error;
            reservation = CreateFileW(temporary, GENERIC_WRITE, 0, NULL,
                                      CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
            if (reservation != INVALID_HANDLE_VALUE){temporary_owned=true;break;}
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
                goto com_error;
        }
        if (reservation == INVALID_HANDLE_VALUE) goto com_error;
    }
    CloseHandle(reservation);
    reservation = INVALID_HANDLE_VALUE;
    hr = IPersistFile_Save(persist, temporary, TRUE);
    if (FAILED(hr)) goto com_error;
    if (!MoveFileExW(temporary, target_full, MOVEFILE_WRITE_THROUGH)) {
        set_error(error, error_capacity,
                  GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS
                    ? L"A shortcut for this application already exists."
                    : L"Could not publish the completed shortcut.");
        goto done;
    }
    wcscpy(saved, target_full);
    if (error && error_capacity) error[0] = L'\0';
    success = true;
    goto done;

com_error:
    set_error(error, error_capacity, L"Windows could not save the application shortcut.");
done:
    if (reservation != INVALID_HANDLE_VALUE) CloseHandle(reservation);
    if (!success && temporary && temporary_owned) DeleteFileW(temporary);
    if (persist) IPersistFile_Release(persist);
    if (link) IShellLinkW_Release(link);
    if (uninitialize) CoUninitialize();
    free(full_launcher);
    free(working);
    free(directory_full);
    free(target_full);
    free(temporary);
    return success;
}

bool app_shortcut_create(const wchar_t *launcher,const wchar_t *component,
    const wchar_t *directory,wchar_t *saved,size_t capacity,wchar_t *error,size_t error_capacity)
{
    return app_shortcut_create_named(launcher,component,NULL,directory,saved,capacity,error,error_capacity);
}
