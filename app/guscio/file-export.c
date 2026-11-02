/* SPDX-License-Identifier: GPL-2.0-only */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdbool.h>
#include <wchar.h>
#include "../release/data-job.h"
#include "file-export.h"

enum { GUEST_PATH_EDIT = 310, DESTINATION_EDIT, CHOOSE_DESTINATION,
       START_EXPORT, CLOSE_EXPORT };

typedef struct {
    wchar_t root[DATA_PATH];
    int port;
    HWND owner, window, heading, description, guest_label, guest_path;
    HWND destination_label, destination, choose, status, progress, start, close;
    HFONT font, heading_font;
    UINT dpi;
    DataJob job;
} FileExportUi;

static FileExportUi ui;
static const wchar_t window_class[] = L"HabumiAndroidFileExport";

static int scale(int value)
{
    return MulDiv(value, (int)ui.dpi, 96);
}

static void place(HWND child, int x, int y, int width, int height)
{
    MoveWindow(child, scale(x), scale(y), scale(width), scale(height), TRUE);
}

static HWND make_control(const wchar_t *kind, const wchar_t *text,
                         DWORD style, int id)
{
    return CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, ui.window, (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL), NULL);
}

static void layout(void)
{
    RECT client;
    GetClientRect(ui.window, &client);
    int width = MulDiv(client.right, 96, (int)ui.dpi);
    int height = MulDiv(client.bottom, 96, (int)ui.dpi);
    place(ui.heading, 18, 14, width - 36, 28);
    place(ui.description, 18, 48, width - 36, 38);
    place(ui.guest_label, 18, 94, width - 36, 20);
    place(ui.guest_path, 18, 117, width - 36, 28);
    place(ui.destination_label, 18, 158, width - 36, 20);
    place(ui.destination, 18, 181, width - 122, 28);
    place(ui.choose, width - 96, 181, 78, 28);
    place(ui.status, 18, 224, width - 36, 54);
    place(ui.progress, 18, height - 58, width - 36, 8);
    place(ui.start, width - 188, height - 40, 82, 28);
    place(ui.close, width - 98, height - 40, 80, 28);
}

static void apply_fonts(void)
{
    if (ui.font) { DeleteObject(ui.font); }
    if (ui.heading_font) { DeleteObject(ui.heading_font); }
    ui.font = CreateFontW(-scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    ui.heading_font = CreateFontW(-scale(21), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
        L"Segoe UI");
    HWND controls[] = {ui.description, ui.guest_label, ui.guest_path,
        ui.destination_label, ui.destination, ui.choose, ui.status,
        ui.start, ui.close};
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        if (controls[i]) { SendMessageW(controls[i], WM_SETFONT, (WPARAM)ui.font, TRUE); }
    }
    if (ui.heading) { SendMessageW(ui.heading, WM_SETFONT, (WPARAM)ui.heading_font, TRUE); }
}

static bool guest_path_valid(const wchar_t *path)
{
    const wchar_t *relative;
    if (!path || !*path || wcslen(path) >= DATA_PATH || wcschr(path, L'\\')) { return false; }
    if (!wcsncmp(path, L"/sdcard/", 8)) { relative = path + 8; }
    else if (!wcsncmp(path, L"/storage/emulated/0/", 20)) { relative = path + 20; }
    else { return false; }
    if (!*relative) { return false; }
    const wchar_t *segment = relative;
    for (const wchar_t *p = relative; ; p++) {
        if (*p && (*p < 32 || (*p >= 0x7f && *p <= 0x9f))) { return false; }
        if (*p == L'/' || !*p) {
            size_t length = (size_t)(p - segment);
            if (!length || (length == 1 && segment[0] == L'.') ||
                (length == 2 && segment[0] == L'.' && segment[1] == L'.')) { return false; }
            if (!*p) { break; }
            segment = p + 1;
        }
    }
    return true;
}

static bool destination_valid(const wchar_t *path)
{
    wchar_t parent[DATA_PATH];
    size_t length;
    DWORD attributes;
    if (!path || !*path || (length = wcslen(path)) >= DATA_PATH) { return false; }
    attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) { return false; }
    wcscpy(parent, path);
    wchar_t *slash = wcsrchr(parent, L'\\');
    if (!slash) { slash = wcsrchr(parent, L'/'); }
    if (!slash || slash == parent) { return false; }
    if (slash == parent + 2 && parent[1] == L':') { slash[1] = L'\0'; }
    else { *slash = L'\0'; }
    attributes = GetFileAttributesW(parent);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static void update_controls(void)
{
    wchar_t guest[DATA_PATH], destination[DATA_PATH];
    bool busy = file_export_busy();
    GetWindowTextW(ui.guest_path, guest, DATA_PATH);
    GetWindowTextW(ui.destination, destination, DATA_PATH);
    bool valid = guest_path_valid(guest) && destination_valid(destination);
    EnableWindow(ui.guest_path, !busy);
    EnableWindow(ui.destination, !busy);
    EnableWindow(ui.choose, !busy);
    EnableWindow(ui.start, !busy && valid);
    SetWindowTextW(ui.close, busy ? L"Hide" : L"Close");
}

