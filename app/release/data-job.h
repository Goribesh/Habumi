/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_DATA_JOB_H
#define HABUMI_DATA_JOB_H
#include <windows.h>
#include <stdbool.h>
#define DATA_PATH 4096
typedef struct {
    HANDLE process;
    wchar_t directory[DATA_PATH], nonce[33];
    wchar_t message[2049], folder[DATA_PATH];
} DataJob;
bool data_quote(wchar_t *output, size_t count, const wchar_t *input);
bool data_job_start(DataJob *job, const wchar_t *root, const wchar_t *operation,
                    const wchar_t *path, const wchar_t *image, int size);
/* 0 running, 1 success, -1 failure. Never terminates a worker. */
int data_job_poll(DataJob *job);
bool data_job_status(DataJob *job, wchar_t *state, size_t count);
#endif
