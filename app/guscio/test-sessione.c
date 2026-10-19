/* SPDX-License-Identifier: GPL-2.0-only */
/* Prove del protocollo A1 e del worker senza avviare QEMU. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "guscio.h"
#include "habumi-session.h"

static int prove;
#define CHECK(x) do { prove++; if (!(x)) { \
    fprintf(stderr, "fallita riga %d: %s\n", __LINE__, #x); return 1; \
} } while (0)

static int prova_wire(void)
{
    HabumiSessionFrame r = {0}, q = {0};
    uint32_t token[4] = {1, 2, 3, 4};
    r.magic = HS_MAGIC; r.version = HS_VERSION; r.op = HS_VIEWPORT;
    r.seq = 1; memcpy(r.token, token, sizeof(token));
    r.deadline = 1200; r.width = 640; r.height = 480;
    CHECK(hs_request_status(&r, token, 1000) == HS_OK);
    r.version++; CHECK(hs_request_status(&r, token, 1000) == HS_BAD_VERSION);
    r.version--; r.token[0]++; CHECK(hs_request_status(&r, token, 1000) == HS_BAD_SESSION);
    r.token[0]--; r.deadline = 1000; CHECK(hs_request_status(&r, token, 1000) == HS_EXPIRED);
    r.deadline = 7001; CHECK(hs_request_status(&r, token, 1000) == HS_EXPIRED);
    r.deadline = 1200; r.op = 99; CHECK(hs_request_status(&r, token, 1000) == HS_BAD_COMMAND);
    r.op = HS_VIEWPORT; r.width = 16385; CHECK(hs_request_status(&r, token, 1000) == HS_BAD_COMMAND);
    r.width = 640; r.height = 0; CHECK(hs_request_status(&r, token, 1000) == HS_BAD_COMMAND);
    q = r; q.op = HS_HELLO; q.seq = 9; q.reply = 1; q.pid = 42; q.caps = HS_CAPS;
    CHECK(hs_reply_matches(&q, &q, 42));
    q.seq++; CHECK(!hs_reply_matches(&q, &r, 42));
    return 0;
}

enum { RESP_MISMATCH, RESP_REJECT, RESP_BAD_CAPS, RESP_STALL, RESP_RESTART };
static int response_mode;
static HANDLE server_ready, request_received, viewport_received, viewport_replied;
static HANDLE server_done, server_release, client_disconnected;
static HWND parent_window, child_window;
static volatile LONG hello_count, viewport_count, response_count, server_errors;

static DWORD WINAPI fake_server(void *unused)
{
    char name[128], token[64];
    HabumiSessionFrame request, reply;
    DWORD n;
    HANDLE pipe;
    (void)unused;
    if (!GetEnvironmentVariableA("HABUMI_SESSION", token, sizeof(token))) {
        InterlockedIncrement(&server_errors); goto done;
    }
    _snprintf(name, sizeof(name), "\\\\.\\pipe\\Habumi.A1.%s", token);
    pipe = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE |
            PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, sizeof(reply), sizeof(request),
            0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) { InterlockedIncrement(&server_errors); goto done; }
    SetEvent(server_ready);
    if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
        InterlockedIncrement(&server_errors); goto close_pipe;
    }
    if (!ReadFile(pipe, &request, sizeof(request), &n, NULL) || n != sizeof(request)) {
        InterlockedIncrement(&server_errors); goto close_pipe;
    }
    if (request.op == HS_HELLO) InterlockedIncrement(&hello_count);
    SetEvent(request_received);
    if (response_mode == RESP_STALL) {
        WaitForSingleObject(server_release, 5000);
        goto close_pipe;
    }
    reply = request; reply.reply = 1; reply.pid = GetCurrentProcessId();
    reply.status = HS_OK; reply.caps = HS_CAPS;
    reply.hwnd = (uint64_t)(uintptr_t)child_window;
    if (!WriteFile(pipe, &reply, sizeof(reply), &n, NULL) || n != sizeof(reply)) {
        InterlockedIncrement(&server_errors); goto close_pipe;
    }
    InterlockedIncrement(&response_count);
    if (!ReadFile(pipe, &request, sizeof(request), &n, NULL) || n != sizeof(request)) {
        InterlockedIncrement(&server_errors); goto close_pipe;
    }
    if (request.op == HS_VIEWPORT) InterlockedIncrement(&viewport_count);
    SetEvent(viewport_received);
    reply = request; reply.reply = 1; reply.pid = GetCurrentProcessId();
    reply.status = response_mode == RESP_REJECT ? HS_BAD_COMMAND : HS_OK;
    reply.caps = response_mode == RESP_BAD_CAPS ? 0 : HS_CAPS;
    if (response_mode == RESP_MISMATCH) reply.seq++;
    if (!WriteFile(pipe, &reply, sizeof(reply), &n, NULL) || n != sizeof(reply)) {
        InterlockedIncrement(&server_errors); goto close_pipe;
    }
    InterlockedIncrement(&response_count);
    SetEvent(viewport_replied);
    /* Il server resta aperto: deve essere il client a rifiutare e scollegarsi.
     * Chiudere qui falserebbe la prova anche se la risposta fosse accettata. */
    bool disconnected = false;
    for (int i = 0; i < 600; i++) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL)) {
            disconnected = GetLastError() == ERROR_BROKEN_PIPE;
            break;
        }
        if (available) { break; }
        Sleep(5);
    }
    if (!disconnected) { InterlockedIncrement(&server_errors); goto close_pipe; }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    pipe = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE |
            PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, sizeof(reply), sizeof(request),
            0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) { InterlockedIncrement(&server_errors); goto done; }
    if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
        InterlockedIncrement(&server_errors); goto close_pipe;
    }
    if (!ReadFile(pipe, &request, sizeof(request), &n, NULL) || n != sizeof(request) ||
        request.op != HS_HELLO) {
        InterlockedIncrement(&server_errors); goto close_pipe;
    }
    InterlockedIncrement(&hello_count);
    SetEvent(client_disconnected);
    CloseHandle(pipe);
    SetEvent(server_done);
    return 0;
