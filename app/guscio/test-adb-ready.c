/* SPDX-License-Identifier: GPL-2.0-only */
/* Isolated test for asynchronous adb_pronto. Run only from
 * research/results/ui-startup-20261013/test-adb-ready.exe. */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guscio.h"

/* Include the implementation to test the total deadline without waiting. */
#include "adb.c"

static unsigned checks, failures;
static char test_dir[MAX_PATH], fake_adb[MAX_PATH], marker[MAX_PATH];
static bool made_fake, made_marker, helper_parked;

#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static bool ends_with_ci(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && _stricmp(s + a - b, suffix) == 0;
}

static int child_mode(int argc, char **argv)
{
    char image[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, image, MAX_PATH);
    const char *env = getenv("HABUMI_ADB_READY_TEST_MODE");
    if (!n || n >= MAX_PATH || !ends_with_ci(image,
        "\\research\\results\\ui-startup-20261013\\adb.exe")) return -1;
    char *slash = strrchr(image, '\\');
    if (!slash || snprintf(marker, sizeof(marker), "%.*s\\reconnect.marker",
                            (int)(slash - image), image) >= (int)sizeof(marker)) return 98;
    if (argc == 6 && !strcmp(argv[1], "-s") &&
        !strcmp(argv[2], "127.0.0.1:16555") && !strcmp(argv[3], "shell") &&
        !strcmp(argv[4], "reboot") && !strcmp(argv[5], "-p")) {
        return 0; /* VM integration owns a fake process, never a real guest. */
    }
    if (argc == 6 && !strcmp(argv[1], "-s") &&
        !strcmp(argv[2], "127.0.0.1:16555") && !strcmp(argv[3], "shell") &&
        !strcmp(argv[4], "getprop") && !strcmp(argv[5], "sys.boot_completed")) {
        if (env && !strcmp(env, "hang")) { Sleep(30000); return 0; }
        if (env && !strcmp(env, "reconnect")) {
            if (GetFileAttributesA(marker) == INVALID_FILE_ATTRIBUTES) return 7;
            puts("1"); return 0;
        }
        if (env && !strcmp(env, "queryfail")) return 7;
        if (env && !strcmp(env, "notready")) { puts("0"); return 0; }
        if (env && !strcmp(env, "misleading")) { puts("error version1"); return 0; }
        if (env && !strcmp(env, "noisy")) {
            for (int i = 0; i < 2000; ++i) putchar(' ');
            puts("1"); return 0;
        }
        puts(env && !strcmp(env, "ready-space") ? " \t1\r\n" : "1");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "connect") &&
        !strcmp(argv[2], "127.0.0.1:16555")) {
        if (env && !strcmp(env, "queryfail")) return 7;
        if (env && !strcmp(env, "reconnect")) {
            HANDLE h = CreateFileA(marker, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) return 8;
            CloseHandle(h); made_marker = true;
        }
        puts("connected"); return 0;
    }
    fprintf(stderr, "fake adb: invalid argv\n");
    return 90;
}

static bool set_mode(const char *mode)
{
    return SetEnvironmentVariableA("HABUMI_ADB_READY_TEST_MODE", mode) != 0;
}

static bool wait_result(AdbPreparazione *r, AdbProntoStato expected,
                        DWORD limit_ms)
{
    ULONGLONG end = GetTickCount64() + limit_ms;
    AdbProntoStato s;
    do {
        s = adb_pronto_passo(r);
        if (s != ADB_PRONTO_ATTESA) return s == expected;
        Sleep(10);
    } while (GetTickCount64() < end);
    return false;
}

static AdbPreparazione *start(Config *c)
{
    LARGE_INTEGER before, after, freq;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&before);
    AdbPreparazione *r = adb_pronto_avvia(c);
    QueryPerformanceCounter(&after);
    CHECK((after.QuadPart - before.QuadPart) * 1000 / freq.QuadPart < 1000);
    return r;
}

static bool paths_init(void)
{
    char image[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, image, MAX_PATH);
    if (!n || n >= MAX_PATH || !ends_with_ci(image,
        "\\research\\results\\ui-startup-20261013\\test-adb-ready.exe")) return false;
    strcpy_s(test_dir, sizeof(test_dir), image);
    char *slash = strrchr(test_dir, '\\');
    if (!slash) return false;
    *slash = '\0';
    if (snprintf(fake_adb, sizeof(fake_adb), "%s\\adb.exe", test_dir) >= (int)sizeof(fake_adb) ||
        snprintf(marker, sizeof(marker), "%s\\reconnect.marker", test_dir) >= (int)sizeof(marker)) return false;
    if (GetFileAttributesA(fake_adb) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA(marker) != INVALID_FILE_ATTRIBUTES) return false;
    if (!CopyFileA(image, fake_adb, TRUE)) return false;
    made_fake = true;
    return true;
}

