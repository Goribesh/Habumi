/* SPDX-License-Identifier: GPL-2.0-only */
#include "config-edit.h"

#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

int main(void)
{
    char out[256];
    const char *source = "# keep=1\r\n  width  = 800  \r\nunknown=x\nwidth=900\n";
    const char *expected = "# keep=1\r\n  width  = 1024  \r\nunknown=x\nwidth=1024\n";
    CHECK(config_edit(source, "width", "1024", out, sizeof(out)) == strlen(expected));
    CHECK(strcmp(out, expected) == 0);
    CHECK(config_edit("# note\nother=x", "variant", "gapps", out, sizeof(out)) > 0);
    CHECK(strcmp(out, "# note\nother=x\nvariant=gapps\n") == 0);
    CHECK(config_edit("variante = vanilla\n", "variant", "gapps", out, sizeof(out)) > 0);
    CHECK(strcmp(out, "variante = vanilla\nvariant=gapps\n") == 0);
    CHECK(config_edit("memory=bad value  \n", "memory", "4096", out, sizeof(out)) > 0);
    CHECK(strcmp(out, "memory=4096  \n") == 0);
    CHECK(config_edit("width=1\n", "width", "2\nother=3", out, sizeof(out)) == 0);
    CHECK(out[0] == '\0');
    CHECK(config_edit("x=y\n", "width", "2", out, 5) == 0);
    CHECK(out[0] == '\0');
    CHECK(config_edit("width=1", "width", "2", out, 9) == strlen("width=2"));
    CHECK(strcmp(out, "width=2") == 0);
    CHECK(config_edit("# only\n", "bad key", "x", out, sizeof(out)) == 0);
    CHECK(config_edit("# only\n", "width", "x#y", out, sizeof(out)) == 0);
    CHECK(config_edit("# only\n", "not_supported", "x", out, sizeof(out)) == 0);
    strcpy(out, "width=1\n");
    CHECK(config_edit(out, "width", "2", out, sizeof(out)) == 0);
    CHECK(out[0] == '\0');
    printf("test-config-edit: %d checks passed\n", checks);
    return 0;
}
