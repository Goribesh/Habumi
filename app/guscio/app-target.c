/* app-target.c -- validates and collects Android launcher components. */
#include <string.h>
#include "app-target.h"

#define APP_INPUT_MAX (128u * 1024u)

static bool ascii_letter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool package_name(const char *p, size_t n)
{
    size_t i = 0;
    bool start = true;

    if (!n) { return false; }
    while (i < n) {
        unsigned char c = (unsigned char)p[i++];
        if (c == '.') {
            if (start) { return false; }
            start = true;
        } else if (start) {
            if (!ascii_letter(c) && c != '_') { return false; }
            start = false;
        } else if (!ascii_letter(c) && (c < '0' || c > '9') && c != '_') {
            return false;
        }
    }
    return !start;
}

static bool activity_name(const char *p, size_t n)
{
    size_t i = 0;
    bool start = true;

    if (!n) { return false; }
    if (p[0] == '.') { i++; }
    if (i == n) { return false; }
    while (i < n) {
        unsigned char c = (unsigned char)p[i++];
        if (c == '.') {
            if (start) { return false; }
            start = true;
        } else if (start) {
            if (!ascii_letter(c) && c != '_' && c != '$') { return false; }
            start = false;
        } else if (!ascii_letter(c) && (c < '0' || c > '9') &&
                   c != '_' && c != '$') {
            return false;
        }
    }
    return !start;
}

bool app_component_valid(const char *component)
{
    const char *slash;
    size_t n;

    if (!component) { return false; }
    n = strlen(component);
    if (!n || n >= APP_COMPONENT_MAX) { return false; }
    slash = strchr(component, '/');
    if (!slash || strchr(slash + 1, '/')) { return false; }
    return package_name(component, (size_t)(slash - component)) &&
           activity_name(slash + 1, n - (size_t)(slash + 1 - component));
}

static void insert_component(AppEntry *entries, size_t *count, size_t capacity,
                             const char *component, size_t component_length,
                             const char *label, size_t label_length,
                             bool *truncated)
{
    char clean_label[APP_LABEL_MAX];
    size_t clean_length = 0, i = 0;
    bool label_valid = true;
    size_t at = 0;

    while (i < label_length) {
        unsigned char first = (unsigned char)label[i];
        size_t width = 1, output_width;
        unsigned value;
        if (first < 0x80) {
            value = first;
        } else if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
            value = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            value = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            value = first & 0x07;
        } else {
            label_valid = false;
            break;
        }
        if (i + width > label_length) { label_valid = false; break; }
        for (size_t j = 1; j < width; j++) {
            unsigned char next = (unsigned char)label[i + j];
            if ((next & 0xc0) != 0x80) { label_valid = false; break; }
            value = (value << 6) | (next & 0x3f);
        }
        if (!label_valid || (width == 3 && value < 0x800) ||
            (width == 4 && value < 0x10000) ||
            (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) {
            label_valid = false;
            break;
        }
        output_width = value < 0x20 || (value >= 0x7f && value <= 0x9f)
                       ? 1 : width;
        if (clean_length + output_width > APP_LABEL_MAX - 1) {
            *truncated = true;
            break;
        }
        if (value < 0x20 || (value >= 0x7f && value <= 0x9f)) {
            clean_label[clean_length++] = ' ';
        } else {
            memcpy(clean_label + clean_length, label + i, width);
            clean_length += width;
        }
        i += width;
    }
    if (!label_valid) { return; }
    clean_label[clean_length] = '\0';
    if (!clean_length) {
        clean_length = component_length < APP_LABEL_MAX - 1
                       ? component_length : APP_LABEL_MAX - 1;
        memcpy(clean_label, component, clean_length);
        clean_label[clean_length] = '\0';
        if (clean_length < component_length) { *truncated = true; }
    }

    while (at < *count && strcmp(entries[at].component, component) < 0) { at++; }
    if (at < *count && strcmp(entries[at].component, component) == 0) { return; }
    if (!capacity || !entries) {
        *truncated = true;
        return;
    }
    if (*count == capacity) {
        *truncated = true;
        if (at == capacity) { return; }
    } else {
        (*count)++;
    }
    if (at + 1 < *count) {
        memmove(&entries[at + 1], &entries[at],
                (*count - at - 1) * sizeof(entries[0]));
    }
    memcpy(entries[at].component, component, component_length);
    entries[at].component[component_length] = '\0';
    memcpy(entries[at].label, clean_label, clean_length + 1);
}

size_t app_library_parse(const char *utf8, AppEntry *entries, size_t capacity,
                         bool *truncated)
{
    size_t length = 0, count = 0, pos = 0;
    bool cut = false;

    if (truncated) { *truncated = false; }
    if (!utf8 || (capacity && !entries)) { return 0; }
    if (capacity > APP_LIBRARY_MAX) { capacity = APP_LIBRARY_MAX; }
    while (length <= APP_INPUT_MAX && utf8[length]) { length++; }
    if (length > APP_INPUT_MAX) {
        if (truncated) { *truncated = true; }
        return 0;
    }
    while (pos < length) {
        size_t begin = pos, n, component_length, label_length;
        const char *label;
        char component[APP_COMPONENT_MAX];
        const char *tab;
        while (pos < length && utf8[pos] != '\n') { pos++; }
        n = pos - begin;
        if (pos < length) { pos++; }
        if (n && utf8[begin + n - 1] == '\r') { n--; }
        if (!n) { continue; }
        tab = memchr(utf8 + begin, '\t', n);
        component_length = tab ? (size_t)(tab - (utf8 + begin)) : n;
        label = tab ? tab + 1 : utf8 + begin;
        label_length = tab ? n - component_length - 1 : component_length;
        if (tab && memchr(tab + 1, '\t', label_length)) { continue; }
        if (!component_length || component_length >= sizeof(component)) { continue; }
        memcpy(component, utf8 + begin, component_length);
        component[component_length] = '\0';
        if (app_component_valid(component)) {
            insert_component(entries, &count, capacity, component,
                             component_length, label, label_length, &cut);
        }
    }
    if (truncated) { *truncated = cut; }
    return count;
}
