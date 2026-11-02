/* SPDX-License-Identifier: GPL-2.0-only */
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "screenshot.h"

#define SHOT_TIMEOUT_MS 20000
#define SHOT_MAX_BYTES (64u * 1024u * 1024u)
#define SHOT_MAX_PIXELS (64u * 1024u * 1024u)

typedef struct ShotWork {
    wchar_t target[32768];
    wchar_t temporary[32768];
    char adb[32768];
    int port;
    HANDLE cancel;
    HANDLE thread;
    volatile LONG done;
    volatile LONG result;
    wchar_t message[256];
} ShotWork;

static ShotWork shot;
static volatile LONG shot_counter;

static void shot_message(ShotWork *w, const wchar_t *s)
{
    wcsncpy(w->message, s, 255);
    w->message[255] = L'\0';
}

static bool shot_target_available(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES) return false;
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static bool shot_make_temp(ShotWork *w)
{
    unsigned attempt;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    for (attempt = 0; attempt < 32; attempt++) {
        LONG n = InterlockedIncrement(&shot_counter);
        int written = _snwprintf(w->temporary, 32768,
                                L"%ls.habumi-%08lx-%08lx-%08lx.tmp",
                                w->target, GetCurrentProcessId(),
                                (DWORD)GetTickCount(), (DWORD)n);
        HANDLE h;
        if (written < 0 || written >= 32768) return false;
        h = CreateFileW(w->temporary, GENERIC_WRITE | GENERIC_READ,
                        FILE_SHARE_READ, &sa, CREATE_NEW,
                        FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return true;
        }
        if (GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return false;
}

static bool shot_quote(const wchar_t *arg, wchar_t *out, size_t cap)
{
    size_t i = 0, used = 0, slashes = 0;
    if (!arg || !out || cap < 3) return false;
    out[used++] = L'"';
    while (arg[i]) {
        if (arg[i] == L'\\') { slashes++; i++; continue; }
        if (arg[i] == L'"') {
            while (slashes) {
                if (used + 2 >= cap) return false;
                out[used++] = L'\\'; out[used++] = L'\\'; slashes--;
            }
            if (used + 2 >= cap) return false;
            out[used++] = L'\\'; out[used++] = L'"'; i++;
            continue;
        }
        while (slashes) {
            if (used + 2 >= cap) return false;
            out[used++] = L'\\'; slashes--;
        }
        if (used + 2 >= cap) return false;
        out[used++] = arg[i++];
    }
    while (slashes) {
        if (used + 2 >= cap) return false;
        out[used++] = L'\\'; out[used++] = L'\\'; slashes--;
    }
    if (used + 2 > cap) return false;
    out[used++] = L'"'; out[used] = L'\0';
    return true;
}

static bool shot_command(const char *adb, int port, wchar_t *line, size_t cap)
{
    wchar_t exe[32768], quoted[65536];
    int count;
    if (!adb || !*adb || port < 1 || port > 65535) return false;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, adb, -1,
                                exe, 32768);
    if (!count || !shot_quote(exe, quoted, 65536)) return false;
    count = _snwprintf(line, cap,
                       L"%ls -s 127.0.0.1:%d exec-out screencap -p",
                       quoted, port);
    return count >= 0 && (size_t)count < cap;
}

static bool shot_validate_png(const wchar_t *path)
{
    HRESULT hr;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    GUID format;
    UINT width = 0, height = 0;
    bool valid = false;
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr)) {
        hr = IWICImagingFactory_CreateDecoderFromFilename(
            factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
            &decoder);
    }
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetContainerFormat(decoder, &format);
    if (SUCCEEDED(hr) && !IsEqualGUID(&format, &GUID_ContainerFormatPng))
        hr = WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameDecode_GetSize(frame, &width, &height);
    if (SUCCEEDED(hr) && width && height && width <= 32768 && height <= 32768 &&
        (uint64_t)width * height <= SHOT_MAX_PIXELS) {
        UINT stride = width * 4;
        size_t bytes = (size_t)stride * height;
        BYTE *pixels = (BYTE *)malloc(bytes);
        if (pixels) {
            hr = IWICBitmapFrameDecode_CopyPixels(frame, NULL, stride,
                                                  (UINT)bytes, pixels);
            free(pixels);
            valid = SUCCEEDED(hr);
        }
    }
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    CoUninitialize();
    return valid;
}

