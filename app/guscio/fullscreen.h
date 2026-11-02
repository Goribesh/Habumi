/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_FULLSCREEN_H
#define HABUMI_FULLSCREEN_H
#include <windows.h>
#include <stdbool.h>
typedef struct {bool active;LONG_PTR style;WINDOWPLACEMENT placement;} FullscreenState;
bool fullscreen_toggle(HWND window,FullscreenState *state);
bool fullscreen_monitor(HWND window,const FullscreenState *state);
#endif
