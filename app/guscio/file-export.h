/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_FILE_EXPORT_H
#define HABUMI_FILE_EXPORT_H

#include <stdbool.h>
#include <windows.h>

HWND file_export_open(HWND owner, const wchar_t *root, int port);
bool file_export_message(MSG *message);
void file_export_tick(void);
bool file_export_busy(void);

#endif
