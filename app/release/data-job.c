/* SPDX-License-Identifier: GPL-2.0-only */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "data-job.h"

bool data_quote(wchar_t *output, size_t count, const wchar_t *input)
{
    size_t used = 0, slashes = 0;
#define PUT(ch) do { if (used + 1 >= count) { if (count) output[0] = 0; return false; } output[used++] = (ch); } while (0)
    PUT(L'"');
    for (const wchar_t *p = input; ; ++p) {
        if (*p == L'\\') { ++slashes; continue; }
        size_t copies = (*p == L'"' || !*p) ? slashes * 2 : slashes;
        while (copies--) { PUT(L'\\'); }
        slashes = 0;
        if (!*p) break;
        if (*p == L'"') { PUT(L'\\'); }
        PUT(*p);
    }
    PUT(L'"'); output[used] = 0;
#undef PUT
    return true;
}

static bool json_string(wchar_t *out, size_t count, const wchar_t *text)
{
    size_t n = 0;
    for (; *text; ++text) {
        if (n + 7 >= count) { return false; }
        if (*text == L'"' || *text == L'\\') { out[n++] = L'\\'; out[n++] = *text; }
        else if (*text < 32) { swprintf(out + n, count - n, L"\\u%04x", (unsigned)*text); n += 6; }
        else { out[n++] = *text; }
    }
    out[n] = 0;
    return true;
}

bool data_job_start(DataJob *job, const wchar_t *root, const wchar_t *operation,
                    const wchar_t *path, const wchar_t *image, int size)
{
    GUID guid;
    wchar_t guid_text[40], temporary[DATA_PATH], request_path[DATA_PATH];
    wchar_t host[DATA_PATH], script[DATA_PATH], log_path[DATA_PATH];
    wchar_t quoted_host[DATA_PATH * 2], quoted_script[DATA_PATH * 2], quoted_request[DATA_PATH * 2];
    wchar_t escaped_root[DATA_PATH * 2], escaped_path[DATA_PATH * 2], command[DATA_PATH * 6];
    wchar_t request[DATA_PATH * 5];
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), NULL, TRUE};
    HANDLE log = INVALID_HANDLE_VALUE, input = INVALID_HANDLE_VALUE, file = INVALID_HANDLE_VALUE;
    char *utf8 = NULL;
    bool ok = false;
    if (job->process || wcslen(root) >= DATA_PATH - 128 || wcslen(path) >= DATA_PATH ||
        (wcscmp(operation,L"Backup") && wcscmp(operation,L"Restore") && wcscmp(operation,L"Grow") && wcscmp(operation,L"Recover") && wcscmp(operation,L"Diagnostics")) ||
        (wcscmp(image,L"data.img") && wcscmp(image,L"data-gapps.img"))) { return false; }
    ZeroMemory(job, sizeof(*job));
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid, guid_text, 40)) { return false; }
    unsigned n = 0;
    for (unsigned i = 0; guid_text[i]; ++i) {
        wchar_t c = guid_text[i];
        if (c >= L'A' && c <= L'F') c += L'a' - L'A';
        if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')) job->nonce[n++] = c;
    }
    DWORD temporary_length = GetTempPathW(DATA_PATH, temporary);
    if (n != 32 || !temporary_length || temporary_length >= DATA_PATH - 128) return false;
    if (swprintf(job->directory, DATA_PATH, L"%lsHabumi-data-%ls", temporary, job->nonce) < 0 ||
        !CreateDirectoryW(job->directory, NULL)) return false;
    swprintf(request_path, DATA_PATH, L"%ls\\request.json", job->directory);
    swprintf(log_path, DATA_PATH, L"%ls\\worker.log", job->directory);
    if (!json_string(escaped_root, DATA_PATH * 2, root) || !json_string(escaped_path, DATA_PATH * 2, path)) goto done;
    int length = swprintf(request, DATA_PATH * 5,
        L"{\"Schema\":\"habumi.data-ui.v1\",\"Nonce\":\"%ls\",\"ProfileRoot\":\"%ls\","
        L"\"Operation\":\"%ls\",\"Path\":\"%ls\",\"Image\":\"%ls\",\"SizeGiB\":%d}\n",
        job->nonce, escaped_root, operation, escaped_path, image, size);
    if (length < 0) goto done;
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, request, length, NULL, 0, NULL, NULL);
    if (!bytes || !(utf8 = malloc((size_t)bytes)) ||
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, request, length, utf8, bytes, NULL, NULL) != bytes) goto done;
    file = CreateFileW(request_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (file == INVALID_HANDLE_VALUE || !WriteFile(file, utf8, (DWORD)bytes, &written, NULL) || written != (DWORD)bytes || !FlushFileBuffers(file)) goto done;
    CloseHandle(file); file = INVALID_HANDLE_VALUE;
    if (!GetSystemDirectoryW(host, DATA_PATH)) goto done;
    if (wcslen(host) + 48 >= DATA_PATH) goto done;
    wcscat(host, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    swprintf(script, DATA_PATH, L"%ls\\Data-Worker.ps1", root);
    if (!data_quote(quoted_host, DATA_PATH * 2, host) || !data_quote(quoted_script, DATA_PATH * 2, script) ||
        !data_quote(quoted_request, DATA_PATH * 2, request_path)) goto done;
    if (swprintf(command, DATA_PATH * 6, L"%ls -NoProfile -NonInteractive -ExecutionPolicy Bypass -File %ls -RequestPath %ls",
                 quoted_host, quoted_script, quoted_request) < 0) goto done;
    log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, NULL);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) goto done;
    startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE; startup.hStdInput = input; startup.hStdOutput = log; startup.hStdError = log;
    if (!CreateProcessW(host, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, root, &startup, &process)) goto done;
    CloseHandle(process.hThread); job->process = process.hProcess;
    wcscpy(job->message, L"Starting the data operation..."); ok = true;
