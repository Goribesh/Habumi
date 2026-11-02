/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_SETTINGS_H
#define HABUMI_SETTINGS_H
#include <windows.h>
#include "guscio.h"
void settings_open(HWND owner,const Config *startup,const char *path);
bool settings_message(MSG *message);
#endif
