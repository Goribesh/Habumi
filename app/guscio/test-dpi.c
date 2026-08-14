/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-dpi.c -- prove delle sole parti PURE di dpi.c.
 *
 * Cosa NON si prova qui, e va detto: SetProcessDpiAwarenessContext e
 * GetDpiForWindow non si provano in un programma di console, perche' la prima
 * ha effetto una volta per processo e la seconda vuole una finestra vera. Si
 * verificano dall'esterno con qemu/scripts/verifica-dpi.ps1, sul prodotto in
 * esecuzione. Qui c'e' l'aritmetica, che e' la parte che si puo' sbagliare in
 * silenzio.
 *
 * Contatore invece di assert(): con assert() il primo fallimento interrompe il
 * programma e non si sa quali altri casi sarebbero passati. Stessa forma di
 * test-registro.c e di qemu/ui-winq/test-winq-coord.c. */
#include <stdio.h>
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

static void a_96_dpi_la_scala_non_cambia_niente(void)
{
    /* 96 e' il DPI di riferimento di Win32: a quella densita' un pixel logico
     * E' un pixel fisico, quindi ogni costante di layout deve uscire identica.
     * Se questa prova fallisce, ogni finestra su uno schermo non scalato si
     * sposta -- il guasto piu' visibile possibile. */
    CHECK(dpi_scala(100, 96) == 100);
    CHECK(dpi_scala(1, 96) == 1);
    CHECK(dpi_scala(0, 96) == 0);
}

static void a_192_dpi_tutto_raddoppia(void)
{
    /* 192 e' la densita' misurata su questa macchina (scala 200%). */
    CHECK(dpi_scala(96, 192) == 192);
    CHECK(dpi_scala(28, 192) == 56);
    CHECK(dpi_scala(100, 192) == 200);
}

static void le_scale_non_intere_arrotondano_al_piu_vicino(void)
{
    /* 144 e' 150%, la scala piu' comune dopo 100 e 200, ed e' quella che
     * scopre un troncamento: 28 * 1,5 fa 42 esatti, ma 12 * 1,5 fa 18 e
     * 100 * 1,5 fa 150 -- con un arrotondamento sbagliato uscirebbero 17 e
     * 149, e un pixel perso per controllo diventa una colonna storta. */
    CHECK(dpi_scala(28, 144) == 42);
    CHECK(dpi_scala(12, 144) == 18);
    CHECK(dpi_scala(100, 144) == 150);
}

static void un_dpi_impossibile_non_azzera_il_layout(void)
{
    /* GetDpiForWindow ritorna 0 quando l'handle non e' valido, e su un Windows
     * dove la funzione non esiste dpi_di_finestra restituisce 96 da se'. Se un
     * 96 mancasse e arrivasse uno 0 fino a qui, senza questa guardia ogni
     * controllo finirebbe di dimensione ZERO: una finestra vuota, che somiglia
     * a un guasto del disegno e non a una divisione. */
    CHECK(dpi_scala(100, 0) == 100);
}

static void spenta_la_scala_guest_non_tocca_la_risoluzione(void)
{
    Config c;

    config_default(&c);
    dpi_applica_scala_guest(&c, 192);
    CHECK(c.larghezza == 1280);
    CHECK(c.altezza == 800);
}

static void accesa_la_scala_guest_moltiplica_per_la_scala(void)
{
    Config c;

    config_default(&c);
    c.scala_guest = true;
    dpi_applica_scala_guest(&c, 192);
    CHECK(c.larghezza == 2560);
    CHECK(c.altezza == 1600);

    /* A 96 DPI non c'e' niente da moltiplicare: la chiave accesa su uno schermo
     * non scalato deve essere un non-evento, non un raddoppio per abitudine. */
    config_default(&c);
    c.scala_guest = true;
    dpi_applica_scala_guest(&c, 96);
    CHECK(c.larghezza == 1280);
    CHECK(c.altezza == 800);

    config_default(&c);
    c.scala_guest = true;
    dpi_applica_scala_guest(&c, 144);
    CHECK(c.larghezza == 1920);
    CHECK(c.altezza == 1200);

    /* IL CONFINE ESATTO, non un valore centrale: 4096 a 192 DPI fa 8192, che e'
     * il massimo che config_carica accetta e che virtio-gpu regge. Deve passare.
     * E' la lezione che questo progetto ha imparato con porta_adb=5555, un
     * valore centrale che avrebbe passato la prova anche con un confine
     * sbagliato di uno. */
    config_default(&c);
    c.scala_guest = true;
    c.larghezza = 4096;
    c.altezza = 2048;
    dpi_applica_scala_guest(&c, 192);
    CHECK(c.larghezza == 8192);
    CHECK(c.altezza == 4096);
}

