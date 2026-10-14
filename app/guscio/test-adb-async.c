/* SPDX-License-Identifier: GPL-2.0-only */
/* Isolated lifecycle test for adb_spegni_avvia/passo/chiudi.
 * The test executable is copied beside itself as adb.exe; it never invokes the
 * product's adb.exe or connects to an ADB server. */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guscio.h"

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static bool suffix_case_insensitive(const char *text, const char *suffix)
{
    size_t text_len = strlen(text), suffix_len = strlen(suffix);
    return text_len >= suffix_len &&
           _stricmp(text + text_len - suffix_len, suffix) == 0;
}

static bool path_join(char *out, size_t cap, const char *dir, const char *leaf)
{
    int n = snprintf(out, cap, "%s\\%s", dir, leaf);
    return n >= 0 && (size_t)n < cap;
}

static int adb_child(int argc, char **argv)
{
    const char *mode = getenv("HABUMI_ADB_SHUTDOWN_TEST_MODE");
    char image[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, image, sizeof(image));

    if (!n || n >= sizeof(image) ||
        !suffix_case_insensitive(image,
            "\\research\\results\\ui-shutdown-20261013\\adb.exe")) {
        return 98; /* Never behave like a real adb outside the isolated folder. */
    }
    if (argc != 6 || strcmp(argv[1], "-s") ||
        strcmp(argv[2], "127.0.0.1:16555") || strcmp(argv[3], "shell") ||
        strcmp(argv[4], "reboot") || strcmp(argv[5], "-p")) {
        fprintf(stderr, "fake adb: unexpected shutdown arguments\n");
        return 90;
    }
    if (mode && !strcmp(mode, "pass")) {
        puts("fake adb: accepted shutdown request");
        return 0;
    }
    if (mode && !strcmp(mode, "fail")) {
        fprintf(stderr, "fake adb: requested failure\n");
        return 7;
    }
    if (mode && !strcmp(mode, "hang")) {
        Sleep(30000);
        return 0;
    }
    fprintf(stderr, "fake adb: invalid test mode\n");
    return 91;
}

static AdbSpegniStato aspetta(AdbSpegnimento *op, DWORD timeout_ms)
{
    ULONGLONG end = GetTickCount64() + timeout_ms;
    AdbSpegniStato state;

    do {
        state = adb_spegni_passo(op);
        if (state != ADB_SPEGNI_ATTESA) {
            return state;
        }
        Sleep(10);
    } while (GetTickCount64() < end);
    return adb_spegni_passo(op);
}

