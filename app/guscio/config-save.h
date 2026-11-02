/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_CONFIG_SAVE_H
#define HABUMI_CONFIG_SAVE_H
#include <stdbool.h>
#include <stddef.h>
typedef struct { const char *key; const char *value; } ConfigChange;
/* Same-directory write/flush/rename. Existing comments and unknown keys stay.
 * A separate file guard serializes cooperating Habumi writers. */
bool config_save(const char *path, const ConfigChange *changes, size_t count,
                 char *error, size_t error_size);
#endif