close_pipe:
    DisconnectNamedPipe(pipe); CloseHandle(pipe);
done:
    SetEvent(server_done);
    return 0;
}

static int prova_worker(int mode)
{
    HANDLE thread = NULL;
    int ok = 1;
    ULONGLONG start;
    /* Il worker azzera il proprio stato in sessione_avvia: pubblica prima i dati
     * della sessione e crea il server solo dopo che il nome della pipe esiste. */
    if (!sessione_avvia(parent_window)) return 1;
    response_mode = mode;
    ResetEvent(server_ready); ResetEvent(request_received); ResetEvent(viewport_received);
    ResetEvent(viewport_replied);
    ResetEvent(server_done); ResetEvent(server_release); ResetEvent(client_disconnected);
    InterlockedExchange(&hello_count, 0); InterlockedExchange(&viewport_count, 0);
    InterlockedExchange(&response_count, 0); InterlockedExchange(&server_errors, 0);
    thread = CreateThread(NULL, 0, fake_server, NULL, 0, NULL);
    if (!thread || WaitForSingleObject(server_ready, 3000) != WAIT_OBJECT_0) { ok = 0; goto done; }
    sessione_area(640, 480);
    sessione_qemu(GetCurrentProcessId());
    if (WaitForSingleObject(request_received, 3000) != WAIT_OBJECT_0 || hello_count != 1) { ok = 0; goto done; }
    if (mode == RESP_STALL) {
        start = GetTickCount64();
        if (!sessione_ferma() || GetTickCount64() - start >= 1000) ok = 0;
        SetEvent(server_release);
    } else {
        if (WaitForSingleObject(viewport_received, 3000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(viewport_replied, 3000) != WAIT_OBJECT_0 ||
            viewport_count != 1 || response_count != 2) ok = 0;
        if (mode == RESP_RESTART) sessione_qemu(GetCurrentProcessId());
        if (WaitForSingleObject(client_disconnected, 3000) != WAIT_OBJECT_0) ok = 0;
        if (!sessione_ferma()) ok = 0;
    }
done:
    if (!sessione_ferma()) ok = 0;
    SetEvent(server_release);
    if (thread) {
        if (WaitForSingleObject(server_done, 3000) != WAIT_OBJECT_0) ok = 0;
        WaitForSingleObject(thread, 1000); CloseHandle(thread);
    }
    if (server_errors != 0 || hello_count != (mode == RESP_STALL ? 1 : 2) ||
        viewport_count != (mode == RESP_STALL ? 0 : 1)) ok = 0;
    if (!ok) fprintf(stderr, "worker mode %d: hello=%ld viewport=%ld replies=%ld errors=%ld disconnect=%lu done=%lu\n",
                     mode, hello_count, viewport_count, response_count, server_errors,
                     WaitForSingleObject(client_disconnected, 0),
                     WaitForSingleObject(server_done, 0));
    prove++;
    return ok ? 0 : 1;
}

int main(void)
{
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "HabumiSessionTest";
    RegisterClassA(&wc);
    parent_window = CreateWindowA(wc.lpszClassName, "", 0, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    child_window = CreateWindowA(wc.lpszClassName, "", WS_CHILD, 0, 0, 1, 1, parent_window, NULL, wc.hInstance, NULL);
    server_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    request_received = CreateEventA(NULL, TRUE, FALSE, NULL);
    viewport_received = CreateEventA(NULL, TRUE, FALSE, NULL);
    viewport_replied = CreateEventA(NULL, TRUE, FALSE, NULL);
    server_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    server_release = CreateEventA(NULL, TRUE, FALSE, NULL);
    client_disconnected = CreateEventA(NULL, TRUE, FALSE, NULL);
    CHECK(parent_window && child_window && server_ready && request_received &&
          viewport_received && viewport_replied && server_done && server_release && client_disconnected);
    if (prova_wire()) return 1;
    if (prova_worker(RESP_MISMATCH) || prova_worker(RESP_REJECT) ||
        prova_worker(RESP_BAD_CAPS) || prova_worker(RESP_STALL) ||
        prova_worker(RESP_RESTART)) return 1;
    DestroyWindow(child_window); DestroyWindow(parent_window);
    CloseHandle(server_ready); CloseHandle(request_received);
    CloseHandle(viewport_received); CloseHandle(server_done);
    CloseHandle(viewport_replied);
    CloseHandle(server_release); CloseHandle(client_disconnected);
    printf("sessione: %d prove superate\n", prove);
    return 0;
}
