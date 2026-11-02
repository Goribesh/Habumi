#ifndef APP_TARGET_H
#define APP_TARGET_H

#include <stdbool.h>
#include <stddef.h>

#define APP_COMPONENT_MAX 512
#define APP_LABEL_MAX 256
#define APP_LIBRARY_MAX 256

typedef struct {
    char component[APP_COMPONENT_MAX];
    char label[APP_LABEL_MAX];
} AppEntry;

bool app_component_valid(const char *component);
size_t app_library_parse(const char *utf8, AppEntry *entries, size_t capacity,
                         bool *truncated);

#endif
