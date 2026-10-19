/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HABUMI_PROFILO_LOCK_H
#define HABUMI_PROFILO_LOCK_H
#include <stdbool.h>
#include <windows.h>

/* Stesso file esclusivo usato dalla manutenzione PowerShell. La presenza
 * del file non significa occupato: conta il suo handle, rilasciato da Windows
 * anche se il processo termina. Il marcatore segnala invece un recupero. */
static inline HANDLE profilo_lock_apri(void)
{
    return CreateFileA(".habumi-profile.lock", GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static inline bool profilo_recupero_pendente(void)
{
    return GetFileAttributesA("storage-maintenance.lock") != INVALID_FILE_ATTRIBUTES;
}
#endif
