/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise the real VM shutdown state machine with two isolated fake clients. */
#include "vm.c"

static int failures;
#define CHECK(value) do { if (!(value)) { failures++; \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (0)

int main(int argc, char **argv)
{
    char self[MAX_PATH], fake[MAX_PATH], helper[MAX_PATH], command[MAX_PATH + 32];
    const char *suffix = "\\research\\results\\ui-shutdown-20261013\\test-vm-async.exe";
    STARTUPINFOA si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    Config config;
    ULONGLONG began, launch_ms, poll_ms;
    AdbSpegnimento *first;
    if (argc == 2 && !strcmp(argv[1], "--fake-qemu")) {
        Sleep(30000);
        return 0;
    }
    if (!GetModuleFileNameA(NULL, self, sizeof(self)) ||
        strlen(self) < strlen(suffix) ||
        _stricmp(self + strlen(self) - strlen(suffix), suffix)) {
        fputs("FAIL: run only from the isolated UI shutdown test folder\n", stderr);
        return 2;
    }
    guscio_percorso_accanto("adb.exe", fake, sizeof(fake));
    guscio_percorso_accanto("test-adb-async.exe", helper, sizeof(helper));
    if (GetFileAttributesA(fake) != INVALID_FILE_ATTRIBUTES ||
        !CopyFileA(helper, fake, TRUE)) {
        fputs("FAIL: refuse existing adb.exe or missing fake helper\n", stderr);
        return 2;
    }
    snprintf(command, sizeof(command), "\"%s\" --fake-qemu", self);
    if (!CreateProcessA(self, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        DeleteFileA(fake);
        return 2;
    }
    CloseHandle(pi.hThread);
    registro_apri();
    config_default(&config);
    config.porta_adb = 16555;
    vm_c = &config;
    vm_processo = pi.hProcess;
    vm_s = VM_PRONTO;
    SetEnvironmentVariableA("HABUMI_ADB_SHUTDOWN_TEST_MODE", "hang");
    began = GetTickCount64();
    vm_avvia_spegnimento();
    launch_ms = GetTickCount64() - began;
    CHECK(launch_ms < 1000);
    CHECK(vm_s == VM_SPEGNIMENTO && vm_adb_spegni != NULL);
    first = vm_adb_spegni;
    vm_avvia_spegnimento();
    CHECK(vm_adb_spegni == first);
    CHECK(vm_azione_chiusura(vm_s) == CHIUSURA_ATTENDI);
    began = GetTickCount64();
    CHECK(vm_passo() == VM_SPEGNIMENTO);
    poll_ms = GetTickCount64() - began;
    CHECK(poll_ms < 100);
    CHECK(vm_viva());
    /* Advance only this state machine's test timestamp, never host time. */
    vm_ora_spegni = vm_adesso() - VM_SPEGNIMENTO_MS - 1;
    CHECK(vm_passo() == VM_USCITO);
    CHECK(vm_adb_spegni == NULL && vm_processo == NULL);
    vm_chiudi();
    SetEnvironmentVariableA("HABUMI_ADB_SHUTDOWN_TEST_MODE", NULL);
    for (int i = 0; i < 100; i++) {
        if (DeleteFileA(fake)) { break; }
        Sleep(20);
    }
    CHECK(GetFileAttributesA(fake) == INVALID_FILE_ATTRIBUTES);
    printf("%s: pending ADB preserves UI timer, duplicate close is ignored, deadline cancels owned clients; launch=%llu ms, poll=%llu ms\n",
           failures ? "FAIL" : "PASS", launch_ms, poll_ms);
    return failures ? 1 : 0;
}