static void cleanup(void)
{
    if (helper_parked) {
        char parked[MAX_PATH];
        if (snprintf(parked, sizeof(parked), "%s\\adb.test-disabled", test_dir) < (int)sizeof(parked))
            MoveFileExA(parked, fake_adb, MOVEFILE_WRITE_THROUGH);
    }
    if (made_marker) DeleteFileA(marker);
    if (made_fake) DeleteFileA(fake_adb);
}

int main(int argc, char **argv)
{
    int child_exit = child_mode(argc, argv);
    if (child_exit >= 0) return child_exit;
    if (!paths_init()) {
        fprintf(stderr, "Refusing to run outside isolated UI-startup test folder.\n");
        return 2;
    }
    atexit(cleanup);
    Config c;
    config_default(&c);
    c.porta_adb = 16555;
    CHECK(adb_pronto_passo(NULL) == ADB_PRONTO_NO);
    CHECK(adb_pronto_chiudi(NULL));

    CHECK(set_mode("ready-space"));
    AdbPreparazione *r = start(&c);
    CHECK(r != NULL);
    if (r) {
        CHECK(wait_result(r, ADB_PRONTO_SI, 3000));
        CHECK(adb_pronto_passo(r) == ADB_PRONTO_SI);
        CHECK(adb_pronto_chiudi(r));
    }

    const char *no_modes[] = {"notready", "misleading", "noisy"};
    for (size_t i = 0; i < sizeof(no_modes)/sizeof(no_modes[0]); ++i) {
        CHECK(set_mode(no_modes[i])); r = start(&c); CHECK(r != NULL);
        if (r) { CHECK(wait_result(r, ADB_PRONTO_NO, 3000)); CHECK(adb_pronto_chiudi(r)); }
    }

    /* The request must retain the port from start time, not reread Config. */
    CHECK(set_mode("ready")); r = start(&c); CHECK(r != NULL); c.porta_adb = 16556;
    if (r) { CHECK(wait_result(r, ADB_PRONTO_SI, 3000)); CHECK(adb_pronto_chiudi(r)); }
    c.porta_adb = 16555;

    CHECK(set_mode("reconnect")); r = start(&c); CHECK(r != NULL);
    if (r) { CHECK(wait_result(r, ADB_PRONTO_SI, 4000)); CHECK(adb_pronto_chiudi(r)); }
    made_marker = GetFileAttributesA(marker) != INVALID_FILE_ATTRIBUTES;
    CHECK(made_marker);
    if (made_marker) { CHECK(DeleteFileA(marker) != 0); made_marker = false; }

    CHECK(set_mode("queryfail")); r = start(&c); CHECK(r != NULL);
    if (r) { CHECK(wait_result(r, ADB_PRONTO_NO, 4000)); CHECK(adb_pronto_chiudi(r)); }

    /* Missing helper must fail closed and leave the owned helper recoverable. */
    char parked_adb[MAX_PATH];
    CHECK(snprintf(parked_adb, sizeof(parked_adb), "%s\\adb.test-disabled", test_dir) < (int)sizeof(parked_adb));
    CHECK(GetFileAttributesA(parked_adb) == INVALID_FILE_ATTRIBUTES);
    CHECK(MoveFileExA(fake_adb, parked_adb, MOVEFILE_WRITE_THROUGH) != 0);
    helper_parked = true;
    r = adb_pronto_avvia(&c);
    CHECK(r == NULL);
    CHECK(MoveFileExA(parked_adb, fake_adb, MOVEFILE_WRITE_THROUGH) != 0);
    helper_parked = false;

    CHECK(set_mode("ready")); r = start(&c); CHECK(r != NULL);
    if (r) {
        r->iniziata -= 20001;
        CHECK(adb_pronto_passo(r) == ADB_PRONTO_NO);
        CHECK(adb_pronto_chiudi(r));
    }

    CHECK(set_mode("hang"));
    DWORD handles_before = 0, handles_after = 0;
    HANDLE self = GetCurrentProcess();
    CHECK(GetProcessHandleCount(self, &handles_before) != 0);
    for (int i = 0; i < 12; ++i) {
        r = start(&c); CHECK(r != NULL);
        if (r) {
            CHECK(adb_pronto_passo(r) == ADB_PRONTO_ATTESA);
            CHECK(adb_pronto_chiudi(r));
        }
    }
    CHECK(GetProcessHandleCount(self, &handles_after) != 0);
    CHECK(handles_after == handles_before);
    SetEnvironmentVariableA("HABUMI_ADB_READY_TEST_MODE", NULL);
    printf("test-adb-ready: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
