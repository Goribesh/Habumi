/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-dati.c -- la parte provabile della creazione del /data.
 *
 * Estrarre davvero vuole tar.exe, un archivio vero e il disco: quello si
 * verifica a mano (vedi il rapporto del task). Qui si prova cio' che si
 * sbaglia in silenzio senza toccare niente: che comando si costruisce, dove
 * nasce la cartella temporanea, e quali errori di uno spostamento fallito
 * vale la pena riprovare. */
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

static void costruisce_il_comando_di_estrazione(void)
{
    char cmd[512];

    CHECK(dati_comando_estrai(cmd, sizeof(cmd), "runtime\\empty-data.zip",
                              "C:\\Temp\\HabumiDati") > 0);
    CHECK(strcmp(cmd, "tar.exe -xf \"runtime\\empty-data.zip\" -C "
                      "\"C:\\Temp\\HabumiDati\"") == 0);
}

static void le_virgolette_proteggono_gli_spazi(void)
{
    char cmd[512];

    /* Sia l'archivio sia la cartella temporanea (dentro AppData) possono
     * avere spazi nel percorso: senza virgolette lo spazio spezzerebbe la
     * riga in argomenti diversi e tar.exe cercherebbe un file che non
     * esiste. */
    CHECK(dati_comando_estrai(cmd, sizeof(cmd), "C:\\con spazi\\dati.zip",
                              "C:\\Users\\nome cognome\\Temp") > 0);
    CHECK(strstr(cmd, "\"C:\\con spazi\\dati.zip\"") != NULL);
    CHECK(strstr(cmd, "\"C:\\Users\\nome cognome\\Temp\"") != NULL);
}

static void un_comando_troncato_non_si_costruisce(void)
{
    char corto[16];

    /* Un comando troncato non e' un comando mancante: e' un comando DIVERSO,
     * ed estrarrebbe altrove o niente. */
    CHECK(dati_comando_estrai(corto, sizeof(corto), "runtime\\empty-data.zip",
                              "C:\\Temp\\HabumiDati") == 0);
    CHECK(corto[0] == '\0');
    CHECK(dati_comando_estrai(NULL, 16, "a.zip", "C:\\t") == 0);
    CHECK(dati_comando_estrai(corto, 0, "a.zip", "C:\\t") == 0);
    CHECK(dati_comando_estrai(corto, sizeof(corto), NULL, "C:\\t") == 0);
    CHECK(dati_comando_estrai(corto, sizeof(corto), "", "C:\\t") == 0);
    CHECK(dati_comando_estrai(corto, sizeof(corto), "a.zip", NULL) == 0);
    CHECK(dati_comando_estrai(corto, sizeof(corto), "a.zip", "") == 0);
}

/* La cartella temporanea nasce SULLO STESSO VOLUME della destinazione: se
 * MoveFileA dovesse attraversare due volumi diversi tornerebbe una copia di
 * 4 GiB invece di una rinomina, e un crash a meta' lascerebbe un data.img
 * troncato creduto buono per sempre (vedi il commento su dati_cartella_temp
 * in guscio.h). Qui si verifica che il prefisso di cartella coincida con
 * quello di percorso_dati per costruzione, sia con '\' sia con '/', e che il
 * nome scelto non collida con nessuno dei file che vivono davvero in quella
 * cartella (la tabella delle varianti, vedi varianti.c). */
static void la_cartella_temp_sta_sullo_stesso_volume(void)
{
    char cmd[MAX_PATH];

    CHECK(dati_cartella_temp(cmd, sizeof(cmd),
                             "C:\\Prodotto\\guest\\images\\android\\data.img")
          > 0);
    CHECK(strcmp(cmd, "C:\\Prodotto\\guest\\images\\android\\"
                      "HabumiDatiTmp") == 0);

    /* var_percorsi (varianti.c) scrive i suoi percorsi con '/', non '\': la
     * stessa cartella deve uscirne anche da questo stile. */
    CHECK(dati_cartella_temp(cmd, sizeof(cmd),
                             "guest/images/android/data.img") > 0);
    CHECK(strcmp(cmd, "guest/images/android/HabumiDatiTmp") == 0);
    /* Non collide con nessuno dei quattro file della tabella di varianti.c,
     * ne' con la destinazione stessa. */
    CHECK(strcmp(cmd, "guest/images/android/data.img") != 0);
    CHECK(strcmp(cmd, "guest/images/android/data-gapps.img") != 0);
    CHECK(strcmp(cmd, "guest/images/android/system.img") != 0);
    CHECK(strcmp(cmd, "guest/images/android/system-gapps.img") != 0);

    /* Nessun separatore: percorso_dati e' gia' nella cartella corrente. */
    CHECK(dati_cartella_temp(cmd, sizeof(cmd), "data.img") > 0);
    CHECK(strcmp(cmd, "HabumiDatiTmp") == 0);

    /* Un percorso troncato e' un percorso DIVERSO, non un percorso mancante:
     * stesso principio di dati_comando_estrai. */
    {
        char corto[8];

        CHECK(dati_cartella_temp(corto, sizeof(corto),
                                 "guest/images/android/data.img") == 0);
        CHECK(corto[0] == '\0');
    }
    CHECK(dati_cartella_temp(NULL, sizeof(cmd), "data.img") == 0);
    CHECK(dati_cartella_temp(cmd, 0, "data.img") == 0);
    CHECK(dati_cartella_temp(cmd, sizeof(cmd), NULL) == 0);
    CHECK(dati_cartella_temp(cmd, sizeof(cmd), "") == 0);
}

/* Solo i due errori misurati come TRANSITORI (vedi il commento sul prodotto
 * vivo in guscio.h, sopra dati_sposta_con_riprova) fanno riprovare uno
 * spostamento: ogni altro errore Win32 non migliora aspettando, e riprovarlo
 * sprecherebbe fino a sei secondi su un guasto permanente. E' la sola
 * decisione dello schema di riprova che si possa sbagliare in silenzio senza
 * un test: il resto (MoveFileExA vera, Sleep vero) si verifica a mano. */
static void solo_i_due_errori_transitori_fanno_riprovare(void)
{
    CHECK(dati_sposta_errore_e_transitorio(ERROR_ACCESS_DENIED));
    CHECK(dati_sposta_errore_e_transitorio(ERROR_SHARING_VIOLATION));
    CHECK(!dati_sposta_errore_e_transitorio(ERROR_FILE_NOT_FOUND));
    CHECK(!dati_sposta_errore_e_transitorio(ERROR_ALREADY_EXISTS));
    CHECK(!dati_sposta_errore_e_transitorio(ERROR_DISK_FULL));
    CHECK(!dati_sposta_errore_e_transitorio(0));
}

int main(void)
{
    costruisce_il_comando_di_estrazione();
    le_virgolette_proteggono_gli_spazi();
    un_comando_troncato_non_si_costruisce();
    la_cartella_temp_sta_sullo_stesso_volume();
    solo_i_due_errori_transitori_fanno_riprovare();

    printf("test-dati: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
