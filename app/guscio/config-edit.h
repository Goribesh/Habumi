/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_CONFIG_EDIT_H
#define HABUMI_CONFIG_EDIT_H

#include <stddef.h>

/* Rewrites all exact occurrences of a supported configuration key. Input and
 * output must not overlap. Returns the byte count, or zero on failure; failure
 * clears out[0] when out and capacity are valid, including for overlap. */
size_t config_edit(const char *input, const char *key, const char *value,
                   char *out, size_t capacity);

#endif