static void set_status(const wchar_t *text)
{
    if (ui.status) { SetWindowTextW(ui.status, text); }
}

static void choose_destination(void)
{
    OPENFILENAMEW dialog = {0};
    wchar_t path[DATA_PATH] = {0}, guest[DATA_PATH] = {0};
    GetWindowTextW(ui.destination, path, DATA_PATH);
    if (!path[0]) {
        GetWindowTextW(ui.guest_path, guest, DATA_PATH);
        const wchar_t *name = wcsrchr(guest, L'/');
        name = name ? name + 1 : L"android-file.bin";
        if (swprintf(path, DATA_PATH, L"%ls", name) < 0) { path[0] = L'\0'; }
    }
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = ui.window;
    dialog.lpstrFilter = L"All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = DATA_PATH;
    dialog.lpstrTitle = L"Save exported Android file";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                   OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&dialog)) {
        SetWindowTextW(ui.destination, path);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            set_status(L"That file already exists. Choose a new destination; existing files are never replaced.");
        } else {
            set_status(L"Choose an Android file path, then select Export.");
        }
    }
    update_controls();
}

static void start_export(void)
{
    wchar_t guest[DATA_PATH], destination[DATA_PATH];
    GetWindowTextW(ui.guest_path, guest, DATA_PATH);
    GetWindowTextW(ui.destination, destination, DATA_PATH);
    if (!guest_path_valid(guest)) {
        set_status(L"Enter a file under /sdcard/ or /storage/emulated/0/. Include its file name.");
        return;
    }
    if (!destination_valid(destination)) {
        set_status(L"Choose a new destination file in an existing folder. Existing files are never replaced.");
        return;
    }
    if (!data_export_start(&ui.job, ui.root, guest, destination, ui.port)) {
        set_status(ui.job.message);
        update_controls();
        return;
    }
    SetWindowTextW(ui.progress, L"");
    SendMessageW(ui.progress, PBM_SETMARQUEE, TRUE, 50);
    set_status(ui.job.message);
    update_controls();
}

void file_export_tick(void)
{
    if (!ui.window || !ui.job.process) { return; }
    int result = data_job_poll(&ui.job);
    if (result) {
        SendMessageW(ui.progress, PBM_SETMARQUEE, FALSE, 0);
        if (result > 0) {
            wchar_t message[DATA_PATH];
            if (ui.job.folder[0]) {
                swprintf(message, DATA_PATH, L"Export complete: %ls", ui.job.folder);
                set_status(message);
            } else { set_status(ui.job.message); }
        } else { set_status(ui.job.message); }
    } else { set_status(ui.job.message); }
    update_controls();
}

bool file_export_busy(void)
{
    return ui.job.process != NULL;
}

static LRESULT CALLBACK export_window_proc(HWND window, UINT message,
                                            WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wp) == CHOOSE_DESTINATION) { choose_destination(); }
        else if (LOWORD(wp) == START_EXPORT) { start_export(); }
        else if (LOWORD(wp) == CLOSE_EXPORT) { SendMessageW(window, WM_CLOSE, 0, 0); }
        else if ((LOWORD(wp) == GUEST_PATH_EDIT || LOWORD(wp) == DESTINATION_EDIT) &&
                 HIWORD(wp) == EN_CHANGE) { update_controls(); }
        return 0;
    case WM_TIMER:
        if (wp == 1) { file_export_tick(); return 0; }
        break;
    case WM_CLOSE:
        if (file_export_busy()) {
            set_status(L"The export will continue while this window is hidden. Keep Habumi open until it finishes.");
            ShowWindow(window, SW_HIDE);
        } else { DestroyWindow(window); }
        return 0;
    case WM_QUERYENDSESSION:
        return file_export_busy() ? FALSE : TRUE;
    case WM_DESTROY:
        KillTimer(window, 1);
        if (ui.font) { DeleteObject(ui.font); ui.font = NULL; }
        if (ui.heading_font) { DeleteObject(ui.heading_font); ui.heading_font = NULL; }
        ui.window = NULL;
        ZeroMemory(&ui, sizeof(ui));
        return 0;
    case WM_SIZE:
        if (ui.heading) { layout(); }
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = scale(500);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = scale(365);
        return 0;
    case WM_DPICHANGED: {
        ui.dpi = HIWORD(wp);
        RECT *suggested = (RECT *)lp;
        SetWindowPos(window, NULL, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        apply_fonts(); layout();
        return 0;
    }
    case WM_ERASEBKGND:
        return DefWindowProcW(window, message, wp, lp);
    }
    return DefWindowProcW(window, message, wp, lp);
}

