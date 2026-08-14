/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-winq-rotella.c -- si compila da solo, senza Windows ne' QEMU.
 *
 * Contatore invece di assert(): con assert() il primo fallimento interrompe il
 * programma e non si sa quali altri casi sarebbero passati. Stessa forma di
 * test-winq-coord.c. */
#include <stdio.h>
#include "winq-rotella.h"

static int totali = 0;
static int fallimenti = 0;

#define CHECK(expr) do { \
    totali++; \
    if (!(expr)) { \
        fallimenti++; \
        printf("FALLITO %s:%d  %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

/* I casi stanno in tabella perche' sono dieci varianti della stessa domanda, e
 * dieci funzioni identiche renderebbero piu' difficile vedere quale manca. La
 * colonna "perche'" esiste per chi legge un fallimento: dice cosa si stava
 * proteggendo, non solo quale numero non torna. */
struct caso {
    int delta;
    int resto_prima;
    int scatti;
    int resto_dopo;
    const char *perche;
};

static void i_casi_della_rotella(void)
{
    static const struct caso casi[] = {
        { 120,   0,  1,   0, "il caso normale del mouse" },
        {  60,   0,  0,  60, "mezzo scatto non deve muovere niente" },
        {  60,  60,  1,   0, "due mezzi fanno uno: e' la ragione del resto" },
        {   8,   0,  0,   8, "il delta minuto vero del trackpad di precisione" },
        {-120,   0, -1,   0, "verso opposto" },
        { 240,   0,  2,   0, "piu' scatti in un solo messaggio" },
        { 300,   0,  2,  60, "il resto sopravvive anche agli scatti multipli" },
        {-300,   0, -2, -60, "la troncatura verso zero vale anche in negativo" },
        { -60,  60,  0,   0, "il cambio di direzione si annulla da se'" },
        {   0,  60,  0,  60, "un delta nullo non deve consumare il resto" },
    };
    size_t i;

    for (i = 0; i < sizeof(casi) / sizeof(casi[0]); i++) {
        int resto = casi[i].resto_prima;
        int scatti = winq_rotella_scatti(casi[i].delta, &resto);

        totali++;
        if (scatti != casi[i].scatti || resto != casi[i].resto_dopo) {
            fallimenti++;
            printf("FALLITO delta=%d resto=%d -> scatti=%d resto=%d, "
                   "attesi scatti=%d resto=%d  (%s)\n",
                   casi[i].delta, casi[i].resto_prima, scatti, resto,
                   casi[i].scatti, casi[i].resto_dopo, casi[i].perche);
        }
    }
}

/* Il contratto dice che resto non deve essere NULL. Se lo e' comunque, non si
 * scrive in memoria altrui e non si finge uno scatto: si torna zero. */
static void un_resto_nullo_non_fa_danno(void)
{
    CHECK(winq_rotella_scatti(120, NULL) == 0);
}

int main(void)
{
    i_casi_della_rotella();
    un_resto_nullo_non_fa_danno();

    printf("test-winq-rotella: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
