/* SPDX-License-Identifier: GPL-2.0-only */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app-library.h"
#include "app-target.h"
#include "app-shortcut.h"

enum { APP_REFRESH=810,APP_OPEN,APP_LINK,APP_LIST,APP_CLOSE };
#define APP_IPC 0x48415031u
typedef struct { wchar_t root[MAX_PATH]; char component[APP_COMPONENT_MAX]; } AppRequest;
typedef struct {
    Config config;
    char root[MAX_PATH];
    HANDLE thread;
    AppEntry entries[APP_LIBRARY_MAX];
    size_t count;
    bool ok,truncated;
    unsigned generation;
} AppScan;
static AppScan scan;
static HWND window,list,status,open_button,link_button,refresh_button;
static HFONT font;
static UINT dpi;
static wchar_t root[MAX_PATH];
static char pending[APP_COMPONENT_MAX];
static AdbComando *launch;
static bool stopped;
static bool was_ready;
static unsigned generation;
static volatile LONG scan_cancelled;

static void notice(const wchar_t *text)
{
    if(status)SetWindowTextW(status,text);
}
static bool queue(const char *component)
{
    if(stopped || pending[0] || launch || !app_component_valid(component))return false;
    strcpy(pending,component);notice(L"Waiting for Android...");return true;
}
bool apps_prepare(void)
{
    wchar_t value[APP_COMPONENT_MAX];
    DWORD n=GetCurrentDirectoryW(MAX_PATH,root);
    if(!n || n>=MAX_PATH)return false;
    n=GetEnvironmentVariableW(L"HABUMI_LAUNCH_COMPONENT",value,APP_COMPONENT_MAX);
    if(!n)return GetLastError()==ERROR_ENVVAR_NOT_FOUND;
    if(n>=APP_COMPONENT_MAX)return false;
    char component[APP_COMPONENT_MAX];
    for(DWORD i=0;i<=n;i++){
        if(value[i]>127)return false;
        component[i]=(char)value[i];
    }
    return queue(component);
}
bool apps_requested(void){return pending[0]!=0;}
bool apps_forward(HWND target)
{
    AppRequest request={0};DWORD_PTR accepted=0;
    if(!pending[0])return false;
    wcscpy(request.root,root);strcpy(request.component,pending);
    COPYDATASTRUCT data={APP_IPC,sizeof(request),&request};
    return SendMessageTimeoutW(target,WM_COPYDATA,0,(LPARAM)&data,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&accepted) && accepted==1;
}
LRESULT apps_receive(const COPYDATASTRUCT *data)
{
    if(!data || data->dwData!=APP_IPC || data->cbData!=sizeof(AppRequest) || !data->lpData)return 0;
    const AppRequest *request=data->lpData;
    if(!wmemchr(request->root,0,MAX_PATH) || !memchr(request->component,0,APP_COMPONENT_MAX) ||
       _wcsicmp(root,request->root))return 0;
    return queue(request->component)?1:0;
}
static DWORD WINAPI read_apps(void *unused)
{
    (void)unused;
    char command[1024],out[512];
    if(InterlockedCompareExchange(&scan_cancelled,0,0))return 0;
    int n=snprintf(command,sizeof(command),"push \"%s\\runtime\\tools\\habumi-app-list.jar\" /data/local/tmp/habumi-app-list.jar",scan.root);
    if(n<0 || n>=(int)sizeof(command) || !adb_esegui_grezzo(&scan.config,command,out,sizeof(out)))return 0;
    if(InterlockedCompareExchange(&scan_cancelled,0,0))return 0;
    char *result=calloc(128*1024+1,1);
    if(!result)return 0;
    if(adb_esegui(&scan.config,"CLASSPATH=/data/local/tmp/habumi-app-list.jar app_process /system/bin com.habumi.tools.AppList",result,128*1024+1)){
        if(InterlockedCompareExchange(&scan_cancelled,0,0)){free(result);return 0;}
        scan.count=app_library_parse(result,scan.entries,APP_LIBRARY_MAX,&scan.truncated);
        scan.ok=scan.count>0;
    }
    free(result);return 0;
}
static void refresh(void)
{
    if(stopped || scan.thread)return;
    if(vm_stato()!=VM_PRONTO){notice(L"Android must be ready before refreshing the app list.");return;}
    scan.count=0;scan.ok=false;scan.truncated=false;scan.generation=generation;
    InterlockedExchange(&scan_cancelled,0);
    SendMessageW(list,LB_RESETCONTENT,0,0);
    scan.thread=CreateThread(NULL,0,read_apps,NULL,0,NULL);
    if(!scan.thread){notice(L"Could not start the app list reader.");return;}
    notice(L"Reading installed apps...");EnableWindow(refresh_button,FALSE);
    EnableWindow(open_button,FALSE);EnableWindow(link_button,FALSE);
}
static const AppEntry *selected(void)
{
    if(scan.thread)return NULL;
    LRESULT index=SendMessageW(list,LB_GETCURSEL,0,0);
    return index!=LB_ERR && (size_t)index<scan.count?&scan.entries[index]:NULL;
}
static void shortcut(void)
{
    const AppEntry *entry=selected();if(!entry)return;
    BROWSEINFOW browse={0};browse.hwndOwner=window;
    browse.lpszTitle=L"Choose where to save the Windows shortcut";
    browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    HRESULT initialized=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE folder=SHBrowseForFolderW(&browse);
    wchar_t directory[MAX_PATH],launcher[MAX_PATH],component[APP_COMPONENT_MAX],label[256],saved[4096],error[512];
    if(folder && SHGetPathFromIDListW(folder,directory) && swprintf(launcher,MAX_PATH,L"%ls\\Habumi.exe",root)>0){
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,entry->component,-1,component,APP_COMPONENT_MAX);
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,entry->label,-1,label,256))label[0]=0;
        notice(app_shortcut_create_named(launcher,component,label,directory,saved,4096,error,512)?L"Windows shortcut saved. It starts this profile and waits for Android.":error);
    }
    if(folder)CoTaskMemFree(folder);
    if(SUCCEEDED(initialized))CoUninitialize();
}
static int px(int n){return MulDiv(n,dpi,96);}
static BOOL CALLBACK apply_font(HWND child,LPARAM parameter){SendMessageW(child,WM_SETFONT,(WPARAM)parameter,TRUE);return TRUE;}
static HWND control(const wchar_t *kind,const wchar_t *text,DWORD style,int id,int x,int y,int w,int h)
{
    HWND child=CreateWindowExW(!wcscmp(kind,L"LISTBOX")?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|WS_VISIBLE|style,
        px(x),px(y),px(w),px(h),window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE);return child;
}
static void layout(void)
{
    RECT r;GetClientRect(window,&r);int width=r.right,height=r.bottom;
    SetWindowPos(list,NULL,px(12),px(12),width-px(24),height-px(110),SWP_NOZORDER);
    SetWindowPos(status,NULL,px(12),height-px(88),width-px(24),px(36),SWP_NOZORDER);
    SetWindowPos(refresh_button,NULL,px(12),height-px(42),px(84),px(28),SWP_NOZORDER);
    SetWindowPos(open_button,NULL,px(104),height-px(42),px(84),px(28),SWP_NOZORDER);
    SetWindowPos(link_button,NULL,px(196),height-px(42),px(160),px(28),SWP_NOZORDER);
}
static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM wp,LPARAM lp)
{
    switch(m){
    case WM_COMMAND:
        if(LOWORD(wp)==APP_REFRESH)refresh();
        else if(LOWORD(wp)==APP_LINK)shortcut();
        else if(LOWORD(wp)==APP_OPEN || (LOWORD(wp)==APP_LIST && HIWORD(wp)==LBN_DBLCLK)){
            const AppEntry *entry=selected();
            if(entry && !queue(entry->component))notice(L"An app launch is already pending, or Android is closing.");
        }else if(LOWORD(wp)==IDCANCEL || LOWORD(wp)==APP_CLOSE)DestroyWindow(h);
        return 0;
    case WM_SIZE:if(list)layout();return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lp)->ptMinTrackSize.x=px(380);((MINMAXINFO*)lp)->ptMinTrackSize.y=px(260);return 0;
    case WM_DPICHANGED:{
        dpi=HIWORD(wp);const RECT *rect=(const RECT*)lp;
        HFONT previous=font;font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        EnumChildWindows(h,apply_font,(LPARAM)font);if(previous)DeleteObject(previous);
        SetWindowPos(h,NULL,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
        layout();return 0;
    }
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:window=NULL;list=NULL;status=NULL;if(font)DeleteObject(font);font=NULL;return 0;
    }
    return DefWindowProcW(h,m,wp,lp);
}
void apps_open(HWND owner,const Config *config)
{
    if(window){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return;}
    if(!config || stopped)return;
    if(!scan.thread){scan.config=*config;GetCurrentDirectoryA(MAX_PATH,scan.root);}
    dpi=GetDpiForWindow(owner);if(!dpi)dpi=96;
    WNDCLASSW cls={0};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(NULL);
    cls.hCursor=LoadCursor(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);cls.lpszClassName=L"HabumiApps";RegisterClassW(&cls);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Apps - Habumi",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,
        CW_USEDEFAULT,CW_USEDEFAULT,px(480),px(480),owner,NULL,cls.hInstance,NULL);
    if(!window)return;
    font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    list=control(L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,APP_LIST,12,12,440,310);
    status=control(L"STATIC",L"",0,0,12,330,440,36);
    refresh_button=control(L"BUTTON",L"Refresh",WS_TABSTOP,APP_REFRESH,12,380,84,28);
    open_button=control(L"BUTTON",L"Open",WS_TABSTOP,APP_OPEN,104,380,84,28);
    link_button=control(L"BUTTON",L"Create shortcut...",WS_TABSTOP,APP_LINK,196,380,160,28);
    layout();ShowWindow(window,SW_SHOW);refresh();
}
bool apps_message(MSG *message){return window && IsDialogMessageW(window,message);}
void apps_tick(const Config *config,bool ready,bool closing)
{
    if(closing){stopped=true;InterlockedExchange(&scan_cancelled,1);}
    if(was_ready && !ready){generation++;pending[0]=0;InterlockedExchange(&scan_cancelled,1);}
    was_ready=ready;
    if(scan.thread && WaitForSingleObject(scan.thread,0)==WAIT_OBJECT_0){
        CloseHandle(scan.thread);scan.thread=NULL;
        if(window){
            SendMessageW(list,LB_RESETCONTENT,0,0);
            if(scan.ok && ready && !stopped && scan.generation==generation){
                for(size_t i=0;i<scan.count;i++){
                    wchar_t label[256];
                    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,scan.entries[i].label,-1,label,256))wcscpy(label,L"App");
                    SendMessageW(list,LB_ADDSTRING,0,(LPARAM)label);
                }
                SendMessageW(list,LB_SETCURSEL,0,0);
                notice(scan.truncated?L"App list limited to 256 entries. Open and shortcuts use the selected app.":L"Select an app to open it or create a Windows shortcut.");
            }else{scan.count=0;notice(L"Could not read apps. Check Android is ready and debug access is enabled.");}
            EnableWindow(refresh_button,!stopped);EnableWindow(open_button,scan.count>0);EnableWindow(link_button,scan.count>0);
        }
    }
    if(launch){
        char output[256]={0};bool truncated=false;
        AdbSpegniStato state=adb_comando_passo(launch,output,sizeof(output),&truncated);
        if(stopped || !ready || state!=ADB_SPEGNI_ATTESA){
            bool ok=!stopped && ready && state==ADB_SPEGNI_OK && !truncated && !strstr(output,"Error") && !strstr(output,"Exception");
            if(adb_comando_chiudi(launch)){
                launch=NULL;notice(ok?L"App opened.":L"App launch failed or was cancelled. Refresh the list and retry.");
                registro_riga(REG_GUSCIO,"app launch: %s",ok?"completed":"failed or cancelled");
            }
        }
    }
    if(stopped){pending[0]=0;return;}
    if(pending[0] && ready && !launch){
        char command[APP_COMPONENT_MAX+64];
        snprintf(command,sizeof(command),"\"am start -W -n '%s'\"",pending);
        launch=adb_comando_avvia(config,command);pending[0]=0;
        notice(launch?L"Opening app...":L"Could not start the app launch request.");
    }
}
bool apps_shutdown(void)
{
    stopped=true;pending[0]=0;
    InterlockedExchange(&scan_cancelled,1);
    if(window)DestroyWindow(window);
    if(launch && adb_comando_chiudi(launch))launch=NULL;
    /* The read-only listing worker owns its storage until process exit. */
    return !scan.thread || WaitForSingleObject(scan.thread,0)==WAIT_OBJECT_0;
}
