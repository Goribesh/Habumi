/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-registro.c -- si compila da solo, senza Win32 ne' QEMU.
 *
 * Contatore invece di assert(): con assert() il primo fallimento interrompe il
 * programma e non si sa quali altri casi sarebbero passati. Stessa forma di
 * qemu/ui-winq/test-winq-coord.c. */
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

static void una_riga_esce_col_suo_prefisso(void)
{
    char buf[512];
    int n;

    registro_apri();
    registro_riga(REG_GUSCIO, "ciao %d", 42);
    n = registro_nuove(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "[shell]") != NULL);
    CHECK(strstr(buf, "ciao 42") != NULL);
    CHECK(strstr(buf, "\r\n") != NULL);
    registro_chiudi();
}

static void le_tre_sorgenti_hanno_tre_prefissi(void)
{
    char buf[512];

    registro_apri();
    registro_riga(REG_GUSCIO, "a");
    registro_riga(REG_QEMU, "b");
    registro_riga(REG_GUEST, "c");
    registro_nuove(buf, sizeof(buf));
    CHECK(strstr(buf, "[shell]") != NULL);
    CHECK(strstr(buf, "[qemu]") != NULL);
    CHECK(strstr(buf, "[guest]") != NULL);
    registro_chiudi();
}

static void cio_che_e_stato_consegnato_non_torna(void)
{
    char buf[512];

    registro_apri();
    registro_riga(REG_GUSCIO, "prima");
    CHECK(registro_nuove(buf, sizeof(buf)) > 0);
    /* Senza righe nuove la seconda lettura deve dare zero: la finestra chiama
     * questa funzione su un timer, e se ridesse tutto ogni volta il pannello
     * crescerebbe all'infinito duplicando. */
    CHECK(registro_nuove(buf, sizeof(buf)) == 0);
    registro_riga(REG_GUSCIO, "dopo");
    CHECK(registro_nuove(buf, sizeof(buf)) > 0);
    CHECK(strstr(buf, "dopo") != NULL);
    CHECK(strstr(buf, "prima") == NULL);
    registro_chiudi();
}

static void il_registro_non_sfonda_col_troppo(void)
{
    char buf[256];
    int i, n;

    registro_apri();
    for (i = 0; i < 5000; i++) {
        registro_riga(REG_QEMU, "riga numero %d con un po' di testo per fare volume", i);
    }
    /* Un buffer piccolo deve ritornare cio' che ci sta, senza scrivere oltre,
     * senza perdere il terminatore, e senza consumare cio' che non ci entra. */
    n = registro_nuove(buf, sizeof(buf));
    CHECK(n > 0);                      /* almeno una riga ci sta */
    CHECK(n < (int)sizeof(buf));
    CHECK(buf[n] == '\0');
    /* Le righe che NON ci sono entrate non devono essere consumate: senza
     * questa riga, un registro_nuove che scarta cio' che non riesce a
     * copiare passerebbe la prova perdendo righe in silenzio. */
    CHECK(registro_nuove(buf, sizeof(buf)) > 0);
    registro_chiudi();
}

/* REG_LUNGHEZZA e' 256 in registro.c, ma e' privata la' e non si vede da qui:
 * si usa una costante locale vicina, cosi' la riga occupa quasi tutto lo
 * spazio della riga nell'anello (prefisso compreso) senza dipendere da un
 * dettaglio interno del modulo. */
#define RIGA_LUNGA_TESTO 240

