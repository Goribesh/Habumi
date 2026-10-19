/* SPDX-License-Identifier: GPL-2.0-only */
/* Solo thread proprietario della finestra; niente BQL o contesto GL. */
#ifndef WINQ_SESSION_H
#define WINQ_SESSION_H
#include "habumi-session.h"

static struct {
    HWND parent;
    DWORD owner;
    uint32_t token[4];
    HANDLE pipe, event;
    OVERLAPPED ov;
    HabumiSessionFrame frame;
    int stage;
    bool hello;
} winq_session = {.pipe = INVALID_HANDLE_VALUE};

static bool winq_session_parent(void)
{
    const char *session = getenv("HABUMI_SESSION");
    const char *handle = getenv("HABUMI_PARENT");
    const char *owner = getenv("HABUMI_OWNER");
    char *end;
    int consumed = 0;
    DWORD pid = 0;
    if (!session && !handle && !owner) { return true; }
    if (!session || !handle || !owner || strlen(session) != 32 ||
        sscanf(session, "%8x%8x%8x%8x%n", &winq_session.token[0],
               &winq_session.token[1], &winq_session.token[2],
               &winq_session.token[3], &consumed) != 4 || consumed != 32) { return false; }
    winq_session.parent = (HWND)(uintptr_t)strtoull(handle, &end, 10);
    if (*end || !winq_session.parent || !IsWindow(winq_session.parent)) { return false; }
    winq_session.owner = strtoul(owner, &end, 10);
    if (*end || !winq_session.owner) { return false; }
    GetWindowThreadProcessId(winq_session.parent, &pid);
    if (pid != winq_session.owner) { return false; }
    typedef DPI_AWARENESS_CONTEXT (WINAPI *GetContext)(HWND);
    typedef BOOL (WINAPI *EqualContext)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
    HMODULE user = GetModuleHandleW(L"user32.dll");
    GetContext get_context = (void *)GetProcAddress(user, "GetWindowDpiAwarenessContext");
    EqualContext equal_context = (void *)GetProcAddress(user, "AreDpiAwarenessContextsEqual");
    return get_context && equal_context && equal_context(get_context(winq_session.parent),
                                        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

static void winq_session_reset(void)
{
    DisconnectNamedPipe(winq_session.pipe);
    winq_session.hello = false;
    winq_session.stage = 1;
    ResetEvent(winq_session.event);
    memset(&winq_session.ov, 0, sizeof(winq_session.ov));
    winq_session.ov.hEvent = winq_session.event;
    BOOL connected = ConnectNamedPipe(winq_session.pipe, &winq_session.ov);
    DWORD error = connected ? ERROR_SUCCESS : GetLastError();
    if (connected || error == ERROR_PIPE_CONNECTED) {
        SetEvent(winq_session.event);
    } else if (error != ERROR_IO_PENDING) {
        error_report("winq/session: connect failed (%lu)", error);
    }
}

static bool winq_session_start(void)
{
    char pipe[96];
    if (!winq_session.parent) { return true; }
    snprintf(pipe, sizeof(pipe), "\\\\.\\pipe\\Habumi.A1.%s", getenv("HABUMI_SESSION"));
    winq_session.event = CreateEventW(NULL, TRUE, FALSE, NULL);
    winq_session.pipe = CreateNamedPipeA(pipe, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
        PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1024, 1024, 0, NULL);
    if (!winq_session.event || winq_session.pipe == INVALID_HANDLE_VALUE) {
        error_report("winq/session: create failed (%lu)", GetLastError());
        if (winq_session.event) { CloseHandle(winq_session.event); }
        if (winq_session.pipe != INVALID_HANDLE_VALUE) { CloseHandle(winq_session.pipe); }
        winq_session.event = NULL;
        return false;
    }
    winq_session_reset();
    info_report("winq/session: child=%llu parent=%llu v%u capabilities=0x%x",
                (unsigned long long)(uintptr_t)winq.hwnd,
                (unsigned long long)(uintptr_t)winq_session.parent, HS_VERSION, HS_CAPS);
    return true;
}

static void winq_session_apply(void)
{
    HabumiSessionFrame *r = &winq_session.frame;
    uint32_t status = hs_request_status(r, winq_session.token, GetTickCount64());
    ULONG client = 0;
    if (!GetNamedPipeClientProcessId(winq_session.pipe, &client) ||
        client != winq_session.owner) { status = HS_BAD_SESSION; }
    if (status == HS_OK && r->op != HS_HELLO && !winq_session.hello) {
        status = HS_BAD_SESSION;
    }
    if (status == HS_OK) {
        if (r->op == HS_HELLO) {
            winq_session.hello = true;
        } else if (r->op == HS_VIEWPORT) {
            if (!SetWindowPos(winq.hwnd, NULL, 0, 0, r->width, r->height,
                              SWP_NOZORDER | SWP_NOACTIVATE)) { status = HS_WINDOW_ERROR; }
        } else if (r->op == HS_FOCUS) {
            if (r->x) {
                /* Un comando rimasto in coda non deve rubare il fuoco ad altre app. */
                if (GetAncestor(winq.hwnd, GA_ROOT) == GetForegroundWindow()) {
                    SetFocus(winq.hwnd);
                    if (GetFocus() != winq.hwnd) { status = HS_WINDOW_ERROR; }
                } else {
                    status = HS_WINDOW_ERROR;
                }
            } else if (GetFocus() == winq.hwnd) {
                SetFocus(NULL);
            }
        }
    }
    r->magic = HS_MAGIC;
    r->version = HS_VERSION;
    r->reply = 1;
    r->status = status;
    r->caps = HS_CAPS;
    r->pid = GetCurrentProcessId();
    r->hwnd = (uintptr_t)winq.hwnd;
}

static void winq_session_step(void)
{
    /* Un limite per giro lascia sempre spazio ai messaggi di input. */
    for (unsigned i = 0; i < 8 && WaitForSingleObject(winq_session.event, 0) == WAIT_OBJECT_0; i++) {
        DWORD bytes = 0;
        if (winq_session.stage != 1 &&
            (!GetOverlappedResult(winq_session.pipe, &winq_session.ov, &bytes, FALSE) ||
             bytes != sizeof(winq_session.frame))) {
            winq_session_reset();
            return;
        }
        bool write = winq_session.stage == 2;
        if (write) { winq_session_apply(); }
        winq_session.stage = write ? 3 : 2;
        ResetEvent(winq_session.event);
        memset(&winq_session.ov, 0, sizeof(winq_session.ov));
        winq_session.ov.hEvent = winq_session.event;
        BOOL done = write ? WriteFile(winq_session.pipe, &winq_session.frame,
                           sizeof(winq_session.frame), &bytes, &winq_session.ov)
                          : ReadFile(winq_session.pipe, &winq_session.frame,
                           sizeof(winq_session.frame), &bytes, &winq_session.ov);
        if (done) { SetEvent(winq_session.event); }
        else if (GetLastError() != ERROR_IO_PENDING) { winq_session_reset(); return; }
    }
}

static BOOL winq_session_message(MSG *message)
{
    if (!winq_session.event) { return GetMessage(message, NULL, 0, 0); }
    for (;;) {
        if (WaitForSingleObject(winq_session.event, 0) == WAIT_OBJECT_0) {
            winq_session_step();
        }
        if (PeekMessage(message, NULL, 0, 0, PM_REMOVE)) {
            return message->message == WM_QUIT ? 0 : 1;
        }
        DWORD wait = MsgWaitForMultipleObjects(1, &winq_session.event, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_FAILED) { return -1; }
        if (wait == WAIT_OBJECT_0) { winq_session_step(); }
    }
}

static void winq_session_close(void)
{
    if (!winq_session.event) { return; }
    CancelIoEx(winq_session.pipe, &winq_session.ov);
    DWORD bytes;
    GetOverlappedResult(winq_session.pipe, &winq_session.ov, &bytes, TRUE);
    CloseHandle(winq_session.pipe);
    CloseHandle(winq_session.event);
    winq_session.event = NULL;
}
#endif