static DWORD WINAPI shot_worker(void *opaque)
{
    ShotWork *w = (ShotWork *)opaque;
    HANDLE output = INVALID_HANDLE_VALUE, nul_in = INVALID_HANDLE_VALUE;
    HANDLE nul_err = INVALID_HANDLE_VALUE, job = NULL;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    SIZE_T attr_size = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    HANDLE inherited[3];
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    wchar_t command[66000];
    DWORD outcome = (DWORD)-1, result = -1;
    bool timed_out = false, cancelled = false, too_big = false;
    bool flush_ok = false;
    bool attributes_ready = false;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    memset(&si, 0, sizeof(si)); memset(&pi, 0, sizeof(pi));
    if (!shot_command(w->adb, w->port, command,
                      sizeof(command) / sizeof(command[0]))) {
        shot_message(w, L"Screenshot failed: invalid adb path or port.");
        goto done;
    }
    output = CreateFileW(w->temporary, GENERIC_WRITE | GENERIC_READ,
                         FILE_SHARE_READ, &sa, OPEN_EXISTING,
                         FILE_ATTRIBUTE_TEMPORARY, NULL);
    nul_in = CreateFileW(L"NUL", GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    nul_err = CreateFileW(L"NUL", GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE || nul_in == INVALID_HANDLE_VALUE ||
        nul_err == INVALID_HANDLE_VALUE) {
        shot_message(w, L"Screenshot failed: could not prepare capture files.");
        goto done;
    }
    SetFilePointer(output, 0, NULL, FILE_BEGIN);
    SetEndOfFile(output);
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
        shot_message(w, L"Screenshot failed: could not prepare adb process.");
        goto done;
    }
    attributes_ready = true;
    inherited[0] = nul_in; inherited[1] = output; inherited[2] = nul_err;
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited, sizeof(inherited), NULL, NULL)) {
        shot_message(w, L"Screenshot failed: could not restrict adb handles.");
        goto done;
    }
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = nul_in;
    si.StartupInfo.hStdOutput = output;
    si.StartupInfo.hStdError = nul_err;
    si.lpAttributeList = attrs;
    job = CreateJobObjectW(NULL, NULL);
    if (!job) {
        shot_message(w, L"Screenshot failed: could not create adb job.");
        goto done;
    }
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        shot_message(w, L"Screenshot failed: could not configure adb job.");
        goto done;
    }
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED |
                        EXTENDED_STARTUPINFO_PRESENT,
                        NULL, NULL, &si.StartupInfo, &pi)) {
        shot_message(w, L"Screenshot failed: adb could not start.");
        goto done;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess) ||
        ResumeThread(pi.hThread) == (DWORD)-1) {
        /* Assignment can fail before the suspended child belongs to the job. */
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        shot_message(w, L"Screenshot failed: adb could not enter its job.");
        goto process_done;
    }
    {
        ULONGLONG deadline = GetTickCount64() + SHOT_TIMEOUT_MS;
        for (;;) {
            LARGE_INTEGER size;
            if (WaitForSingleObject(w->cancel, 0) == WAIT_OBJECT_0) {
                cancelled = true; break;
            }
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) break;
            if (!GetFileSizeEx(output, &size) || size.QuadPart > SHOT_MAX_BYTES) {
                too_big = true; break;
            }
            if (GetTickCount64() >= deadline) { timed_out = true; break; }
        }
    }
    if (cancelled || timed_out || too_big) {
        TerminateJobObject(job, 2);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    if (!cancelled && !timed_out && !too_big)
        flush_ok = FlushFileBuffers(output) != 0;
    CloseHandle(output); output = INVALID_HANDLE_VALUE;
    CloseHandle(nul_in); nul_in = INVALID_HANDLE_VALUE;
    CloseHandle(nul_err); nul_err = INVALID_HANDLE_VALUE;
    GetExitCodeProcess(pi.hProcess, &outcome);
    if (cancelled) shot_message(w, L"Screenshot cancelled.");
    else if (timed_out) shot_message(w, L"Screenshot failed: adb timed out.");
    else if (too_big) shot_message(w, L"Screenshot failed: PNG exceeds 64 MiB.");
    else if (WaitForSingleObject(w->cancel, 0) == WAIT_OBJECT_0)
        shot_message(w, L"Screenshot cancelled.");
    else if (outcome != 0) shot_message(w, L"Screenshot failed: adb returned an error.");
    else if (!flush_ok) shot_message(w, L"Screenshot failed: capture data could not be flushed.");
    else {
        HANDLE file = CreateFileW(w->temporary, GENERIC_READ, FILE_SHARE_READ,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  NULL);
        LARGE_INTEGER size;
        bool okay = file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) &&
                    size.QuadPart >= 8 && (uint64_t)size.QuadPart <= SHOT_MAX_BYTES;
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (!okay) shot_message(w, L"Screenshot failed: PNG size is invalid.");
        else if (!shot_validate_png(w->temporary))
            shot_message(w, L"Screenshot failed: adb output is not a valid PNG.");
        else if (WaitForSingleObject(w->cancel, 0) == WAIT_OBJECT_0)
            shot_message(w, L"Screenshot cancelled.");
        else if (!MoveFileExW(w->temporary, w->target, MOVEFILE_WRITE_THROUGH))
            shot_message(w, L"Screenshot failed: target already exists or cannot be written.");
        else { result = 1; shot_message(w, L"Screenshot saved."); }
    }
