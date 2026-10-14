/* SPDX-License-Identifier: GPL-2.0-only */
/* Isolated tests for the asynchronous UI command FIFO. */
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

/* Include these implementations to inspect their private deadlines and queue. */
static bool fail_cancel_wait_once;
static DWORD test_client_wait(HANDLE process, DWORD timeout)
{
    if (fail_cancel_wait_once && timeout == 1000) {
        fail_cancel_wait_once = false;
        return WAIT_TIMEOUT;
    }
    return WaitForSingleObject(process, timeout);
}
#define WaitForSingleObject test_client_wait
#include "adb.c"
#undef WaitForSingleObject
#include "comandi.c"

static char test_dir[MAX_PATH], fake_adb[MAX_PATH];
static char actions_path[MAX_PATH], rotation_path[MAX_PATH];
static bool own_fake, own_actions, own_rotation;
static unsigned checks, failures, pipe_calls;
static int pipe_w[8], pipe_h[8];
static bool pipe_fail;

#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

bool tubo_risoluzione(int w, int h)
{
    if (pipe_calls < sizeof(pipe_w)/sizeof(pipe_w[0])) {
        pipe_w[pipe_calls] = w; pipe_h[pipe_calls] = h;
    }
    pipe_calls++;
    return !pipe_fail;
}

static bool suffix_ci(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && _stricmp(s + n - m, suffix) == 0;
}

static bool set_mode(const char *mode)
{
    return SetEnvironmentVariableA("HABUMI_UI_TEST_MODE", mode) != 0;
}

static int fake_child(int argc, char **argv)
{
    char image[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, image, MAX_PATH);
    const char *mode = getenv("HABUMI_UI_TEST_MODE");
    if (!n || n >= MAX_PATH || !suffix_ci(image,
        "\\research\\results\\ui-controls-20261013\\adb.exe")) return -1;
    char *slash = strrchr(image, '\\');
    if (!slash) return 98;
    *slash = '\0';
    if (snprintf(actions_path, sizeof(actions_path), "%s\\actions.log", image) >= (int)sizeof(actions_path) ||
        snprintf(rotation_path, sizeof(rotation_path), "%s\\rotation.state", image) >= (int)sizeof(rotation_path)) return 98;
    if (argc < 5 || strcmp(argv[1], "-s") || strcmp(argv[2], "127.0.0.1:16555") ||
        strcmp(argv[3], "shell")) {
        fprintf(stderr, "fake adb: invalid target or argv\n"); return 90;
    }
    FILE *log = fopen(actions_path, "ab");
    if (!log) return 91;
    fprintf(log, "%s", argv[2]);
    for (int i = 4; i < argc; ++i) fprintf(log, " %s", argv[i]);
    fputc('\n', log); fclose(log);

    if (mode && !strcmp(mode, "hang")) { Sleep(30000); return 0; }
    if (mode && !strcmp(mode, "fail")) return 7;
    if (argc == 6 && !strcmp(argv[4], "wm") && !strcmp(argv[5], "size")) {
        if (mode && !strcmp(mode, "bad-size")) { puts("size unavailable"); return 0; }
        puts("Physical size: 1280x800"); return 0;
    }
    if (argc == 8 && !strcmp(argv[4], "settings") && !strcmp(argv[5], "get") &&
        !strcmp(argv[6], "system") && !strcmp(argv[7], "user_rotation")) {
        if (mode && !strcmp(mode, "bad-rotation")) { puts("bad rotation"); return 0; }
        FILE *state = fopen(rotation_path, "rb");
        if (!state) { puts("null"); return 0; }
        char value[16] = {0}; (void)fread(value, 1, sizeof(value)-1, state); fclose(state);
        puts(value); return 0;
    }
    if (argc == 9 && !strcmp(argv[4], "settings") && !strcmp(argv[5], "put") &&
        !strcmp(argv[6], "system")) {
        if (mode && !strcmp(mode, "fail-write")) return 7;
        if (!strcmp(argv[7], "accelerometer_rotation")) {
            if (strcmp(argv[8], "0")) return 92;
            puts("0"); return 0;
        }
        if (!strcmp(argv[7], "user_rotation") && argc == 9 &&
            (!strcmp(argv[8], "0") || !strcmp(argv[8], "1"))) {
            FILE *state = fopen(rotation_path, "wb");
            if (!state) return 93;
            fputs(argv[8], state); fputc('\n', state); fclose(state);
            puts(argv[8]); return 0;
        }
    }
    if (argc == 7 && !strcmp(argv[4], "wm") && !strcmp(argv[5], "density")) {
        int d = atoi(argv[6]);
        if (d < 120 || d > 640) return 95;
        puts("Physical density set"); return 0;
    }
    if (argc == 7 && !strcmp(argv[4], "input") && !strcmp(argv[5], "keyevent")) {
        const char *keys[] = {"KEYCODE_POWER", "KEYCODE_VOLUME_UP", "KEYCODE_VOLUME_DOWN",
                              "KEYCODE_HOME", "KEYCODE_BACK", "KEYCODE_APP_SWITCH"};
        for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)
            if (!strcmp(argv[6], keys[i])) { puts("OK"); return 0; }
    }
    fprintf(stderr, "fake adb: unknown shell command\n");
    return 96;
}