static bool processo_fake_vivo(const char *expected_image)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 entry = { .dwSize = sizeof(entry) };
    bool found = false;

    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (Process32First(snapshot, &entry)) {
        do {
            HANDLE process;
            char image[MAX_PATH];
            DWORD size = sizeof(image);

            if (_stricmp(entry.szExeFile, "adb.exe")) {
                continue;
            }
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  entry.th32ProcessID);
            if (process) {
                if (QueryFullProcessImageNameA(process, 0, image, &size) &&
                    !_stricmp(image, expected_image)) {
                    found = true;
                }
                CloseHandle(process);
            }
        } while (!found && Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

static void prova_esito(const Config *config, const char *mode,
                        AdbSpegniStato expected)
{
    ULONGLONG start = GetTickCount64();
    AdbSpegnimento *op;
    AdbSpegniStato actual;

    CHECK(SetEnvironmentVariableA("HABUMI_ADB_SHUTDOWN_TEST_MODE", mode));
    op = adb_spegni_avvia(config);
    CHECK(op != NULL);
    CHECK(GetTickCount64() - start < 1000);
    if (!op) {
        return;
    }
    actual = aspetta(op, 3000);
    CHECK(actual == expected);
    adb_spegni_chiudi(op);
}

int main(int argc, char **argv)
{
    char self[MAX_PATH], dir[MAX_PATH], fake[MAX_PATH], held[MAX_PATH];
    char *slash;
    DWORD n;
    Config config;
    bool fake_created = false, fake_held = false;
    DWORD handles_before = 0, handles_after = 0;
    int i;

    n = GetModuleFileNameA(NULL, self, sizeof(self));
    if (!n || n >= sizeof(self)) {
        fprintf(stderr, "FAIL: cannot resolve test executable path\n");
        return 2;
    }
    if (suffix_case_insensitive(self,
            "\\research\\results\\ui-shutdown-20261013\\adb.exe")) {
        return adb_child(argc, argv);
    }
    if (!suffix_case_insensitive(self,
            "\\research\\results\\ui-shutdown-20261013\\test-adb-async.exe")) {
        fprintf(stderr, "FAIL: test must run from the isolated ui-shutdown-20261013 folder\n");
        return 2;
    }
    strcpy(dir, self);
    slash = strrchr(dir, '\\');
    if (!slash) {
        return 2;
    }
    *slash = '\0';
    if (!path_join(fake, sizeof(fake), dir, "adb.exe") ||
        !path_join(held, sizeof(held), dir, "adb.exe.test-held")) {
        return 2;
    }

    /* Never adopt or delete a pre-existing adb.exe, even in this test folder. */
    if (GetFileAttributesA(fake) != INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "FAIL: adb.exe already exists; refusing to overwrite it\n");
        return 2;
    }
    if (!CopyFileA(self, fake, TRUE)) {
        fprintf(stderr, "FAIL: isolated fake adb.exe could not be created (%lu)\n",
                GetLastError());
        return 2;
    }
    fake_created = true;

    config_default(&config);
    /* This helper test uses the separately isolated loopback endpoint. */
    config.porta_adb = 16555;
    CHECK(adb_spegni_passo(NULL) == ADB_SPEGNI_ERRORE);
    CHECK(adb_spegni_chiudi(NULL));

    prova_esito(&config, "pass", ADB_SPEGNI_OK);
    prova_esito(&config, "fail", ADB_SPEGNI_ERRORE);

    CHECK(SetEnvironmentVariableA("HABUMI_ADB_SHUTDOWN_TEST_MODE", "hang"));
    {
        ULONGLONG start = GetTickCount64();
        AdbSpegnimento *op = adb_spegni_avvia(&config);
        CHECK(op != NULL);
        CHECK(GetTickCount64() - start < 1000);
        if (op) {
            Sleep(80);
            CHECK(adb_spegni_passo(op) == ADB_SPEGNI_ATTESA);
            CHECK(processo_fake_vivo(fake));
            CHECK(adb_spegni_chiudi(op));
        }
    }
    for (i = 0; i < 100 && processo_fake_vivo(fake); i++) {
        Sleep(20);
    }
    CHECK(!processo_fake_vivo(fake));

    /* Missing helper must fail without starting or touching a different adb. */
    CHECK(MoveFileExA(fake, held, MOVEFILE_WRITE_THROUGH));
    fake_held = GetFileAttributesA(held) != INVALID_FILE_ATTRIBUTES;
    if (fake_held) {
        CHECK(adb_spegni_avvia(&config) == NULL);
        CHECK(MoveFileExA(held, fake, MOVEFILE_WRITE_THROUGH));
        fake_held = GetFileAttributesA(held) != INVALID_FILE_ATTRIBUTES;
    }

    /* Repeated completion/cancellation exercises pipe, process and event cleanup. */
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    for (i = 0; i < 3; i++) {
        prova_esito(&config, "pass", ADB_SPEGNI_OK);
        prova_esito(&config, "fail", ADB_SPEGNI_ERRORE);
        CHECK(SetEnvironmentVariableA("HABUMI_ADB_SHUTDOWN_TEST_MODE", "hang"));
        {
            AdbSpegnimento *op = adb_spegni_avvia(&config);
            CHECK(op != NULL);
            if (op) {
                CHECK(adb_spegni_chiudi(op));
            }
        }
    }
    for (i = 0; i < 100 && processo_fake_vivo(fake); i++) {
        Sleep(20);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    CHECK(handles_after == handles_before);
    CHECK(SetEnvironmentVariableA("HABUMI_ADB_SHUTDOWN_TEST_MODE", NULL));

    if (fake_held) {
        fprintf(stderr, "FAIL: fake adb helper was not restored from its test-only backup\n");
        failures++;
    }
    if (fake_created && !fake_held) {
        CHECK(DeleteFileA(fake));
    }
    printf("%s: %u checks, %u failures\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
