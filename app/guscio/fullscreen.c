/* SPDX-License-Identifier: GPL-2.0-only */
#include "fullscreen.h"
bool fullscreen_monitor(HWND window,const FullscreenState *state)
{
    MONITORINFO monitor={.cbSize=sizeof(monitor)};
    if(!state || !state->active || !GetMonitorInfoA(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return false;
    RECT r=monitor.rcMonitor;
    return SetWindowPos(window,NULL,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE)!=0;
}
bool fullscreen_toggle(HWND window,FullscreenState *state)
{
    if(!window || !state)return false;
    if(!state->active){
        state->placement.length=sizeof(state->placement);
        if(!GetWindowPlacement(window,&state->placement))return false;
        state->style=GetWindowLongPtrA(window,GWL_STYLE);
        SetLastError(0);
        if(!SetWindowLongPtrA(window,GWL_STYLE,state->style&~(LONG_PTR)WS_OVERLAPPEDWINDOW) && GetLastError())return false;
        state->active=true;
        if(fullscreen_monitor(window,state))return true;
        SetWindowLongPtrA(window,GWL_STYLE,state->style);state->active=false;
        SetWindowPlacement(window,&state->placement);return false;
    }
    SetLastError(0);
    if(!SetWindowLongPtrA(window,GWL_STYLE,state->style) && GetLastError())return false;
    state->active=false;
    WINDOWPLACEMENT placement=state->placement;
    if(!(state->style&WS_VISIBLE))placement.showCmd=SW_HIDE;
    bool restored=SetWindowPlacement(window,&placement)!=0;
    SetWindowPos(window,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
    return restored;
}