done:
    free(utf8);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (!ok) wcscpy(job->message, L"The data worker could not be started. Check the operation folder.");
    return ok;
}

bool data_job_status(DataJob *job, wchar_t *state, size_t count)
{
    wchar_t path[DATA_PATH], text[8192], expected[96];
    swprintf(path, DATA_PATH, L"%ls\\status.txt", job->directory);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD bytes = 0, size = GetFileSize(file, NULL);
    bool ok = size >= 4 && size < sizeof(text) && !(size & 1) && ReadFile(file, text, size, &bytes, NULL) && bytes == size;
    CloseHandle(file);
    if (!ok || text[0] != 0xfeff) return false;
    text[bytes / sizeof(wchar_t)] = 0;
    wchar_t *lines[4], *start = text + 1;
    for (int i = 0; i < 4; ++i) {
        lines[i] = start;
        wchar_t *end = wcschr(start, L'\n');
        if (!end) return false;
        *end = 0; start = end + 1;
    }
    swprintf(expected, 96, L"HABUMI-DATA-UI-V1 %ls", job->nonce);
    if (wcscmp(lines[0], expected) || wcslen(lines[1]) >= count || wcslen(lines[2]) >= 2049 || wcslen(lines[3]) >= DATA_PATH ||
        (wcscmp(lines[1], L"Working") && wcscmp(lines[1], L"Succeeded") && wcscmp(lines[1], L"Failed"))) return false;
    wcscpy(state, lines[1]); wcscpy(job->message, lines[2]); wcscpy(job->folder, lines[3]);
    return true;
}

int data_job_poll(DataJob *job)
{
    wchar_t state[32] = L"";
    if (job->process && WaitForSingleObject(job->process, 0) == WAIT_TIMEOUT) {
        data_job_status(job, state, 32);
        return 0;
    }
    /* Read the final status after exit; an earlier progress sample can race
     * the worker's last atomic status replacement. */
    bool valid = data_job_status(job, state, 32);
    DWORD code = 1;
    if (job->process) { GetExitCodeProcess(job->process, &code); CloseHandle(job->process); job->process = NULL; }
    if (valid && !wcscmp(state, L"Succeeded") && code == 0) return 1;
    if (valid && !wcscmp(state, L"Succeeded") && code != 0) wcscpy(job->message, L"The worker reported success but exited with an error. Inspect the operation folder.");
    if (!valid || !wcscmp(state, L"Working")) wcscpy(job->message, L"The worker stopped without a verified completion result. Inspect the operation folder before retrying.");
    if (!job->folder[0]) wcscpy(job->folder, job->directory);
    return -1;
}
