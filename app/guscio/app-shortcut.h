/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_APP_SHORTCUT_H
#define HABUMI_APP_SHORTCUT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

bool app_shortcut_create(const wchar_t *launcher, const wchar_t *component,
                         const wchar_t *directory, wchar_t *saved,
                         size_t capacity, wchar_t *error,
                         size_t error_capacity);
bool app_shortcut_create_named(const wchar_t *launcher,const wchar_t *component,
    const wchar_t *label,const wchar_t *directory,wchar_t *saved,size_t capacity,
    wchar_t *error,size_t error_capacity);

#endif
