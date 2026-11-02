/* SPDX-License-Identifier: GPL-2.0-only */
#include "config-edit.h"

#include <stdint.h>
#include <string.h>

static int field_valid(const char *s, size_t limit)
{
    size_t n;
    const unsigned char *p;

    if (s == NULL || (n = strlen(s)) == 0 || n > limit) return 0;
    for (p = (const unsigned char *)s; *p; ++p) {
        if (*p <= 32 || *p == 127 || *p == '#') return 0;
    }
    return 1;
}

static int supported(const char *key)
{
    static const char *const keys[] = {
        "vcpu", "memory", "width", "height", "retries", "adb_port",
        "clipboard_port", "gl", "audio", "gl_flush_wait", "guest_scale",
        "variant", "hz", "battery_hz", "density"
    };
    size_t i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (strcmp(key, keys[i]) == 0) return 1;
    return 0;
}

static int line_matches(const char *line, size_t length, const char *key,
                        const char **value_start, const char **value_end)
{
    const char *p = line, *end = line + length, *token;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p < end && *p == '#') return 0;
    token = p;
    while (p < end && *p != ' ' && *p != '\t' && *p != '=') ++p;
    if ((size_t)(p - token) != strlen(key) ||
        memcmp(token, key, strlen(key)) != 0) return 0;
    if (p == end || (*p != ' ' && *p != '\t' && *p != '=')) return 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p == end || *p++ != '=') return 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    *value_start = p;
    p = end;
    while (p > *value_start &&
           (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\r')) --p;
    *value_end = p;
    return 1;
}

size_t config_edit(const char *input, const char *key, const char *value,
                   char *out, size_t capacity)
{
    size_t in_len, key_len, value_len, used = 0, pos = 0;
    int found = 0;

    if (out == NULL || capacity == 0) return 0;
    if (input == NULL || !field_valid(key, 48) || !supported(key) ||
        !field_valid(value, 64)) {
        out[0] = '\0';
        return 0;
    }
    in_len = strlen(input); key_len = strlen(key); value_len = strlen(value);
    if (((uintptr_t)input <= (uintptr_t)out &&
         (uintptr_t)out - (uintptr_t)input < in_len + 1) ||
        ((uintptr_t)out < (uintptr_t)input &&
         (uintptr_t)input - (uintptr_t)out < capacity)) {
        out[0] = '\0';
        return 0;
    }
    out[0] = '\0';

    while (pos < in_len) {
        size_t end = pos, line_len, copy_len;
        const char *vs, *ve;
        while (end < in_len && input[end] != '\n') ++end;
        line_len = end - pos;
        if (line_matches(input + pos, line_len, key, &vs, &ve)) {
            size_t a = (size_t)(vs - (input + pos));
            size_t b = (size_t)(ve - (input + pos));
            copy_len = a + value_len + line_len - b + (end < in_len ? 1 : 0);
            if (copy_len >= capacity - used) goto fail;
            memcpy(out + used, input + pos, a); used += a;
            memcpy(out + used, value, value_len); used += value_len;
            memcpy(out + used, input + pos + b, line_len - b); used += line_len - b;
            if (end < in_len) out[used++] = '\n';
            found = 1;
        } else {
            copy_len = line_len + (end < in_len ? 1 : 0);
            if (copy_len >= capacity - used) goto fail;
            memcpy(out + used, input + pos, copy_len); used += copy_len;
        }
        pos = end + (end < in_len ? 1 : 0);
    }
    if (!found) {
        size_t extra = key_len + 1 + value_len + 1;
        if (used && out[used - 1] != '\n') {
            if (extra >= capacity - used) goto fail;
            out[used++] = '\n';
        } else if (extra >= capacity - used) goto fail;
        memcpy(out + used, key, key_len); used += key_len;
        out[used++] = '=';
        memcpy(out + used, value, value_len); used += value_len;
        out[used++] = '\n';
    }
    out[used] = '\0';
    return used;
fail:
    out[0] = '\0';
    return 0;
}