static bool setup_paths(void)
{
    char image[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, image, MAX_PATH);
    if (!n || n >= MAX_PATH || !suffix_ci(image,
        "\\research\\results\\ui-controls-20261013\\test-comandi.exe")) return false;
    strcpy_s(test_dir, sizeof(test_dir), image);
    char *slash = strrchr(test_dir, '\\');
    if (!slash) return false;
    *slash = '\0';
    if (snprintf(fake_adb, sizeof(fake_adb), "%s\\adb.exe", test_dir) >= (int)sizeof(fake_adb) ||
        snprintf(actions_path, sizeof(actions_path), "%s\\actions.log", test_dir) >= (int)sizeof(actions_path) ||
        snprintf(rotation_path, sizeof(rotation_path), "%s\\rotation.state", test_dir) >= (int)sizeof(rotation_path)) return false;
    if (GetFileAttributesA(fake_adb) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA(actions_path) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA(rotation_path) != INVALID_FILE_ATTRIBUTES) return false;
    if (!CopyFileA(image, fake_adb, TRUE)) return false;
    own_fake = true;
    return true;
}

static void cleanup(void)
{
    if (own_rotation) DeleteFileA(rotation_path);
    if (own_actions) DeleteFileA(actions_path);
    if (own_fake) DeleteFileA(fake_adb);
}

static int read_actions(char lines[][256], int cap)
{
    FILE *f = fopen(actions_path, "rb");
    if (!f) return 0;
    int count = 0;
    while (count < cap && fgets(lines[count], 256, f)) {
        size_t n = strlen(lines[count]);
        while (n && (lines[count][n-1] == '\r' || lines[count][n-1] == '\n')) lines[count][--n] = 0;
        count++;
    }
    fclose(f); return count;
}

static void clear_actions(void)
{
    if (GetFileAttributesA(actions_path) != INVALID_FILE_ATTRIBUTES) DeleteFileA(actions_path);
    own_actions = false;
}

static bool wait_for_action_count(int wanted, DWORD timeout_ms)
{
    ULONGLONG end = GetTickCount64() + timeout_ms;
    char lines[8][256];
    do {
        if (read_actions(lines, 8) >= wanted) return true;
        Sleep(5);
    } while (GetTickCount64() < end);
    return false;
}

static bool drain(DWORD limit_ms)
{
    ULONGLONG end = GetTickCount64() + limit_ms;
    do {
        comandi_passo();
        if (!comandi_in_corso()) return true;
        Sleep(10);
    } while (GetTickCount64() < end);
    return false;
}

