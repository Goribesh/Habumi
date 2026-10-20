/* SPDX-License-Identifier: GPL-2.0-only */
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>
#include "data-ui.h"
#include "data-job.h"

enum { ACTION = 200, PATH_EDIT, BROWSE, IMAGE, CAPACITY, RUN, OPEN_FOLDER, START_ANDROID, CLOSE_UI };
static const wchar_t *operations[] = {L"Backup", L"Restore", L"Grow", L"Recover", L"Diagnostics"};
static const wchar_t *descriptions[] = {
    L"Choose a new backup folder. Android data, settings and key maps are included.",
    L"Choose a verified Habumi backup. Current files will be retained for recovery.",
    L"Select the data disk and a larger capacity. The original disk is retained.",
    L"Recover the interrupted replacement using its retained originals.",
    L"Save system and driver information, configured settings and recent logs locally. Android data disks are excluded."
};
static const wchar_t *run_labels[] = {L"Back up", L"Restore", L"Grow", L"Recover", L"Export"};
static const wchar_t *images[] = {L"data-gapps.img", L"data.img"};
typedef struct {
    wchar_t root[DATA_PATH];
    HWND window, heading, profile, status, action_label, action, description;
    HWND path_label, path, browse, image, capacity, size_info, progress, result;
    HWND run, open, start, close;
    HFONT font, heading_font;
    HBRUSH brush;
    COLORREF background, foreground;
    bool dark, busy, recovery, closing;
    UINT dpi;
    DataJob job;
} DataUi;
static DataUi ui;
#define PROFILE_PROPERTY L"HabumiDataProfile"
static uintptr_t profile_id(const wchar_t *root)
{
    wchar_t lower[DATA_PATH];
    if(wcslen(root)>=DATA_PATH)return 0;
    wcscpy(lower,root);CharLowerBuffW(lower,(DWORD)wcslen(lower));
    uintptr_t hash=(uintptr_t)1469598103934665603ULL;
    for(const wchar_t *p=lower;*p;++p){hash^=(uintptr_t)*p;hash*=(uintptr_t)1099511628211ULL;}
    return hash?hash:1;
}
typedef struct {uintptr_t id;HWND found;} Existing;
static BOOL CALLBACK find_existing(HWND window,LPARAM parameter)
{
    Existing *search=(Existing*)parameter;wchar_t name[64];
    if(GetClassNameW(window,name,64) && !wcscmp(name,L"HabumiDataManagement") && (uintptr_t)GetPropW(window,PROFILE_PROPERTY)==search->id){search->found=window;return FALSE;}
    return TRUE;
}
HWND data_ui_existing(const wchar_t *root)
{
    Existing search={profile_id(root),NULL};EnumWindows(find_existing,(LPARAM)&search);return search.found;
}
static int scale(int value) { return MulDiv(value, (int)ui.dpi, 96); }
static int selection(void) { int n = (int)SendMessageW(ui.action, CB_GETCURSEL, 0, 0); return n >= 0 && n < 5 ? n : 0; }
static void set_text(HWND control,const wchar_t *text)
{if(!control)return;wchar_t current[DATA_PATH]={0};GetWindowTextW(control,current,DATA_PATH);if(wcscmp(current,text))SetWindowTextW(control,text);}

static void place(HWND child, int x, int y, int width, int height)
{ MoveWindow(child, scale(x), scale(y), scale(width), scale(height), TRUE); }

static void layout(void)
{
    RECT r; GetClientRect(ui.window, &r);
    int w = MulDiv(r.right, 96, (int)ui.dpi), h = MulDiv(r.bottom, 96, (int)ui.dpi);
    place(ui.heading, 16, 12, w - 32, 28); place(ui.profile, 16, 44, w - 32, 20);
    place(ui.status, 16, 72, w - 32, 38); place(ui.action_label, 16, 120, 60, 20);
    place(ui.action, 80, 114, w - 96, 180); place(ui.description, 16, 154, w - 32, 44);
    place(ui.path_label, 16, 210, 52, 20); place(ui.path, 72, 204, w - 168, 26);
    place(ui.browse, w - 88, 204, 72, 26);
    place(ui.image, 16, 204, w - 160, 140); place(ui.capacity, w - 132, 204, 116, 140);
    place(ui.size_info, 16, 244, w - 32, 38); place(ui.progress, 16, h - 134, w - 32, 8);
    place(ui.result, 16, h - 110, w - 32, 54);
    place(ui.open, 16, h - 42, 92, 28); place(ui.start, w - 316, h - 42, 112, 28);
    place(ui.run, w - 196, h - 42, 84, 28); place(ui.close, w - 104, h - 42, 88, 28);
}