static void oltre_il_tetto_la_scala_guest_si_rifiuta_intera(void)
{
    Config c;

    /* SI RIFIUTA INTERA, non si limita a 8192: limitare cambierebbe le
     * PROPORZIONI del guest -- 8192x1600 invece di 16000x1600 -- e Android
     * disegnerebbe in un rapporto che non e' quello dello schermo, che e' un
     * guasto peggiore e piu' difficile da riconoscere di "la chiave non ha fatto
     * niente, e il registro dice perche'". */
    config_default(&c);
    c.scala_guest = true;
    c.larghezza = 8000;
    dpi_applica_scala_guest(&c, 192);
    CHECK(c.larghezza == 8000);
    CHECK(c.altezza == 800);
}

static void il_framebuffer_segue_il_VERSO_non_lo_scambio(void)
{
    int bw = 0, bh = 0;

    /* IL DIFETTO CHE QUESTA PROVA CHIUDE, misurato leggendo il
     * registro del prodotto: il bersaglio era "scambia cio' che dice wm size", e
     * wm size in questo QEMU NON CAMBIA MAI. Quindi tre pressioni davano tre
     * volte lo stesso bersaglio --
     *     rotazione: verso il verticale   (framebuffer 2560x1600 -> 1600x2560)
     *     rotazione: verso il orizzontale (framebuffer 2560x1600 -> 1600x2560)
     * -- mentre il verso alternava correttamente. Risultato: dopo la prima
     * pressione il framebuffer restava verticale per sempre, e quando Android
     * tornava orizzontale il contenuto finiva scalato dentro le bande. */
    dpi_ruota(2560, 1600, false, &bw, &bh);
    CHECK(bw == 1600 && bh == 2560);   /* verso il verticale: lato corto in larghezza */
    dpi_ruota(2560, 1600, true, &bw, &bh);
    CHECK(bw == 2560 && bh == 1600);   /* verso l'orizzontale: lato lungo in larghezza */

    /* E DEVE DARE LO STESSO RISULTATO se la geometria letta e' GIA' verticale:
     * il bersaglio dipende dal verso, non da come e' girato adesso. Senza questo
     * caso, un ritorno allo scambio passerebbe con le due prove di sopra. */
    dpi_ruota(1600, 2560, false, &bw, &bh);
    CHECK(bw == 1600 && bh == 2560);
    dpi_ruota(1600, 2560, true, &bw, &bh);
    CHECK(bw == 2560 && bh == 1600);
}

static void una_geometria_assurda_non_produce_un_bersaglio(void)
{
    int bw = 7, bh = 7;

    /* Zero o negativo: si azzera il bersaglio invece di mandare a QEMU una
     * risoluzione che rifiuterebbe (o peggio, accetterebbe). */
    dpi_ruota(0, 1600, false, &bw, &bh);
    CHECK(bw == 0 && bh == 0);
    dpi_ruota(2560, -1, true, &bw, &bh);
    CHECK(bw == 0 && bh == 0);
    /* Uno schermo quadrato non ha un verso: il bersaglio e' lo stesso in
     * entrambi i casi, e non e' un errore. */
    dpi_ruota(1000, 1000, false, &bw, &bh);
    CHECK(bw == 1000 && bh == 1000);
    dpi_ruota(1000, 1000, true, &bw, &bh);
    CHECK(bw == 1000 && bh == 1000);
    /* Puntatori nulli: non si scrive da nessuna parte e non si esplode. */
    dpi_ruota(2560, 1600, true, NULL, &bh);
    dpi_ruota(2560, 1600, true, &bw, NULL);
}

int main(void)
{
    a_96_dpi_la_scala_non_cambia_niente();
    a_192_dpi_tutto_raddoppia();
    le_scale_non_intere_arrotondano_al_piu_vicino();
    un_dpi_impossibile_non_azzera_il_layout();
    spenta_la_scala_guest_non_tocca_la_risoluzione();
    accesa_la_scala_guest_moltiplica_per_la_scala();
    oltre_il_tetto_la_scala_guest_si_rifiuta_intera();
    il_framebuffer_segue_il_VERSO_non_lo_scambio();
    una_geometria_assurda_non_produce_un_bersaglio();

    printf("test-dpi: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
