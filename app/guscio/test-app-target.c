#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app-target.h"

static int checks;
static int failures;

#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void validates_components(void)
{
    CHECK(app_component_valid("com.android.settings/.Settings"));
    CHECK(app_component_valid("org.example.app/org.example.app.Main$Inner"));
    CHECK(app_component_valid("org.example_2/.ui.Main"));
    CHECK(!app_component_valid(NULL));
    CHECK(!app_component_valid(""));
    CHECK(!app_component_valid("com..bad/.Main"));
    CHECK(!app_component_valid("com.example/"));
    CHECK(!app_component_valid("com.example/.Main."));
    CHECK(!app_component_valid("com.example/.Main/Extra"));
    CHECK(!app_component_valid("com.example/.Main;quit"));
    CHECK(!app_component_valid("com.example/ .Main"));
    CHECK(!app_component_valid("com.example/.Main\nother"));
    CHECK(!app_component_valid("com.example/.Main\""));
}

static void parses_sorted_unique_and_skips_noise(void)
{
    const char input[] = "Activity Resolver Table:\r\n"
        "com.zeta/.Z\r\n\r\n"
        "com.android.settings/.Settings\n"
        "com.zeta/.Z\ninvalid\ncom.alpha/com.alpha.Main$Nested\n";
    AppEntry entries[APP_LIBRARY_MAX];
    bool truncated = true;
    size_t n = app_library_parse(input, entries, APP_LIBRARY_MAX, &truncated);
    CHECK(n == 3);
    CHECK(!truncated);
    CHECK(strcmp(entries[0].component, "com.alpha/com.alpha.Main$Nested") == 0);
    CHECK(strcmp(entries[1].component, "com.android.settings/.Settings") == 0);
    CHECK(strcmp(entries[2].component, "com.zeta/.Z") == 0);
    CHECK(strcmp(entries[1].label, "com.android.settings/.Settings") == 0);
}

static void parses_and_sanitizes_labels(void)
{
    const char input[] = "com.android.settings/.Settings\tSettings\r\n"
        "com.example/.Main\tFirst\tInjected\n"
        "com.example/.Other\tLine\rBreak\n"
        "com.example/.Nested\tCafe\303\251\302\205\n"
        "com.example/.Old\n";
    AppEntry entries[APP_LIBRARY_MAX];
    bool truncated = false;
    size_t n = app_library_parse(input, entries, APP_LIBRARY_MAX, &truncated);
    CHECK(n == 4);
    CHECK(!truncated);
    CHECK(strcmp(entries[0].label, "Settings") == 0);
    CHECK(strcmp(entries[1].label, "Cafe\303\251 ") == 0);
    CHECK(strcmp(entries[2].label, "com.example/.Old") == 0);
    CHECK(strcmp(entries[3].label, "Line Break") == 0);
}

static void truncates_long_labels_on_utf8_boundaries(void)
{
    char input[APP_LABEL_MAX + 64];
    AppEntry entries[1];
    bool truncated = false;
    size_t n = (size_t)snprintf(input, sizeof(input), "com.example/.Main\t");
    while (n + 2 < sizeof(input)) {
        input[n++] = (char)0xc3;
        input[n++] = (char)0xa9;
    }
    input[n] = '\0';
    CHECK(app_library_parse(input, entries, 1, &truncated) == 1);
    CHECK(truncated);
    CHECK(strlen(entries[0].label) == APP_LABEL_MAX - 2);
    CHECK((unsigned char)entries[0].label[APP_LABEL_MAX - 4] == 0xc3);
    CHECK((unsigned char)entries[0].label[APP_LABEL_MAX - 3] == 0xa9);
}

static void truncates_component_fallback_label(void)
{
    char component[APP_COMPONENT_MAX];
    AppEntry entry[1];
    bool truncated = false;
    size_t prefix = strlen("com.example/");
    memcpy(component, "com.example/", prefix);
    memset(component + prefix, 'A', 300);
    component[prefix + 300] = '\0';
    CHECK(app_library_parse(component, entry, 1, &truncated) == 1);
    CHECK(truncated);
    CHECK(strlen(entry[0].label) == APP_LABEL_MAX - 1);
}

static void reports_capacity_and_input_limits(void)
{
    AppEntry entries[2];
    bool truncated = false;
    char *large = (char *)malloc(128u * 1024u + 2u);
    char long_component[APP_COMPONENT_MAX + 1];
    size_t n = app_library_parse("com.z/.Z\ncom.a/.A\ncom.b/.B\n",
                                 entries, 2, &truncated);
    CHECK(n == 2 && truncated);
    CHECK(strcmp(entries[0].component, "com.a/.A") == 0);
    CHECK(strcmp(entries[1].component, "com.b/.B") == 0);
    CHECK(app_library_parse("", entries, 2, &truncated) == 0);
    CHECK(!truncated);
    CHECK(app_library_parse(NULL, entries, 2, &truncated) == 0);
    CHECK(app_library_parse("com.a/.A\n", NULL, 2, &truncated) == 0);
    memset(long_component, 'a', sizeof(long_component));
    long_component[sizeof(long_component) - 1] = '\0';
    CHECK(!app_component_valid(long_component));
    CHECK(large != NULL);
    if (large) {
        memset(large, 'x', 128u * 1024u + 1u);
        large[128u * 1024u + 1u] = '\0';
        CHECK(app_library_parse(large, entries, 2, &truncated) == 0);
        CHECK(truncated);
        free(large);
    }
}

int main(void)
{
    validates_components();
    parses_sorted_unique_and_skips_noise();
    parses_and_sanitizes_labels();
    truncates_long_labels_on_utf8_boundaries();
    truncates_component_fallback_label();
    reports_capacity_and_input_limits();
    printf("test-app-target: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