static void theme(void)
{
    HIGHCONTRASTW high = {sizeof(high), 0, NULL};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high), &high, 0);
    DWORD light = 1, bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light, &bytes);
    ui.dark = !(high.dwFlags & HCF_HIGHCONTRASTON) && !light;
    ui.background = high.dwFlags & HCF_HIGHCONTRASTON ? GetSysColor(COLOR_WINDOW) : ui.dark ? RGB(32,32,32) : RGB(250,250,250);
    ui.foreground = high.dwFlags & HCF_HIGHCONTRASTON ? GetSysColor(COLOR_WINDOWTEXT) : ui.dark ? RGB(240,240,240) : RGB(28,28,28);
    if (ui.brush) DeleteObject(ui.brush);
    ui.brush = CreateSolidBrush(ui.background);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        typedef HRESULT (WINAPI *SetAttribute)(HWND,DWORD,LPCVOID,DWORD);
        SetAttribute set = (SetAttribute)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
        BOOL dark = ui.dark;
        if (set) set(ui.window, 20, &dark, sizeof(dark));
        FreeLibrary(dwm);
    }
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
        typedef HRESULT (WINAPI *SetTheme)(HWND,LPCWSTR,LPCWSTR);
        SetTheme set = (SetTheme)(void *)GetProcAddress(ux, "SetWindowTheme");
        HWND controls[] = {ui.action,ui.image,ui.capacity,ui.path,ui.run,ui.start,ui.close,ui.open,ui.browse};
        if (set) for (unsigned i = 0; i < sizeof(controls)/sizeof(controls[0]); ++i) set(controls[i], ui.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
        FreeLibrary(ux);
    }
    InvalidateRect(ui.window, NULL, TRUE);
}

static void fonts(void)
{
    if (ui.font) DeleteObject(ui.font);
    if (ui.heading_font) DeleteObject(ui.heading_font);
    ui.font = CreateFontW(-scale(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    ui.heading_font = CreateFontW(-scale(21),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HWND controls[] = {ui.profile,ui.status,ui.action_label,ui.action,ui.description,ui.path_label,ui.path,ui.browse,ui.image,ui.capacity,ui.size_info,ui.result,ui.run,ui.open,ui.start,ui.close};
    for (unsigned i = 0; i < sizeof(controls)/sizeof(controls[0]); ++i) SendMessageW(controls[i],WM_SETFONT,(WPARAM)ui.font,TRUE);
    SendMessageW(ui.heading,WM_SETFONT,(WPARAM)ui.heading_font,TRUE);
}

static void new_path(void)
{
    if (selection() == 1) { SetWindowTextW(ui.path, L""); return; }
    wchar_t desktop[DATA_PATH], path[DATA_PATH]; SYSTEMTIME now;
    GetLocalTime(&now);
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, desktop))) return;
    if(swprintf(path, DATA_PATH, L"%ls\\Habumi-%ls-%04u%02u%02u-%02u%02u%02u", desktop,
        selection() == 4 ? L"diagnostics" : L"backup", now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond)<0){SetWindowTextW(ui.path,L"");return;}
    SetWindowTextW(ui.path, path);
}

