/* SPDX-License-Identifier: GPL-2.0-only */
/* Il thread UI pubblica intenzioni; soltanto il worker attende la pipe. */
#include <stdio.h>
#include <windows.h>
#include <bcrypt.h>
#include "guscio.h"
#include "habumi-session.h"

static struct {
    HANDLE thread, stop, changed;
    CRITICAL_SECTION lock;
    HWND parent;
    char pipe[96];
    uint32_t token[4], pid, generation, seq;
    int width, height, focus;
    unsigned viewport_revision, focus_revision;
} session;

static bool session_io(HANDLE pipe, void *data, DWORD size, bool write)
{
    OVERLAPPED ov = {0};
    DWORD bytes = 0;
    bool ok = false;
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) { return false; }
    BOOL started = write ? WriteFile(pipe, data, size, &bytes, &ov)
                         : ReadFile(pipe, data, size, &bytes, &ov);
    if (started) {
        ok = bytes == size;
    } else if (GetLastError() == ERROR_IO_PENDING) {
        HANDLE waits[] = {session.stop, ov.hEvent};
        if (WaitForMultipleObjects(2, waits, FALSE, 2000) == WAIT_OBJECT_0 + 1) {
            ok = GetOverlappedResult(pipe, &ov, &bytes, FALSE) && bytes == size;
        } else {
            CancelIoEx(pipe, &ov);
            /* Il buffer resta vivo finche' il kernel ha concluso la richiesta. */
            GetOverlappedResult(pipe, &ov, &bytes, TRUE);
        }
    }
    CloseHandle(ov.hEvent);
    return ok;
}

static bool session_request(HANDLE pipe, uint32_t pid, unsigned op,
                            int width, int height, int focus)
{
    HabumiSessionFrame request = {0}, reply;
    request.magic = HS_MAGIC;
    request.version = HS_VERSION;
    request.op = op;
    request.seq = ++session.seq;
    memcpy(request.token, session.token, sizeof(request.token));
    request.deadline = GetTickCount64() + 2000;
    request.width = width;
    request.height = height;
    request.x = op == HS_FOCUS ? focus : 0;
    if (!session_io(pipe, &request, sizeof(request), true) ||
        !session_io(pipe, &reply, sizeof(reply), false)) {
        registro_riga(REG_GUSCIO, "session: command %u timed out or disconnected", op);
        return false;
    }
    if (!hs_reply_matches(&reply, &request, pid) || reply.status != HS_OK) {
        registro_riga(REG_GUSCIO, "session: command %u rejected (status %u)",
                      op, reply.status);
        return false;
    }
    if (op == HS_HELLO) {
        HWND child = (HWND)(uintptr_t)reply.hwnd;
        DWORD owner = 0;
        GetWindowThreadProcessId(child, &owner);
        if (owner != pid || GetParent(child) != session.parent) { return false; }
        registro_riga(REG_GUSCIO, "session: v%u capabilities=0x%x QEMU pid=%u child=%llu connected",
                      reply.version, reply.caps, pid, (unsigned long long)reply.hwnd);
    }
    return true;
}

static DWORD WINAPI session_worker(void *unused)
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    uint32_t generation = 0;
    unsigned viewport = 0, focus_revision = 0;
    (void)unused;
    for (;;) {
        HANDLE waits[] = {session.stop, session.changed};
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE,
                    pipe == INVALID_HANDLE_VALUE ? 200 : INFINITE);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) { break; }
        EnterCriticalSection(&session.lock);
        uint32_t pid = session.pid, current = session.generation;
        int width = session.width, height = session.height, focus = session.focus;
        unsigned vr = session.viewport_revision, fr = session.focus_revision;
        LeaveCriticalSection(&session.lock);
        if (current != generation) {
            if (pipe != INVALID_HANDLE_VALUE) { CloseHandle(pipe); }
            pipe = INVALID_HANDLE_VALUE;
            generation = current;
            viewport = focus_revision = 0;
        }
        if (!pid) { continue; }
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe = CreateFileA(session.pipe, GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (pipe == INVALID_HANDLE_VALUE) { continue; }
            ULONG server = 0;
            if (!GetNamedPipeServerProcessId(pipe, &server) || server != pid ||
                !session_request(pipe, pid, HS_HELLO, 0, 0, 0)) { goto disconnected; }
        }
        if (vr != viewport && width > 0 && height > 0) {
            if (!session_request(pipe, pid, HS_VIEWPORT, width, height, 0)) { goto disconnected; }
            viewport = vr;
        }
        if (fr != focus_revision) {
            if (!session_request(pipe, pid, HS_FOCUS, 0, 0, focus)) { goto disconnected; }
            focus_revision = fr;
        }
        continue;
disconnected:
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
        viewport = focus_revision = 0;
    }
    if (pipe != INVALID_HANDLE_VALUE) { CloseHandle(pipe); }
    return 0;
}

bool sessione_avvia(HWND parent)
{
    char value[64];
    if (session.thread) { return false; }
    memset(&session, 0, sizeof(session));
    if (BCryptGenRandom(NULL, (PUCHAR)session.token,
        sizeof(session.token), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) { return false; }
    session.parent = parent;
    snprintf(value, sizeof(value), "%08x%08x%08x%08x", session.token[0],
             session.token[1], session.token[2], session.token[3]);
    snprintf(session.pipe, sizeof(session.pipe), "\\\\.\\pipe\\Habumi.A1.%s", value);
    if (!SetEnvironmentVariableA("HABUMI_SESSION", value)) { return false; }
    snprintf(value, sizeof(value), "%llu", (unsigned long long)(uintptr_t)parent);
    if (!SetEnvironmentVariableA("HABUMI_PARENT", value)) { return false; }
    snprintf(value, sizeof(value), "%lu", GetCurrentProcessId());
    if (!SetEnvironmentVariableA("HABUMI_OWNER", value)) { return false; }
    InitializeCriticalSection(&session.lock);
    session.stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    session.changed = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (session.stop && session.changed) {
        session.thread = CreateThread(NULL, 0, session_worker, NULL, 0, NULL);
    }
    if (!session.thread) {
        if (session.stop) { CloseHandle(session.stop); }
        if (session.changed) { CloseHandle(session.changed); }
        DeleteCriticalSection(&session.lock);
        return false;
    }
    return true;
}

void sessione_qemu(DWORD pid)
{
    if (!session.thread) { return; }
    EnterCriticalSection(&session.lock);
    session.pid = pid;
    session.generation++;
    LeaveCriticalSection(&session.lock);
    SetEvent(session.changed);
}

void sessione_area(int width, int height)
{
    if (!session.thread || width < 1 || height < 1) { return; }
    EnterCriticalSection(&session.lock);
    session.width = width;
    session.height = height;
    session.viewport_revision++;
    LeaveCriticalSection(&session.lock);
    SetEvent(session.changed);
}

void sessione_focus(bool active)
{
    if (!session.thread) { return; }
    EnterCriticalSection(&session.lock);
    session.focus = active;
    session.focus_revision++;
    LeaveCriticalSection(&session.lock);
    SetEvent(session.changed);
}

bool sessione_ferma(void)
{
    if (!session.thread) { return true; }
    SetEvent(session.stop);
    if (WaitForSingleObject(session.thread, 3000) != WAIT_OBJECT_0) { return false; }
    CloseHandle(session.thread);
    CloseHandle(session.stop);
    CloseHandle(session.changed);
    session.thread = NULL;
    DeleteCriticalSection(&session.lock);
    return true;
}
