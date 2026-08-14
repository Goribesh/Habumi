/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-file.c -- le decisioni della copia di un file nel guest.
 *
 * Copiare davvero vuole una VM accesa: quello lo verifica la prova sul prodotto
 * vivo. Qui si provano le due cose che si sbagliano in silenzio: quali nomi il
 * canale ANSI trasporta, e che comando si costruisce. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

static int totali = 0;
static int fallimenti = 0;

#define CHECK(expr) do { \
    totali++; \
    if (!(expr)) { \
        fallimenti++; \
        printf("FALLITO %s:%d  %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static void trasporta_i_nomi_che_l_ansi_rappresenta(void)
{
    CHECK(file_nome_trasportabile(L"canzone.mp3"));
    CHECK(file_nome_trasportabile(L"con spazi e (parentesi).pdf"));
    /* Gli accentati passano sulla tabella 1252, ed e' il caso che conta: in
     * italiano un nome di file accentato e' la norma, non un caso limite. */
    CHECK(file_nome_trasportabile(L"citt\u00e0.txt"));
    CHECK(file_nome_trasportabile(L"perch\u00e9 no.txt"));
}

static void rifiuta_i_nomi_che_perderebbe(void)
{
    /* Cirillico: fuori dalla 1252. Senza questo controllo il file arriverebbe
     * nel guest con un nome DIVERSO da quello che l'utente vede, che e' peggio
     * di non arrivare -- nessun errore, e un file che non si ritrova. */
    CHECK(!file_nome_trasportabile(L"\u043f\u0435\u0441\u043d\u044f.mp3"));
    /* Cinese, e un emoji: stesso motivo. */
    CHECK(!file_nome_trasportabile(L"\u6b4c.mp3"));
    CHECK(!file_nome_trasportabile(L"\U0001F3B5.mp3"));
    CHECK(!file_nome_trasportabile(NULL));
}

static void costruisce_il_comando_di_copia(void)
{
    char cmd[512];

    CHECK(file_comando_push(cmd, sizeof(cmd), "C:\\musica\\canzone.mp3") > 0);
    CHECK(strcmp(cmd, "push \"C:\\musica\\canzone.mp3\" "
                      "\"/sdcard/Download/\"") == 0);
    /* Il percorso host va fra virgolette o uno spazio lo spezza in due
     * argomenti e adb copia un file che non esiste. */
    CHECK(file_comando_push(cmd, sizeof(cmd), "C:\\con spazi\\a.txt") > 0);
    CHECK(strstr(cmd, "\"C:\\con spazi\\a.txt\"") != NULL);
}

static void il_bersaglio_finisce_sempre_con_la_barra(void)
{
    char cmd[512];

    /* PROVA NEGATIVA, ed e' quella che conta: senza la barra finale adb crea un
     * FILE di nome Download quando la cartella non esiste, e il file "arriva"
     * con un nome che nessuna app trovera'. Un futuro appiattimento del
     * bersaglio deve far fallire questa prova. */
    CHECK(file_comando_push(cmd, sizeof(cmd), "C:\\a\\b.mp3") > 0);
    CHECK(strstr(cmd, "/sdcard/Download/") != NULL);
    CHECK(strstr(cmd, "/sdcard/Downloadb.mp3") == NULL);
    CHECK(strstr(cmd, "/sdcard/Download\"") == NULL);
}

static void un_comando_troncato_non_si_costruisce(void)
{
    char cmd[16];

    /* Un comando troncato non e' un comando mancante: e' un comando DIVERSO, e
     * copierebbe altrove o niente. */
    CHECK(file_comando_push(cmd, sizeof(cmd), "C:\\molto\\lungo\\davvero.mp3")
          == 0);
    CHECK(cmd[0] == '\0');
    CHECK(file_comando_push(NULL, 10, "C:\\a.mp3") == 0);
    CHECK(file_comando_push(cmd, 0, "C:\\a.mp3") == 0);
    CHECK(file_comando_push(cmd, sizeof(cmd), NULL) == 0);
    CHECK(file_comando_push(cmd, sizeof(cmd), "") == 0);
}

int main(void)
{
    /* Le prove sugli accentati ASSUMONO che questa macchina sia sulla tabella
     * 1252: stamparla rende l'assunzione visibile, invece di lasciare un
     * fallimento incomprensibile a chi esegue su una macchina diversa. */
    printf("test-file: tabella ANSI di questa macchina = %u\n",
           (unsigned)GetACP());
    trasporta_i_nomi_che_l_ansi_rappresenta();
    rifiuta_i_nomi_che_perderebbe();
    costruisce_il_comando_di_copia();
    il_bersaglio_finisce_sempre_con_la_barra();
    un_comando_troncato_non_si_costruisce();

    printf("test-file: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