static void refresh(void)
{
    int action = selection();
    wchar_t marker[DATA_PATH], lock[DATA_PATH], size_text[200], disk[DATA_PATH];
    swprintf(marker,DATA_PATH,L"%ls\\storage-maintenance.lock",ui.root);
    bool pending = GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES;
    if (pending && !ui.recovery && !ui.job.process) { SendMessageW(ui.action,CB_SETCURSEL,3,0); action = 3; }
    ui.recovery = pending;
    swprintf(lock,DATA_PATH,L"%ls\\.habumi-profile.lock",ui.root);
    HANDLE guard = CreateFileW(lock,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    ui.busy = guard == INVALID_HANDLE_VALUE;
    if (guard != INVALID_HANDLE_VALUE) CloseHandle(guard);
    bool working = ui.job.process != NULL;
    set_text(ui.status, working ? L"Data operation in progress. Android remains stopped." : ui.busy ?
        L"This profile is in use. Close Android normally or wait for maintenance to finish." : pending ?
        L"Interrupted maintenance detected. Recover the profile before starting Android." :
        L"Android is stopped. Data operations are available.");
    set_text(ui.description, descriptions[action]);
    set_text(ui.run, run_labels[action]);
    ShowWindow(ui.progress,working?SW_SHOW:SW_HIDE);
    bool paths = action == 0 || action == 1 || action == 4;
    ShowWindow(ui.path_label,paths?SW_SHOW:SW_HIDE); ShowWindow(ui.path,paths?SW_SHOW:SW_HIDE); ShowWindow(ui.browse,paths?SW_SHOW:SW_HIDE);
    ShowWindow(ui.image,action==2?SW_SHOW:SW_HIDE); ShowWindow(ui.capacity,action==2?SW_SHOW:SW_HIDE); ShowWindow(ui.size_info,action==2?SW_SHOW:SW_HIDE);
    bool permitted = !working && !ui.busy && (!pending || action == 3 || action == 4);
    if (action == 3 && !pending) permitted = false;
    if (action == 2) {
        int image = (int)SendMessageW(ui.image,CB_GETCURSEL,0,0), capacity = (int)SendMessageW(ui.capacity,CB_GETCURSEL,0,0);
        if (image < 0 || image > 1) image = 0;
        int target = capacity == 0 ? 8 : capacity == 1 ? 12 : 16;
        swprintf(disk,DATA_PATH,L"%ls\\guest\\images\\android\\%ls",ui.root,images[image]);
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (GetFileAttributesExW(disk,GetFileExInfoStandard,&data)) {
            ULONGLONG bytes = ((ULONGLONG)data.nFileSizeHigh<<32)|data.nFileSizeLow;
            swprintf(size_text,200,L"Current size: %.1f GiB. Choose a larger capacity.",(double)bytes/1073741824.0);
            if (bytes >= (ULONGLONG)target*1073741824ULL) permitted = false;
        } else { wcscpy(size_text,L"The selected data disk is missing. Start this Android variant once before growing it."); permitted = false; }
        swprintf(disk,DATA_PATH,L"%ls\\guest\\maintenance\\maintenance-initramfs.img",ui.root);
        if (GetFileAttributesW(disk) == INVALID_FILE_ATTRIBUTES) { wcscpy(size_text,L"This package does not include the maintenance tools."); permitted = false; }
        set_text(ui.size_info,size_text);
    }
    if (paths && !GetWindowTextLengthW(ui.path)) permitted = false;
    EnableWindow(ui.run,permitted); EnableWindow(ui.action,!working);
    EnableWindow(ui.path,!working); EnableWindow(ui.browse,!working); EnableWindow(ui.image,!working); EnableWindow(ui.capacity,!working);
    EnableWindow(ui.start,!working && !ui.busy && !pending);
    EnableWindow(ui.open,!working && (ui.job.folder[0] || ui.job.directory[0]));
}

static void browse(void)
{
    IFileOpenDialog *dialog = NULL; IShellItem *item = NULL; PWSTR path = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog,NULL,CLSCTX_INPROC_SERVER,&IID_IFileOpenDialog,(void **)&dialog))) return;
    DWORD options = 0;
    IFileOpenDialog_GetOptions(dialog,&options);
    IFileOpenDialog_SetOptions(dialog,options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
    IFileOpenDialog_SetTitle(dialog,selection()==1?L"Choose a Habumi backup":L"Choose the parent folder");
    if (SUCCEEDED(IFileOpenDialog_Show(dialog,ui.window)) && SUCCEEDED(IFileOpenDialog_GetResult(dialog,&item)) &&
        SUCCEEDED(IShellItem_GetDisplayName(item,SIGDN_FILESYSPATH,&path))) {
        wchar_t chosen[DATA_PATH];
        int length;
        if (selection() == 1) length=swprintf(chosen,DATA_PATH,L"%ls",path);
        else {
            wchar_t old[DATA_PATH]; GetWindowTextW(ui.path,old,DATA_PATH);
            wchar_t *name = wcsrchr(old,L'\\');
            length=swprintf(chosen,DATA_PATH,L"%ls\\%ls",path,name?name+1:L"Habumi-backup");
        }
        if(length>=0)SetWindowTextW(ui.path,chosen);
        else SetWindowTextW(ui.result,L"The selected folder path is too long.");
    }
    if (path) CoTaskMemFree(path);
    if (item) IShellItem_Release(item);
    IFileOpenDialog_Release(dialog); refresh();
}