HWND file_export_open(HWND owner, const wchar_t *root, int port)
{
    if (!root || wcslen(root) >= DATA_PATH - 128 || port < 1024 || port > 65535 ||
        (port >= 5554 && port <= 5585)) { return NULL; }
    if (ui.window) {
        ShowWindow(ui.window, SW_RESTORE);
        SetForegroundWindow(ui.window);
        return ui.window;
    }
    WNDCLASSW window_class_info = {0};
    window_class_info.lpfnWndProc = export_window_proc;
    window_class_info.hInstance = GetModuleHandleW(NULL);
    window_class_info.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    window_class_info.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class_info.lpszClassName = window_class;
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_PROGRESS_CLASS};
    if (!InitCommonControlsEx(&common)) { return NULL; }
    if (!RegisterClassW(&window_class_info) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return NULL;
    }
    wcscpy(ui.root, root);
    ui.port = port;
    ui.owner = owner;
    ui.dpi = 96;
    ui.window = CreateWindowExW(WS_EX_TOOLWINDOW, window_class,
        L"Habumi - Export Android file", WS_OVERLAPPED | WS_CAPTION |
        WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        scale(620), scale(410), owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!ui.window) { ZeroMemory(&ui, sizeof(ui)); return NULL; }
    ui.dpi = GetDpiForWindow(ui.window);
    ui.heading = make_control(L"STATIC", L"Export a file from Android", 0, 0);
    ui.description = make_control(L"STATIC",
        L"Copy one file from Android shared storage to a new Windows file. The existing file is never replaced.",
        SS_LEFT, 0);
    ui.guest_label = make_control(L"STATIC", L"Android file path", 0, 0);
    ui.guest_path = make_control(L"EDIT", L"/sdcard/Download/filename",
        WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, GUEST_PATH_EDIT);
    SendMessageW(ui.guest_path, EM_SETLIMITTEXT, DATA_PATH - 1, 0);
    ui.destination_label = make_control(L"STATIC", L"Save as", 0, 0);
    ui.destination = make_control(L"EDIT", L"", WS_TABSTOP | WS_BORDER |
        ES_AUTOHSCROLL | ES_READONLY, DESTINATION_EDIT);
    SendMessageW(ui.destination, EM_SETLIMITTEXT, DATA_PATH - 1, 0);
    ui.choose = make_control(L"BUTTON", L"Choose...", WS_TABSTOP, CHOOSE_DESTINATION);
    ui.status = make_control(L"STATIC", L"Choose a destination file to begin.",
        SS_LEFT | SS_NOPREFIX, 0);
    ui.progress = make_control(PROGRESS_CLASSW, L"", PBS_MARQUEE, 0);
    ui.start = make_control(L"BUTTON", L"Export", WS_TABSTOP | BS_DEFPUSHBUTTON,
        START_EXPORT);
    ui.close = make_control(L"BUTTON", L"Close", WS_TABSTOP, CLOSE_EXPORT);
    if (!ui.heading || !ui.description || !ui.guest_label || !ui.guest_path ||
        !ui.destination_label || !ui.destination || !ui.choose || !ui.status ||
        !ui.progress || !ui.start || !ui.close) {
        DestroyWindow(ui.window);
        return NULL;
    }
    apply_fonts();
    SetWindowPos(ui.window, NULL, 0, 0, scale(620), scale(410),
        SWP_NOMOVE | SWP_NOZORDER);
    layout();
    SetTimer(ui.window, 1, 200, NULL);
    ShowWindow(ui.window, SW_SHOW);
    SetForegroundWindow(ui.window);
    SetFocus(ui.guest_path);
    update_controls();
    return ui.window;
}

bool file_export_message(MSG *message)
{
    return message && ui.window && IsWindow(ui.window) &&
           IsDialogMessageW(ui.window, message) != FALSE;
}
