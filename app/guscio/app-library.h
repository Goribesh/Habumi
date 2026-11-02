/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_APP_LIBRARY_H
#define HABUMI_APP_LIBRARY_H
#include "guscio.h"
bool apps_prepare(void);
bool apps_requested(void);
bool apps_forward(HWND window);
LRESULT apps_receive(const COPYDATASTRUCT *data);
void apps_open(HWND owner,const Config *config);
bool apps_message(MSG *message);
void apps_tick(const Config *config,bool ready,bool closing);
bool apps_shutdown(void);
#endif