static void run_operation(void)
{
    refresh(); if (!IsWindowEnabled(ui.run)) return;
    if (selection()==1 && MessageBoxW(ui.window,L"Restore this backup? Current profile files will be replaced after verification. Previous files are retained for recovery.",L"Restore Habumi data",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES) return;
    wchar_t path[DATA_PATH]; GetWindowTextW(ui.path,path,DATA_PATH);
    int image = (int)SendMessageW(ui.image,CB_GETCURSEL,0,0), capacity = (int)SendMessageW(ui.capacity,CB_GETCURSEL,0,0);
    if (image < 0 || image > 1) image = 0;
    if (data_job_start(&ui.job,ui.root,operations[selection()],path,images[image],capacity==0?8:capacity==1?12:16)) {
        SendMessageW(ui.progress,PBM_SETMARQUEE,TRUE,40);
    }
    SetWindowTextW(ui.result,ui.job.message); refresh();
}

static void start_android(void)
{
    refresh(); if (!IsWindowEnabled(ui.start)) return;
    wchar_t exe[DATA_PATH], command[DATA_PATH*2];
    swprintf(exe,DATA_PATH,L"%ls\\Habumi.exe",ui.root);
    if (!data_quote(command,DATA_PATH*2,exe)) return;
    STARTUPINFOW startup={0}; PROCESS_INFORMATION process={0}; startup.cb=sizeof(startup);
    RemovePropW(ui.window,PROFILE_PROPERTY);
    if (CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,ui.root,&startup,&process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess); DestroyWindow(ui.window);
    } else {SetPropW(ui.window,PROFILE_PROPERTY,(HANDLE)profile_id(ui.root));SetWindowTextW(ui.result,L"Android could not be started. Check that the complete package is present.");}
}

static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wp)==ACTION && HIWORD(wp)==CBN_SELCHANGE) { new_path(); refresh(); }
        else if (LOWORD(wp)==BROWSE) browse();
        else if (LOWORD(wp)==RUN) run_operation();
        else if (LOWORD(wp)==START_ANDROID) start_android();
        else if (LOWORD(wp)==CLOSE_UI) SendMessageW(window,WM_CLOSE,0,0);
        else if (LOWORD(wp)==OPEN_FOLDER) ShellExecuteW(window,L"open",ui.job.folder[0]?ui.job.folder:ui.job.directory,NULL,NULL,SW_SHOWNORMAL);
        else if ((LOWORD(wp)==IMAGE || LOWORD(wp)==CAPACITY) && HIWORD(wp)==CBN_SELCHANGE) refresh();
        else if (LOWORD(wp)==PATH_EDIT && HIWORD(wp)==EN_CHANGE) refresh();
        return 0;
    case WM_TIMER:
        if (ui.job.process) {
            int result=data_job_poll(&ui.job); set_text(ui.result,ui.job.message);
            if (result) {
                SendMessageW(ui.progress,PBM_SETMARQUEE,FALSE,0);
                if (ui.closing && result>0) { DestroyWindow(window); return 0; }
                if (result<0) {ui.closing=false;SetWindowTextW(ui.close,L"Close");}
            }
        }
        refresh(); return 0;
    case WM_CLOSE:
        if (ui.job.process) { ui.closing=true;SetWindowTextW(ui.close,L"Finishing...");SetWindowTextW(ui.result,L"This operation will finish before the window closes."); }
        else DestroyWindow(window);
        return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_DESTROY: KillTimer(window,1);PostQuitMessage(0);return 0;
    case WM_SIZE: if(ui.heading) layout();return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize.x=scale(480);((MINMAXINFO*)lp)->ptMinTrackSize.y=scale(430);return 0;
    case WM_DPICHANGED: {
        ui.dpi=HIWORD(wp);RECT *r=(RECT*)lp;SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);fonts();layout();return 0;
    }
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: if(ui.heading) theme();break;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp,ui.foreground);SetBkColor((HDC)wp,ui.background);return (LRESULT)ui.brush;
    case WM_ERASEBKGND: {RECT r;GetClientRect(window,&r);FillRect((HDC)wp,&r,ui.brush?ui.brush:(HBRUSH)(COLOR_WINDOW+1));return 1;}
    }
    return DefWindowProcW(window,message,wp,lp);
}

static HWND control(const wchar_t *class_name,const wchar_t *text,DWORD style,int id)
{ return CreateWindowExW(0,class_name,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,ui.window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL); }

