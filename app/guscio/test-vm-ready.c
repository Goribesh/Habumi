/* SPDX-License-Identifier: GPL-2.0-only */
/* Real VM state machine and ADB implementation, isolated fake processes only. */
#include "adb.c"
#include "vm.c"

static int failures;
#define CHECK(value) do { if (!(value)) { failures++; \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (0)
static char self[MAX_PATH];
static Config config;

static void start_vm(const char *mode)
{
    STARTUPINFOA si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    char command[MAX_PATH + 32];
    snprintf(command, sizeof(command), "\"%s\" --fake-qemu", self);
    if (!CreateProcessA(self, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) { exit(2); }
    CloseHandle(pi.hThread);
    config_default(&config);
    config.porta_adb = 16555;
    vm_c = &config;
    vm_processo = pi.hProcess;
    vm_s = VM_ATTESA_ANDROID;
    vm_ora_stato = vm_ora_ultima_riga = vm_adesso();
    vm_ora_ultima_adb = vm_adesso() - 2001;
    vm_righe_totali = 10000; /* Serial progress alone must never enable controls. */
    SetEnvironmentVariableA("HABUMI_ADB_READY_TEST_MODE", mode);
}

static HANDLE check_process_copy(void)
{
    HANDLE copy = NULL;
    CHECK(vm_adb_pronto != NULL);
    if (vm_adb_pronto) {
        CHECK(DuplicateHandle(GetCurrentProcess(), vm_adb_pronto->cliente->processo,
                              GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS));
    }
    return copy;
}

static void finish_check(void)
{
    ULONGLONG end = GetTickCount64() + 3000;
    while (vm_adb_pronto && GetTickCount64() < end) {
        vm_passo();
        Sleep(10);
    }
    CHECK(vm_adb_pronto == NULL);
}

int main(int argc, char **argv)
{
    const char *suffix = "\\research\\results\\ui-startup-20261013\\test-vm-ready.exe";
    char fake[MAX_PATH], helper[MAX_PATH];
    if (argc == 2 && !strcmp(argv[1], "--fake-qemu")) { Sleep(30000); return 0; }
    if (!GetModuleFileNameA(NULL, self, sizeof(self)) || strlen(self) < strlen(suffix) ||
        _stricmp(self + strlen(self) - strlen(suffix), suffix)) { return 2; }
    guscio_percorso_accanto("adb.exe", fake, sizeof(fake));
    guscio_percorso_accanto("test-adb-ready.exe", helper, sizeof(helper));
    if (GetFileAttributesA(fake) != INVALID_FILE_ATTRIBUTES || !CopyFileA(helper, fake, TRUE)) {
        fputs("FAIL: refuse existing adb.exe or missing fake helper\n", stderr);
        return 2;
    }
    registro_apri();
    start_vm("hang");
    ULONGLONG began = GetTickCount64();
    CHECK(vm_passo() == VM_ATTESA_ANDROID);
    ULONGLONG launch_ms = GetTickCount64() - began;
    CHECK(launch_ms < 1000);
    AdbPreparazione *first = vm_adb_pronto;
    began = GetTickCount64();
    CHECK(vm_passo() == VM_ATTESA_ANDROID);
    ULONGLONG poll_ms = GetTickCount64() - began;
    CHECK(poll_ms < 100 && vm_adb_pronto == first);
    HANDLE query = check_process_copy();
    vm_avvia_spegnimento();
    CHECK(vm_s == VM_SPEGNIMENTO && vm_adb_pronto == NULL && vm_adb_spegni != NULL);
    CHECK(WaitForSingleObject(query, 0) == WAIT_OBJECT_0);
    CloseHandle(query);
    AdbSpegnimento *shutdown = vm_adb_spegni;
    vm_avvia_spegnimento();
    CHECK(vm_adb_spegni == shutdown);
    vm_chiudi();

    start_vm("hang");
    vm_passo();
    query = check_process_copy();
    CHECK(TerminateProcess(vm_processo, 0));
    CHECK(WaitForSingleObject(vm_processo, 1000) == WAIT_OBJECT_0);
    CHECK(vm_passo() == VM_FALLITA);
    CHECK(vm_adb_pronto == NULL && vm_processo == NULL);
    CHECK(WaitForSingleObject(query, 0) == WAIT_OBJECT_0);
    CloseHandle(query);
    vm_chiudi();

    start_vm("notready"); vm_passo(); finish_check();
    CHECK(vm_s == VM_ATTESA_ANDROID);
    vm_passo(); CHECK(vm_adb_pronto == NULL); /* Preserve the 2 s retry spacing. */
    vm_chiudi();
    start_vm("misleading"); vm_passo(); finish_check();
    CHECK(vm_s == VM_ATTESA_ANDROID); vm_chiudi();
    start_vm("ready"); vm_passo(); finish_check();
    CHECK(vm_s == VM_PRONTO); vm_chiudi();

    start_vm("hang"); vm_passo();
    CHECK(vm_adb_pronto != NULL);
    if (vm_adb_pronto) { vm_adb_pronto->iniziata -= 20001; }
    CHECK(vm_passo() == VM_ATTESA_ANDROID && vm_adb_pronto == NULL && vm_viva());
    vm_chiudi();
    SetEnvironmentVariableA("HABUMI_ADB_READY_TEST_MODE", NULL);
    for (int i = 0; i < 100; i++) { if (DeleteFileA(fake)) { break; } Sleep(20); }
    CHECK(GetFileAttributesA(fake) == INVALID_FILE_ATTRIBUTES);
    printf("%s: pending boot check keeps UI timer available; one query, cancel on close/QEMU exit, exact readiness, retry spacing, and deadline passed; launch=%llu ms, poll=%llu ms\n",
           failures ? "FAIL" : "PASS", launch_ms, poll_ms);
    return failures ? 1 : 0;
}
