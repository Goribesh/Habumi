/* SPDX-License-Identifier: GPL-2.0-only */
/* Own-process stand-in for the staged maintenance runner; never boots a VM. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char mode[32] = "ok", token[33] = "", line[256];
    const char *log = NULL;
    int tcg = 0, cpu = 0;
    FILE *file;
    int i;
    if (GetConsoleWindow() != NULL) return 42;
    file = fopen("maintenance-probe-mode.txt", "r");
    if (file) { (void)fgets(mode, sizeof(mode), file); fclose(file); }
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-accel") && i + 1 < argc &&
            !strcmp(argv[i + 1], "tcg,thread=single")) tcg = 1;
        if (!strcmp(argv[i], "-cpu") && i + 1 < argc &&
            !strcmp(argv[i + 1], "max")) cpu = 1;
        const char *marker = strstr(argv[i], "habumi.maintenance=");
        if (marker) { memcpy(token, marker + 19, 32); token[32] = 0; }
        if (!strcmp(argv[i], "-serial") && i + 1 < argc &&
            !strncmp(argv[i + 1], "file:", 5)) log = argv[i + 1] + 5;
    }
    if (!strncmp(mode, "timeout", 7)) { Sleep(30000); return 0; }
    if (!log || !token[0] || !tcg || !cpu) return 43;
    /* More than a pipe buffer on both streams tests asynchronous draining. */
    memset(line, 'x', sizeof(line) - 1); line[sizeof(line) - 1] = 0;
    for (i = 0; i < 512; ++i) { puts(line); fprintf(stderr, "%s\n", line); }
    file = fopen(log, "w");
    if (!file) return 44;
    if (strncmp(mode, "missing", 7))
        fprintf(file, "HABUMI-MAINT v1 %s OK\n",
                !strncmp(mode, "badnonce", 8) ? "wrong" : token);
    fclose(file);
    return !strncmp(mode, "nonzero", 7) ? 7 : 0;
}
