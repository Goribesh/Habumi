/* SPDX-License-Identifier: GPL-2.0-only */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../guscio/app-shortcut.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fwprintf(stderr, L"FAIL line %d\n", __LINE__); \
        return 1; \
    } \
} while (0)

static bool write_file(const wchar_t *path, const char *contents)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    size_t length = strlen(contents);
    bool ok;
    if (file == INVALID_HANDLE_VALUE) return false;
    ok = WriteFile(file, contents, (DWORD)length, &written, NULL) && written == length;
    CloseHandle(file);
    return ok;
}

static bool read_file(const wchar_t *path, char *buffer, DWORD capacity, DWORD *length)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    bool ok;
    if (file == INVALID_HANDLE_VALUE) return false;
    ok = ReadFile(file, buffer, capacity, length, NULL) != 0;
    CloseHandle(file);
    return ok;
}

static bool join(wchar_t *out, size_t capacity, const wchar_t *root,
                 const wchar_t *leaf)
{
    return swprintf(out, capacity, L"%ls\\%ls", root, leaf) >= 0;
}

int wmain(int argc, wchar_t **argv)
{
    static const wchar_t component[] = L"com.example.demo/.MainActivity";
    wchar_t launcher[MAX_PATH], folder[MAX_PATH], expected[MAX_PATH];
    wchar_t saved[MAX_PATH], error[256];
    wchar_t got_path[MAX_PATH], got_args[1024], got_working[MAX_PATH];
    wchar_t got_icon[MAX_PATH];
    char before[4096], after[4096];
    DWORD before_length = 0, after_length = 0;
    int icon_index = -1;
    HANDLE lock;
    HRESULT co;
    IShellLinkW *link = NULL;
    IPersistFile *persist = NULL;

    if(argc==6 && !wcscmp(argv[1],L"--create")){
        if(!app_shortcut_create_named(argv[2],argv[3],argv[4],argv[5],saved,MAX_PATH,error,256)){fwprintf(stderr,L"%ls\n",error);return 2;}
        wprintf(L"%ls\n",saved);return 0;
    }

    CHECK(argc == 2);
    CHECK(join(launcher, MAX_PATH, argv[1], L"Habumi.exe"));
    CHECK(join(folder, MAX_PATH, argv[1], L"Shortcuts"));
    CHECK(CreateDirectoryW(folder, NULL));
    CHECK(write_file(launcher, "fake launcher fixture"));

    CHECK(!app_shortcut_create(launcher, L"com.example.demo/\u00E9", folder,
                               saved, MAX_PATH, error, 256));
    CHECK(saved[0] == L'\0');
    CHECK(error[0] != L'\0');
    CHECK(!app_shortcut_create(launcher, component, L"Z:\\missing\\folder",
                               saved, MAX_PATH, error, 256));

    CHECK(app_shortcut_create(launcher, component, folder, saved, MAX_PATH,
                              error, 256));
    CHECK(error[0] == L'\0');
    CHECK(GetFileAttributesW(saved) != INVALID_FILE_ATTRIBUTES);
    CHECK(!app_shortcut_create(launcher, component, folder, expected, MAX_PATH,
                               error, 256));
    CHECK(expected[0] == L'\0');
    CHECK(wcsstr(error, L"already exists") != NULL);

    CHECK(join(expected, MAX_PATH, argv[1], L"Habumi.exe"));
    CHECK(read_file(saved, before, sizeof(before), &before_length));
    CHECK(before_length > 0);
    co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CHECK(SUCCEEDED(co));
    CHECK(SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                     &IID_IShellLinkW, (void **)&link)));
    CHECK(SUCCEEDED(IShellLinkW_QueryInterface(link, &IID_IPersistFile,
                                               (void **)&persist)));
    CHECK(SUCCEEDED(IPersistFile_Load(persist, saved, STGM_READ)));
    CHECK(SUCCEEDED(IShellLinkW_GetPath(link, got_path, MAX_PATH, NULL,
                                        SLGP_RAWPATH)));
    CHECK(SUCCEEDED(IShellLinkW_GetArguments(link, got_args, 1024)));
    CHECK(SUCCEEDED(IShellLinkW_GetWorkingDirectory(link, got_working, MAX_PATH)));
    CHECK(SUCCEEDED(IShellLinkW_GetIconLocation(link, got_icon, MAX_PATH,
                                                &icon_index)));
    CHECK(wcscmp(got_path, expected) == 0);
    CHECK(wcscmp(got_args, L"--app \"com.example.demo/.MainActivity\"") == 0);
    CHECK(wcscmp(got_working, argv[1]) == 0);
    CHECK(wcscmp(got_icon, expected) == 0);
    CHECK(icon_index == 0);
    IPersistFile_Release(persist);
    IShellLinkW_Release(link);
    CoUninitialize();

    CHECK(read_file(saved, after, sizeof(after), &after_length));
    CHECK(after_length == before_length);
    CHECK(memcmp(before, after, before_length) == 0);
    lock = CreateFileW(folder, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
    CHECK(lock != INVALID_HANDLE_VALUE);
    CloseHandle(lock);
    CHECK(app_shortcut_create_named(launcher,L"com.example/.Other",L"Calculator / test",folder,saved,MAX_PATH,error,256));
    CHECK(wcsstr(saved,L"Habumi - Calculator  test - ")!=NULL);
    const char *colliding="com.example/.Collide";unsigned long hash=2166136261u;
    for(const unsigned char *p=(const unsigned char*)colliding;*p;p++)hash=(hash^*p)*16777619u;
    wchar_t reserved[MAX_PATH];
    for(unsigned i=0;i<128;i++){
        swprintf(reserved,MAX_PATH,L"%ls\\.Habumi-app-%08lX-%08lX-%03u.tmp",folder,hash,(unsigned long)GetCurrentProcessId(),i);
        CHECK(write_file(reserved,"reserved by another operation"));
    }
    CHECK(!app_shortcut_create(launcher,L"com.example/.Collide",folder,saved,MAX_PATH,error,256));
    CHECK(read_file(reserved,after,sizeof(after),&after_length));
    CHECK(after_length==strlen("reserved by another operation"));
    wprintf(L"PASS: verified ShellLink target, arguments, working directory, icon, "
            L"Unicode path, invalid component and no-overwrite behavior.\n");
    return 0;
}
