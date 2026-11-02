/* SPDX-License-Identifier: GPL-2.0-only */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "config-save.h"

enum { SETTINGS_SAVE=700,SETTINGS_CLOSE,SETTINGS_FIELD=720,SETTINGS_AUDIO=740,SETTINGS_SCALE };
static const char *keys[]={"vcpu","memory","width","height","hz","battery_hz","density"};
static const char *labels[]={"CPU cores","Memory (MiB)","Width (pixels)","Height (pixels)","Refresh limit (Hz)","Battery limit (Hz)","Density (DPI)"};
static const int minimum[]={1,1024,320,320,24,24,120},maximum[]={64,65536,8192,8192,240,240,640};
typedef struct {
    HWND window,field[7],audio,scale,message;
    HFONT font;HBRUSH brush;COLORREF background,foreground;
    UINT dpi;char path[MAX_PATH],initial[9][32];
} Settings;
static Settings settings;
typedef struct {UINT old_dpi,new_dpi;} DpiChange;
static BOOL CALLBACK scale_child(HWND child,LPARAM parameter)
{
    if(GetParent(child)!=settings.window)return TRUE;
    DpiChange *change=(DpiChange*)parameter;RECT r;GetWindowRect(child,&r);
    MapWindowPoints(NULL,settings.window,(POINT*)&r,2);
    SetWindowPos(child,NULL,MulDiv(r.left,change->new_dpi,change->old_dpi),MulDiv(r.top,change->new_dpi,change->old_dpi),
        MulDiv(r.right-r.left,change->new_dpi,change->old_dpi),MulDiv(r.bottom-r.top,change->new_dpi,change->old_dpi),SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageA(child,WM_SETFONT,(WPARAM)settings.font,TRUE);return TRUE;
}
static int px(int value){return MulDiv(value,(int)settings.dpi,96);}
static HWND control(const char *kind,const char *text,DWORD style,int id,int x,int y,int w,int h)
{
    HWND child=CreateWindowExA(!strcmp(kind,"EDIT")?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|WS_VISIBLE|style,
        px(x),px(y),px(w),px(h),settings.window,(HMENU)(INT_PTR)id,GetModuleHandleA(NULL),NULL);
    SendMessageA(child,WM_SETFONT,(WPARAM)settings.font,TRUE);return child;
}
static void colors(void)
{
    DWORD light=1,size=sizeof(light);HIGHCONTRASTA high={sizeof(high),0,NULL};
    SystemParametersInfoA(SPI_GETHIGHCONTRAST,sizeof(high),&high,0);
    RegGetValueA(HKEY_CURRENT_USER,"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize","AppsUseLightTheme",RRF_RT_REG_DWORD,NULL,&light,&size);
    bool contrast=(high.dwFlags&HCF_HIGHCONTRASTON)!=0;
    settings.background=contrast?GetSysColor(COLOR_WINDOW):light?RGB(250,250,250):RGB(32,32,32);
    settings.foreground=contrast?GetSysColor(COLOR_WINDOWTEXT):light?RGB(28,28,28):RGB(240,240,240);
    HMODULE dwm=LoadLibraryA("dwmapi.dll");
    if(dwm){
        typedef HRESULT (WINAPI *SetAttribute)(HWND,DWORD,LPCVOID,DWORD);
        SetAttribute set=(SetAttribute)(void*)GetProcAddress(dwm,"DwmSetWindowAttribute");BOOL dark=!contrast && !light;
        if(set)set(settings.window,20,&dark,sizeof(dark));FreeLibrary(dwm);
    }
    if(settings.brush)DeleteObject(settings.brush);settings.brush=CreateSolidBrush(settings.background);
    InvalidateRect(settings.window,NULL,TRUE);
}
static void save(void)
{
    ConfigChange changes[9];char values[9][32],error[256];size_t count=0;
    for(int i=0;i<7;i++){
        char *end;GetWindowTextA(settings.field[i],values[i],sizeof(values[i]));
        long value=strtol(values[i],&end,10);
        if(!values[i][0] || *end || (value!=0 && (value<minimum[i] || value>maximum[i])) || (value==0 && i<4)){
            snprintf(error,sizeof(error),"%s: enter %d..%d%s.",labels[i],minimum[i],maximum[i],i>=4?", or 0 for automatic":"");
            SetWindowTextA(settings.message,error);SetFocus(settings.field[i]);return;
        }
        snprintf(values[i],sizeof(values[i]),"%ld",value);
        if(strcmp(values[i],settings.initial[i]))changes[count++]=(ConfigChange){keys[i],values[i]};
    }
    strcpy(values[7],SendMessageA(settings.audio,BM_GETCHECK,0,0)==BST_CHECKED?"on":"off");
    strcpy(values[8],SendMessageA(settings.scale,BM_GETCHECK,0,0)==BST_CHECKED?"yes":"no");
    if(strcmp(values[7],settings.initial[7]))changes[count++]=(ConfigChange){"audio",values[7]};
    if(strcmp(values[8],settings.initial[8]))changes[count++]=(ConfigChange){"guest_scale",values[8]};
    if(!count){SetWindowTextA(settings.message,"No settings changed.");return;}
    if(!config_save(settings.path,changes,count,error,sizeof(error))){SetWindowTextA(settings.message,error);return;}
    for(int i=0;i<9;i++)strcpy(settings.initial[i],values[i]);
    SetWindowTextA(settings.message,"Saved. Close and reopen Habumi to apply these changes.");
    registro_riga(REG_GUSCIO,"settings: saved atomically; restart required to apply changes");
}
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    switch(message){
    case WM_COMMAND:
        if(LOWORD(wp)==SETTINGS_SAVE)save();
        else if(LOWORD(wp)==SETTINGS_CLOSE || LOWORD(wp)==IDCANCEL)DestroyWindow(window);
        return 0;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:
        if(settings.font)DeleteObject(settings.font);if(settings.brush)DeleteObject(settings.brush);
        settings.window=NULL;settings.font=NULL;settings.brush=NULL;return 0;
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:colors();return 0;
    case WM_DPICHANGED:{
        DpiChange change={settings.dpi,HIWORD(wp)};HFONT old=settings.font;settings.dpi=change.new_dpi;
        settings.font=CreateFontA(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        EnumChildWindows(window,scale_child,(LPARAM)&change);const RECT *r=(const RECT*)lp;
        SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        if(old)DeleteObject(old);return 0;
    }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORBTN:
        SetTextColor((HDC)wp,settings.foreground);SetBkColor((HDC)wp,settings.background);return (LRESULT)settings.brush;
    case WM_ERASEBKGND:{RECT r;GetClientRect(window,&r);FillRect((HDC)wp,&r,settings.brush?settings.brush:(HBRUSH)(COLOR_WINDOW+1));return 1;}
    }
    return DefWindowProcA(window,message,wp,lp);
}
void settings_open(HWND owner,const Config *startup,const char *path)
{
    if(settings.window){ShowWindow(settings.window,SW_RESTORE);SetForegroundWindow(settings.window);return;}
    if(!startup || !path || strlen(path)>=MAX_PATH)return;
    memset(&settings,0,sizeof(settings));strcpy(settings.path,path);settings.dpi=GetDpiForWindow(owner);if(!settings.dpi)settings.dpi=96;
    Config saved;config_default(&saved);config_carica(&saved,path);
    int configured[]={saved.vcpu,saved.memoria,saved.larghezza,saved.altezza,saved.hz,saved.hz_batteria,saved.densita};
    int running[]={startup->vcpu,startup->memoria,startup->larghezza,startup->altezza,startup->hz,startup->hz_batteria,startup->densita};
    WNDCLASSA wc={0};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleA(NULL);wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.lpszClassName="HabumiSettings";RegisterClassA(&wc);
    RECT rect={0,0,px(500),px(520)};AdjustWindowRectEx(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,0);
    settings.window=CreateWindowExA(WS_EX_CONTROLPARENT,wc.lpszClassName,"Habumi - Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,owner,NULL,wc.hInstance,NULL);
    if(!settings.window)return;
    settings.font=CreateFontA(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");colors();
    control("STATIC","Settings",0,0,16,12,460,25);
    control("STATIC","Close and reopen Habumi to apply saved values.",0,0,16,43,460,22);
    control("STATIC","Graphics: native Vulkan (fixed backend in this build).",0,0,16,70,460,35);
    control("STATIC","Setting",0,0,16,115,180,20);control("STATIC","Saved",0,0,224,115,80,20);control("STATIC","Startup config",0,0,340,115,145,20);
    for(int i=0;i<7;i++){
        char text[32];snprintf(settings.initial[i],32,"%d",configured[i]);snprintf(text,32,"%d",running[i]);
        control("STATIC",labels[i],0,0,16,147+i*32,200,23);
        settings.field[i]=control("EDIT",settings.initial[i],WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,SETTINGS_FIELD+i,224,142+i*32,90,26);
        SendMessageA(settings.field[i],EM_SETLIMITTEXT,10,0);control("STATIC",text,0,0,340,147+i*32,130,23);
    }
    strcpy(settings.initial[7],saved.audio?"on":"off");strcpy(settings.initial[8],saved.scala_guest?"yes":"no");
    settings.audio=control("BUTTON","Audio",WS_TABSTOP|BS_AUTOCHECKBOX,SETTINGS_AUDIO,16,377,130,24);
    settings.scale=control("BUTTON","Scale guest to Windows DPI",WS_TABSTOP|BS_AUTOCHECKBOX,SETTINGS_SCALE,164,377,315,24);
    SendMessageA(settings.audio,BM_SETCHECK,saved.audio?BST_CHECKED:BST_UNCHECKED,0);SendMessageA(settings.scale,BM_SETCHECK,saved.scala_guest?BST_CHECKED:BST_UNCHECKED,0);
    control("STATIC","0 keeps automatic/default behavior. Startup config is the requested configuration; applied GPU/display values are not queried here.",0,0,16,412,466,47);
    settings.message=control("STATIC","",0,0,16,461,295,48);
    control("BUTTON","Save",WS_TABSTOP|BS_DEFPUSHBUTTON,SETTINGS_SAVE,320,476,75,28);control("BUTTON","Close",WS_TABSTOP,SETTINGS_CLOSE,405,476,75,28);
    ShowWindow(settings.window,SW_SHOW);SetFocus(settings.field[0]);
}
bool settings_message(MSG *message)
{return settings.window && IsDialogMessageA(settings.window,message);}