process_done:
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
done:
    if (attrs) { if(attributes_ready)DeleteProcThreadAttributeList(attrs); free(attrs); }
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
    if (nul_err != INVALID_HANDLE_VALUE) CloseHandle(nul_err);
    if (job) CloseHandle(job);
    if (result != 1 && w->temporary[0]) DeleteFileW(w->temporary);
    InterlockedExchange(&w->result, result);
    InterlockedExchange(&w->done, 1);
    return 0;
}

bool screenshot_start(const char *adb, int port, const wchar_t *target)
{
    ShotWork *w = &shot;
    size_t target_len;
    if (w->thread && WaitForSingleObject(w->thread, 0) != WAIT_OBJECT_0)
        return false;
    if (w->thread) {
        CloseHandle(w->thread); CloseHandle(w->cancel);
        memset(w, 0, sizeof(*w));
    }
    if (!adb || !*adb || !target || !*target || port < 1 || port > 65535)
        return false;
    target_len = wcslen(target);
    if (target_len >= 32768 || strlen(adb) >= sizeof(w->adb) ||
        !shot_target_available(target)) return false;
    wcscpy(w->target, target);
    strcpy(w->adb, adb);
    w->port = port;
    w->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!w->cancel || !shot_make_temp(w)) {
        if (w->cancel) CloseHandle(w->cancel);
        memset(w, 0, sizeof(*w)); return false;
    }
    w->thread = CreateThread(NULL, 0, shot_worker, w, 0, NULL);
    if (!w->thread) {
        DeleteFileW(w->temporary); CloseHandle(w->cancel);
        memset(w, 0, sizeof(*w)); return false;
    }
    return true;
}

int screenshot_poll(wchar_t *message, size_t cap)
{
    ShotWork *w = &shot;
    int result;
    if (!w->thread) return -1;
    if (WaitForSingleObject(w->thread, 0) != WAIT_OBJECT_0) return 0;
    result = (int)w->result;
    if (message && cap) {
        wcsncpy(message, w->message, cap - 1);
        message[cap - 1] = L'\0';
    }
    CloseHandle(w->thread); CloseHandle(w->cancel);
    memset(w, 0, sizeof(*w));
    return result;
}

bool screenshot_stop(void)
{
    if (!shot.thread) return true;
    SetEvent(shot.cancel);
    return WaitForSingleObject(shot.thread, 0) == WAIT_OBJECT_0;
}