static void una_riga_piu_grande_del_buffer_non_blocca(void)
{
    char lunga[RIGA_LUNGA_TESTO + 1];
    char area[65];   /* 64 per la chiamata, +1 di sentinella oltre max */
    char *buf = area;
    int i, n;

    registro_apri();
    for (i = 0; i < RIGA_LUNGA_TESTO; i++) {
        lunga[i] = 'x';
    }
    lunga[RIGA_LUNGA_TESTO] = '\0';
    registro_riga(REG_GUSCIO, "%s", lunga);

    area[64] = (char)0xAA;
    n = registro_nuove(buf, 64);
    /* Prova di progresso: senza avanzare, questa e le prossime chiamate
     * ritroverebbero la stessa riga e la stessa condizione per sempre --
     * uno stallo silenzioso, non un ritardo. */
    CHECK(n > 0);
    CHECK(n < 64);
    CHECK(buf[n] == '\0');
    CHECK(area[64] == (char)0xAA);           /* nulla scritto oltre max */
    /* Il taglio deve vedersi: una riga tagliata in silenzio e' un'altra
     * bugia. */
    CHECK(n >= 3 && strcmp(buf + n - 3, "...") == 0);

    /* La riga lunga va CONSUMATA anche se troncata: una seconda chiamata deve
     * portare la riga successiva, non ripresentare quella tagliata. */
    registro_riga(REG_GUSCIO, "dopo la riga lunga");
    n = registro_nuove(buf, 64);
    CHECK(n > 0);
    CHECK(strstr(buf, "dopo la riga lunga") != NULL);
    CHECK(strstr(buf, "xxxx") == NULL);
    registro_chiudi();
}

/* IL FILE E' PER QUANDO IL PROCESSO MUORE, e la prova esiste per una ragione
 * misurata: QEMU e' morto durante una prova su Venus, e la ragione
 * era nel suo stderr -- che il guscio raccoglie nel registro, cioe' in un pannello
 * che si e' chiuso insieme al processo. L'informazione era stata catturata e persa.
 *
 * Si prova che la riga sia nel file SUBITO, non a fine sessione: senza lo svuotamento
 * a ogni riga, un crash porta via proprio le ultime righe -- quelle che dicono perche'. */
static void ogni_riga_finisce_anche_nel_file_subito(void)
{
    char buf[512];
    FILE *f;
    long dimensione;

    remove("prova-registro.log");
    /* La variabile esiste per poter provare questo senza scrivere nel percorso
     * vero del prodotto, e come scappatoia per chi voglia il registro altrove. */
    _putenv("GUSCIO_REGISTRO_FILE=prova-registro.log");
    registro_apri();
    registro_riga(REG_QEMU, "morto per la ragione %d", 42);

    /* Mentre il registro e' ANCORA APERTO: e' il caso del crash. */
    f = fopen("prova-registro.log", "rb");
    CHECK(f != NULL);
    if (f) {
        size_t letti = fread(buf, 1, sizeof(buf) - 1, f);
        buf[letti] = '\0';
        fclose(f);
        CHECK(strstr(buf, "[qemu]") != NULL);
        CHECK(strstr(buf, "morto per la ragione 42") != NULL);
    }

    registro_chiudi();
    f = fopen("prova-registro.log", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        dimensione = ftell(f);
        fclose(f);
        CHECK(dimensione > 0);
    }
    remove("prova-registro.log");
    _putenv("GUSCIO_REGISTRO_FILE=");
}

/* Un percorso impossibile NON deve far cadere il registro: il log e' un aiuto, e
 * un aiuto che impedisce di avviare il prodotto e' peggio della sua assenza. */
static void un_file_che_non_si_apre_non_ferma_il_registro(void)
{
    char buf[256];

    _putenv("GUSCIO_REGISTRO_FILE=cartella-che-non-esiste/e/nemmeno/questa.log");
    registro_apri();
    registro_riga(REG_GUSCIO, "il registro funziona comunque");
    CHECK(registro_nuove(buf, sizeof(buf)) > 0);
    CHECK(strstr(buf, "il registro funziona comunque") != NULL);
    registro_chiudi();
    _putenv("GUSCIO_REGISTRO_FILE=");
}

int main(void)
{
    una_riga_esce_col_suo_prefisso();
    le_tre_sorgenti_hanno_tre_prefissi();
    cio_che_e_stato_consegnato_non_torna();
    il_registro_non_sfonda_col_troppo();
    una_riga_piu_grande_del_buffer_non_blocca();
    ogni_riga_finisce_anche_nel_file_subito();
    un_file_che_non_si_apre_non_ferma_il_registro();

    printf("test-registro: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
