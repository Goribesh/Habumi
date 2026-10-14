/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise the same control controller on the explicitly selected test guest. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"
int main(int argc, char **argv)
{
    Config config;
    config_default(&config);
    config.porta_adb = 16555;
    registro_apri();
    bool ruota = argc == 2 && !strcmp(argv[1], "--rotation-pair");
    if (argc > 1 && !ruota) { return 2; }
    if (ruota) {
        if (!comandi_accoda(&config, COMANDO_RUOTA, 0) ||
            !comandi_accoda(&config, COMANDO_RUOTA, 0)) { return 1; }
    } else if (!comandi_accoda(&config, COMANDO_DENSITA, 426) ||
        !comandi_accoda(&config, COMANDO_HOME, 0) ||
        !comandi_accoda(&config, COMANDO_RECENTI, 0) ||
        !comandi_accoda(&config, COMANDO_HOME, 0) ||
        !comandi_accoda(&config, COMANDO_BACK, 0)) { return 1; }
    ULONGLONG end = GetTickCount64() + 25000;
    while (comandi_in_corso() && GetTickCount64() < end) {
        comandi_passo();
        Sleep(50);
    }
    bool busy = comandi_in_corso();
    if (!comandi_annulla()) { return 2; }
    char log[16384];
    registro_nuove(log, sizeof(log));
    printf("%s", log);
    bool ok = !strstr(log, " failed") && !strstr(log, "discarded");
    if (ruota) {
        ok = ok && strstr(log, "rotation: applied user_rotation=1") &&
                   strstr(log, "rotation: applied user_rotation=0");
    } else {
        ok = ok && strstr(log, "density: applied 426") &&
                   strstr(log, "controls: recent apps delivered") &&
                   strstr(log, "controls: back delivered");
    }
    registro_chiudi();
    return busy || !ok ? 1 : 0;
}