int data_ui_run(const wchar_t *root)
{
    if (wcslen(root)>=DATA_PATH-128) {MessageBoxW(NULL,L"The profile path is too long for data management.",L"Habumi",MB_OK|MB_ICONERROR);return 1;}
    const wchar_t *required[]={L"runtime\\bin\\config.txt",L"Data-Worker.ps1",L"Profile-Data.psm1",L"Profile-Growth.ps1",L"Profile-SystemInfo.ps1"};
    wchar_t file[DATA_PATH],mutex_name[128];
    for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);++i){swprintf(file,DATA_PATH,L"%ls\\%ls",root,required[i]);if(GetFileAttributesW(file)==INVALID_FILE_ATTRIBUTES){MessageBoxW(NULL,L"Data management requires a complete Habumi package and profile configuration.",L"Habumi",MB_OK|MB_ICONERROR);return 1;}}
    swprintf(mutex_name,128,L"Local\\HabumiData.%016llx",(unsigned long long)profile_id(root));
    HANDLE mutex=CreateMutexW(NULL,FALSE,mutex_name);
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=data_ui_existing(root);if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}CloseHandle(mutex);return 0;}
    ZeroMemory(&ui,sizeof(ui));wcscpy(ui.root,root);ui.dpi=96;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW wc={0};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(NULL);wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.lpszClassName=L"HabumiDataManagement";
    RegisterClassW(&wc);
    ui.window=CreateWindowExW(0,wc.lpszClassName,L"Habumi - Data management",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,520,470,NULL,NULL,wc.hInstance,NULL);
    if(!ui.window) {if(SUCCEEDED(com))CoUninitialize();CloseHandle(mutex);return 1;}
    SetPropW(ui.window,PROFILE_PROPERTY,(HANDLE)profile_id(root));
    ui.dpi=GetDpiForWindow(ui.window);
    ui.heading=control(L"STATIC",L"Data management",0,0);ui.profile=control(L"STATIC",root,SS_PATHELLIPSIS,0);
    ui.status=control(L"STATIC",L"Checking profile...",0,0);ui.action_label=control(L"STATIC",L"Action",0,0);
    ui.action=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,ACTION);
    for(int i=0;i<5;++i)SendMessageW(ui.action,CB_ADDSTRING,0,(LPARAM)operations[i]);
    SendMessageW(ui.action,CB_SETCURSEL,0,0);ui.description=control(L"STATIC",L"",0,0);
    ui.path_label=control(L"STATIC",L"Folder",0,0);ui.path=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,PATH_EDIT);
    SendMessageW(ui.path,EM_SETLIMITTEXT,DATA_PATH-1,0);ui.browse=control(L"BUTTON",L"Browse...",WS_TABSTOP,BROWSE);
    ui.image=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,IMAGE);
    SendMessageW(ui.image,CB_ADDSTRING,0,(LPARAM)L"GApps (data-gapps.img)");SendMessageW(ui.image,CB_ADDSTRING,0,(LPARAM)L"Android (data.img)");SendMessageW(ui.image,CB_SETCURSEL,0,0);
    ui.capacity=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,CAPACITY);
    SendMessageW(ui.capacity,CB_ADDSTRING,0,(LPARAM)L"8 GiB");SendMessageW(ui.capacity,CB_ADDSTRING,0,(LPARAM)L"12 GiB");SendMessageW(ui.capacity,CB_ADDSTRING,0,(LPARAM)L"16 GiB");SendMessageW(ui.capacity,CB_SETCURSEL,2,0);
    ui.size_info=control(L"STATIC",L"",0,0);ui.progress=control(PROGRESS_CLASSW,L"",PBS_MARQUEE,0);ui.result=control(L"STATIC",L"",SS_LEFT,0);
    ui.open=control(L"BUTTON",L"Open folder",WS_TABSTOP,OPEN_FOLDER);ui.start=control(L"BUTTON",L"Start Android",WS_TABSTOP,START_ANDROID);
    ui.run=control(L"BUTTON",L"Back up",WS_TABSTOP|BS_DEFPUSHBUTTON,RUN);ui.close=control(L"BUTTON",L"Close",WS_TABSTOP,CLOSE_UI);
    fonts();theme();SetWindowPos(ui.window,NULL,0,0,scale(520),scale(470),SWP_NOMOVE|SWP_NOZORDER);
    new_path();refresh();layout();ShowWindow(ui.window,SW_SHOW);SetTimer(ui.window,1,200,NULL);SetFocus(ui.action);
    MSG msg;
    while(GetMessageW(&msg,NULL,0,0)>0){
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE){SendMessageW(ui.window,WM_CLOSE,0,0);continue;}
        if(!IsDialogMessageW(ui.window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(ui.font)DeleteObject(ui.font);if(ui.heading_font)DeleteObject(ui.heading_font);if(ui.brush)DeleteObject(ui.brush);
    if(SUCCEEDED(com))CoUninitialize();CloseHandle(mutex);return 0;
}
