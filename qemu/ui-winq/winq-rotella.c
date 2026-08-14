/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-rotella.c -- vedi winq-rotella.h per il perche' di questo file. */
#include "winq-rotella.h"

/* WHEEL_DELTA di Windows. Si ridichiara qui invece di includere windows.h
 * perche' includerlo renderebbe questo file non compilabile da solo, cioe'
 * non provabile senza VM -- che e' l'unica ragione per cui il file esiste.
 * Il valore e' fissato nella API di Windows dal 1995 e non cambia. */
#define WINQ_ROTELLA_DELTA 120

int winq_rotella_scatti(int delta, int *resto)
{
    int totale;
    int scatti;

    if (!resto) {
        return 0;
    }
    totale = *resto + delta;
    /* La divisione fra interi in C tronca VERSO ZERO, e qui e' voluto: -300 da'
     * -2 scatti e resto -60, simmetrico al caso positivo. Con un arrotondamento
     * verso il basso (-3) si emetterebbe uno scatto che l'utente non ha fatto. */
    scatti = totale / WINQ_ROTELLA_DELTA;
    *resto = totale - scatti * WINQ_ROTELLA_DELTA;
    return scatti;
}