int main(int argc, char **argv)
{
    int child = fake_child(argc, argv);
    if (child >= 0) return child;
    if (!setup_paths()) { fprintf(stderr, "Refusing to run outside isolated UI-controls folder.\n"); return 2; }
    atexit(cleanup);
    Config c; config_default(&c); c.porta_adb = 16555;

    /* FIFO: enqueue everything before dispatch, then mutate the config port. */
    CHECK(set_mode("normal"));
    ComandoUi keys[] = {COMANDO_POWER, COMANDO_VOL_SU, COMANDO_VOL_GIU,
                        COMANDO_HOME, COMANDO_BACK, COMANDO_RECENTI};
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i) CHECK(comandi_accoda(&c, keys[i], 0));
    CHECK(comandi_accoda(&c, COMANDO_DENSITA, 426)); c.porta_adb = 16556;
    own_actions = true;
    CHECK(drain(5000));
    char lines[64][256]; int count = read_actions(lines, 64);
    const char *ordered[] = {"input keyevent KEYCODE_POWER", "input keyevent KEYCODE_VOLUME_UP",
        "input keyevent KEYCODE_VOLUME_DOWN", "input keyevent KEYCODE_HOME",
        "input keyevent KEYCODE_BACK", "input keyevent KEYCODE_APP_SWITCH", "wm density 426"};
    CHECK(count == 7);
    for (int i = 0; i < 7 && i < count; ++i) {
        char expected[256]; snprintf(expected, sizeof(expected), "127.0.0.1:16555 %s", ordered[i]);
        CHECK(strcmp(lines[i], expected) == 0);
    }
    c.porta_adb = 16555;
    clear_actions();

    /* Queue cap is 16; the rejected 17th item must not start or be retained. */
    for (int i = 0; i < 16; ++i) CHECK(comandi_accoda(&c, COMANDO_HOME, 0));
    CHECK(!comandi_accoda(&c, COMANDO_BACK, 0));
    CHECK(comandi_annulla()); CHECK(!comandi_in_corso());

    /* A command failure discards queued work instead of delivering it later. */
    clear_actions(); own_actions = true; CHECK(set_mode("fail"));
    CHECK(comandi_accoda(&c, COMANDO_HOME, 0)); CHECK(comandi_accoda(&c, COMANDO_BACK, 0));
    comandi_passo(); CHECK(drain(3000));
    count = read_actions(lines, 64); CHECK(count == 1);
    CHECK(!comandi_in_corso());

    /* Total deadline and explicit cancel both terminate only the owned fake. */
    clear_actions(); own_actions = true; CHECK(set_mode("hang"));
    CHECK(comandi_accoda(&c, COMANDO_HOME, 0)); CHECK(comandi_accoda(&c, COMANDO_BACK, 0));
    comandi_passo(); CHECK(comandi_in_corso()); CHECK(wait_for_action_count(1, 1000));
    LARGE_INTEGER qpc0, qpc1, qpcfreq; QueryPerformanceFrequency(&qpcfreq);
    QueryPerformanceCounter(&qpc0); comandi_passo(); QueryPerformanceCounter(&qpc1);
    CHECK((qpc1.QuadPart - qpc0.QuadPart) * 1000 / qpcfreq.QuadPart < 100);
    iniziata -= 20001; comandi_passo(); CHECK(!comandi_in_corso());
    CHECK(set_mode("normal")); comandi_passo();
    count = read_actions(lines, 64); CHECK(count == 1);
    clear_actions(); own_actions = true; CHECK(set_mode("hang"));
    CHECK(comandi_accoda(&c, COMANDO_HOME, 0)); CHECK(comandi_accoda(&c, COMANDO_BACK, 0));
    comandi_passo(); CHECK(wait_for_action_count(1, 1000));
    CHECK(comandi_annulla()); CHECK(!comandi_in_corso());
    count = read_actions(lines, 64); CHECK(count == 1);

    /* If the fast UI timer cannot run, the ordinary VM timer retries retained
     * cancellation. Inject one wait timeout after cancelling our fake client. */
    clear_actions(); own_actions = true; CHECK(set_mode("hang"));
    CHECK(comandi_accoda(&c, COMANDO_HOME, 0)); comandi_passo();
    CHECK(wait_for_action_count(1, 1000));
    fail_cancel_wait_once = true;
    CHECK(!comandi_annulla()); CHECK(comandi_in_corso());
    comandi_pulizia(); CHECK(!comandi_in_corso());

    /* Rotation preserves read-size -> read-state -> pipe -> Android writes. */
    clear_actions(); own_actions = true; own_rotation = true; CHECK(set_mode("normal"));
    pipe_calls = 0; pipe_fail = true;
    CHECK(comandi_accoda(&c, COMANDO_RUOTA, 0)); comandi_passo(); CHECK(drain(5000));
    count = read_actions(lines, 64);
    const char *rot1[] = {"wm size", "settings get system user_rotation",
        "settings put system accelerometer_rotation 0", "settings put system user_rotation 1"};
    CHECK(count == 4 && pipe_calls == 1 && pipe_w[0] == 800 && pipe_h[0] == 1280);
    for (int i = 0; i < 4 && i < count; ++i) { char e[256]; snprintf(e, sizeof(e), "127.0.0.1:16555 %s", rot1[i]); CHECK(strcmp(lines[i], e) == 0); }
    clear_actions(); pipe_calls = 0;
    CHECK(comandi_accoda(&c, COMANDO_RUOTA, 0)); comandi_passo(); CHECK(drain(5000));
    count = read_actions(lines, 64);
    const char *rot2[] = {"wm size", "settings get system user_rotation",
        "settings put system accelerometer_rotation 0", "settings put system user_rotation 0"};
    CHECK(count == 4 && pipe_calls == 1 && pipe_w[0] == 1280 && pipe_h[0] == 800);
    for (int i = 0; i < 4 && i < count; ++i) { char e[256]; snprintf(e, sizeof(e), "127.0.0.1:16555 %s", rot2[i]); CHECK(strcmp(lines[i], e) == 0); }

    /* Invalid size/state and failed writes clear queued commands. */
    const char *bad_modes[] = {"bad-size", "bad-rotation", "fail-write"};
    const int expected_prefix[] = {1, 2, 3};
    for (size_t i = 0; i < 3; ++i) {
        clear_actions(); CHECK(set_mode(bad_modes[i])); pipe_calls = 0;
        CHECK(comandi_accoda(&c, COMANDO_RUOTA, 0)); CHECK(comandi_accoda(&c, COMANDO_HOME, 0));
        comandi_passo(); CHECK(drain(5000));
        count = read_actions(lines, 64); CHECK(count == expected_prefix[i]);
        CHECK(!comandi_in_corso());
    }
    CHECK(pipe_calls == 1); /* framebuffer still updates before failed Android write */
    clear_actions(); own_actions = false;
    if (own_rotation && GetFileAttributesA(rotation_path) != INVALID_FILE_ATTRIBUTES) {
        CHECK(DeleteFileA(rotation_path) != 0); own_rotation = false;
    }

    /* Repeated cancellation must return owned process handles to baseline. */
    CHECK(set_mode("hang"));
    DWORD before = 0, after = 0; HANDLE self = GetCurrentProcess();
    CHECK(GetProcessHandleCount(self, &before) != 0);
    for (int i = 0; i < 10; ++i) {
        CHECK(comandi_accoda(&c, COMANDO_HOME, 0)); comandi_passo();
        CHECK(comandi_annulla()); CHECK(!comandi_in_corso());
    }
    CHECK(GetProcessHandleCount(self, &after) != 0); CHECK(before == after);
    SetEnvironmentVariableA("HABUMI_UI_TEST_MODE", NULL);
    printf("test-comandi: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
