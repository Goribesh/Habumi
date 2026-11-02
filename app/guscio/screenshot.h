/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_SCREENSHOT_H
#define HABUMI_SCREENSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/* adb is UTF-8, target is UTF-16. Call the API only from the UI owner thread. */
bool screenshot_start(const char *adb, int port, const wchar_t *target);
int screenshot_poll(wchar_t *message, size_t cap);
bool screenshot_stop(void);

#endif
