/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-apk.c -- le parti provabili dell'installazione per trascinamento.
 *
 * Installare davvero vuole una VM accesa e un APK: quello lo verifica la prova
 * sul prodotto vivo. Qui si provano le tre decisioni che si possono sbagliare
 * senza accorgersene: se un file e' un APK, che comando si costruisce, e quale
 * riga dell'uscita di adb si mostra all'utente. */
#include <stdio.h>
#include <string.h>
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

static void riconosce_un_apk(void)
{
    CHECK(apk_e_apk("gioco.apk"));
    /* Windows non distingue le maiuscole nei nomi: un GIOCO.APK e' un APK. */
    CHECK(apk_e_apk("GIOCO.APK"));
    CHECK(apk_e_apk("Gioco.Apk"));
    CHECK(apk_e_apk("C:\\cartella con spazi\\app.apk"));
}

static void rifiuta_cio_che_non_lo_e(void)
{
    /* Il caso che frega: l'estensione giusta in mezzo, non in fondo. */
    CHECK(!apk_e_apk("gioco.apk.txt"));
    CHECK(!apk_e_apk("apk"));
    CHECK(!apk_e_apk("gioco.zip"));
    /* Estensione senza nome: non e' un file da installare. */
    CHECK(!apk_e_apk(".apk"));
    CHECK(!apk_e_apk(""));
    CHECK(!apk_e_apk(NULL));
}

static void costruisce_il_comando_di_installazione(void)
{
    char cmd[256];

    CHECK(apk_comando(cmd, sizeof(cmd), "C:\\cartella con spazi\\app.apk") > 0);
    /* -r reinstalla conservando i dati: e' quello che serve a chi ricompila la
     * stessa app venti volte. Le virgolette servono per gli spazi. */
    CHECK(strcmp(cmd, "install -r \"C:\\cartella con spazi\\app.apk\"") == 0);
}

static void un_comando_troncato_non_si_costruisce(void)
{
    char corto[16];

    CHECK(apk_comando(corto, sizeof(corto), "C:\\percorso lungo\\app.apk") == 0);
    CHECK(corto[0] == '\0');
    CHECK(apk_comando(NULL, 16, "a.apk") == 0);
    CHECK(apk_comando(corto, sizeof(corto), NULL) == 0);
}

/* Le prove sull'ultima riga dell'uscita di adb sono state SPOSTATE in
 * test-adb.c insieme alla funzione: leggono come adb scrive, non cos'e' un
 * APK. */

int main(void)
{
    riconosce_un_apk();
    rifiuta_cio_che_non_lo_e();
    costruisce_il_comando_di_installazione();
    un_comando_troncato_non_si_costruisce();

    printf("test-apk: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
