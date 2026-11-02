/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include "fullscreen.h"
static int checks,failed;
#define CHECK(x) do{checks++;if(!(x)){failed++;printf("FAIL: %d\n",__LINE__);}}while(0)
int main(void)
{
    WNDCLASSA wc={0};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="HabumiFullscreenTest";RegisterClassA(&wc);
    HWND window=CreateWindowA(wc.lpszClassName,"Hidden fullscreen test",WS_OVERLAPPEDWINDOW,100,100,640,480,NULL,NULL,wc.hInstance,NULL);
    CHECK(window!=NULL);FullscreenState state={0};WINDOWPLACEMENT initial={.length=sizeof(initial)};
    CHECK(GetWindowPlacement(window,&initial));LONG_PTR style=GetWindowLongPtrA(window,GWL_STYLE);
    CHECK(fullscreen_toggle(window,&state));CHECK(state.active);CHECK(!(GetWindowLongPtrA(window,GWL_STYLE)&WS_CAPTION));
    MONITORINFO monitor={.cbSize=sizeof(monitor)};RECT r;GetMonitorInfoA(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);GetWindowRect(window,&r);
    CHECK(EqualRect(&r,&monitor.rcMonitor));CHECK(fullscreen_monitor(window,&state));
    CHECK(fullscreen_toggle(window,&state));CHECK(!state.active);CHECK(GetWindowLongPtrA(window,GWL_STYLE)==style);
    WINDOWPLACEMENT restored={.length=sizeof(restored)};GetWindowPlacement(window,&restored);
    CHECK(EqualRect(&restored.rcNormalPosition,&initial.rcNormalPosition));
    CHECK(!IsWindowVisible(window));CHECK(!fullscreen_toggle(NULL,&state));
    DestroyWindow(window);printf("Fullscreen: %d checks, %d failed\n",checks,failed);return failed?1:0;
}
