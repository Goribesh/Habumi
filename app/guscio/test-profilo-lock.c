/* SPDX-License-Identifier: GPL-2.0-only */
/* Processo indipendente: il test PowerShell compete con questo handle. */
#include <stdbool.h>
#include <stdio.h>
#include "profilo-lock.h"
int main(void)
{
    HANDLE lock = profilo_lock_apri();
    if (lock == INVALID_HANDLE_VALUE) { return 2; }
    FILE *ready = fopen("lock-ready.txt", "w");
    if (!ready) { return 1; }
    fputs("ready", ready);
    fclose(ready);
    Sleep(1500);
    /* L'uscita rilascia l'handle, come una terminazione inattesa. */
    return 0;
}
