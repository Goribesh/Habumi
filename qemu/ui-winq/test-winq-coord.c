/* test-winq-coord.c — si compila da solo, senza QEMU.
 * Verifica i tre casi che contano: proporzioni identiche, finestra piu' larga
 * (bande verticali), finestra piu' alta (bande orizzontali), piu' i bordi.
 *
 * Contatore invece di assert(): con assert() il primo fallimento interrompe
 * il programma e non si sa quanti degli altri casi sarebbero passati, utile
 * proprio qui dove un errore di mappatura non da' un crash ma un tocco nel
 * posto sbagliato — si vuole vedere TUTTI i casi rotti in un colpo solo, non
 * fermarsi al primo. In piu' assert() sparisce silenziosamente se qualcuno
 * compila con -DNDEBUG; CHECK() non dipende da quella macro. I valori e le
 * condizioni sono identici a quelli del brief, solo la forma cambia. */
#include <stdio.h>
#include "winq-coord.h"

static int totali = 0;
static int fallimenti = 0;

#define CHECK(expr) do { \
    totali++; \
    if (!(expr)) { \
        fallimenti++; \
        fprintf(stderr, "FALLITO %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

/* Confronto in virgola mobile con tolleranza: i valori attesi qui sono tutti
 * esatti in binario (interi e meta' di interi), ma un confronto con == su
 * double invita a rompersi al primo cambio di formula anche quando il
 * risultato e' giusto. 1/1000 di pixel e' ben sotto qualunque cosa conti. */
static int quasi(double a, double b) {
    double d = a - b;
    return (d < 0 ? -d : d) < 0.001;
}

/* winq_guest_rect ha casi propri, e non solo di rimbalzo attraverso
 * winq_win_to_guest, per una ragione precisa: da winq-present.c la presentazione
 * la chiama DIRETTAMENTE per costruire il viewport GL. Se restasse coperta solo
 * di riflesso, un errore nel rettangolo (per esempio bande calcolate su un lato
 * solo) passerebbe i test del tocco -- che guarda solo il verdetto
 * dentro/fuori -- e si vedrebbe unicamente come immagine spostata nella
 * finestra, cioe' come "problema di GL". */
static void rettangolo_utile(void) {
    double x, y, w, h;

    /* Proporzioni identiche: nessuna banda, il guest riempie la finestra. */
    WinqGeom uguali = { .win_w = 1280, .win_h = 800, .guest_w = 1280, .guest_h = 800 };
    CHECK(winq_guest_rect(&uguali, &x, &y, &w, &h)
          && quasi(x, 0) && quasi(y, 0) && quasi(w, 1280) && quasi(h, 800));

    /* Finestra piu' larga: bande verticali di 160 px, SIMMETRICHE. E' la
     * simmetria che permette a winq-present.c di passare la stessa y a
     * glViewport (origine in basso) e alla finestra (origine in alto). */
    WinqGeom larga = { .win_w = 1600, .win_h = 800, .guest_w = 1280, .guest_h = 800 };
    CHECK(winq_guest_rect(&larga, &x, &y, &w, &h)
          && quasi(x, 160) && quasi(y, 0) && quasi(w, 1280) && quasi(h, 800));
    CHECK(quasi(x, larga.win_w - (x + w)));      /* banda sinistra == destra */

    /* Finestra piu' alta: bande orizzontali di 100 px, simmetriche. */
    WinqGeom alta = { .win_w = 1280, .win_h = 1000, .guest_w = 1280, .guest_h = 800 };
    CHECK(winq_guest_rect(&alta, &x, &y, &w, &h)
          && quasi(x, 0) && quasi(y, 100) && quasi(w, 1280) && quasi(h, 800));
    CHECK(quasi(y, alta.win_h - (y + h)));       /* banda sopra == sotto */

    /* Rimpicciolita a meta': scala 0.5, nessuna banda. */
    WinqGeom meta = { .win_w = 640, .win_h = 400, .guest_w = 1280, .guest_h = 800 };
    CHECK(winq_guest_rect(&meta, &x, &y, &w, &h)
          && quasi(x, 0) && quasi(y, 0) && quasi(w, 640) && quasi(h, 400));

    /* Casi degeneri: false e nulla toccato. Conta perche' winq-present.c
     * distingue proprio su questo se ha un rettangolo usabile o deve ripiegare
     * su tutta la finestra. */
    double sentinella = -12345;
    x = y = w = h = sentinella;
    WinqGeom minimizzata = { 0, 0, 1280, 800 };
    CHECK(!winq_guest_rect(&minimizzata, &x, &y, &w, &h));
    CHECK(quasi(x, sentinella) && quasi(y, sentinella)
          && quasi(w, sentinella) && quasi(h, sentinella));
    WinqGeom senza_guest = { 1280, 800, 0, 0 };
    CHECK(!winq_guest_rect(&senza_guest, &x, &y, &w, &h));
}

/* L'invariante che tiene insieme i due chiamanti: l'angolo del rettangolo che
 * la presentazione disegna deve essere lo stesso punto che il tocco mappa a
 * (0,0) del guest, e un pixel prima deve cadere nella banda. Se un giorno
 * qualcuno cambiasse una delle due funzioni senza l'altra, e' questo caso a
 * rompersi -- non il disegno in silenzio. */
static void disegno_e_tocco_concordano(void) {
    WinqGeom g = { .win_w = 1600, .win_h = 1000, .guest_w = 1280, .guest_h = 800 };
    double x, y, w, h;
    int gx, gy;

    CHECK(winq_guest_rect(&g, &x, &y, &w, &h));
    CHECK(winq_win_to_guest(&g, (int)x, (int)y, &gx, &gy) && gx == 0 && gy == 0);
    CHECK(!winq_win_to_guest(&g, (int)x - 1, (int)y, &gx, &gy));
    CHECK(winq_win_to_guest(&g, (int)(x + w) - 1, (int)(y + h) - 1, &gx, &gy));
    CHECK(!winq_win_to_guest(&g, (int)(x + w), (int)(y + h), &gx, &gy));
}

static void proporzioni_identiche(void) {
    WinqGeom g = { .win_w = 1280, .win_h = 800, .guest_w = 1280, .guest_h = 800 };
    int x, y;
    CHECK(winq_win_to_guest(&g, 0, 0, &x, &y) && x == 0 && y == 0);
    CHECK(winq_win_to_guest(&g, 640, 400, &x, &y) && x == 640 && y == 400);
    CHECK(winq_win_to_guest(&g, 1279, 799, &x, &y) && x == 1279 && y == 799);
}

static void finestra_piu_larga(void) {
    /* 1600x800 per un guest 1280x800: scala 1.0, bande di 160 px ai lati. */
    WinqGeom g = { .win_w = 1600, .win_h = 800, .guest_w = 1280, .guest_h = 800 };
    int x, y;
    CHECK(!winq_win_to_guest(&g, 10, 400, &x, &y));      /* banda sinistra */
    CHECK(!winq_win_to_guest(&g, 1590, 400, &x, &y));    /* banda destra */
    CHECK(winq_win_to_guest(&g, 160, 0, &x, &y) && x == 0 && y == 0);
    CHECK(winq_win_to_guest(&g, 800, 400, &x, &y) && x == 640 && y == 400);
}

static void finestra_piu_alta(void) {
    /* 1280x1000: scala 1.0, bande di 100 px sopra e sotto. */
    WinqGeom g = { .win_w = 1280, .win_h = 1000, .guest_w = 1280, .guest_h = 800 };
    int x, y;
    CHECK(!winq_win_to_guest(&g, 640, 50, &x, &y));
    CHECK(!winq_win_to_guest(&g, 640, 950, &x, &y));
    CHECK(winq_win_to_guest(&g, 640, 100, &x, &y) && x == 640 && y == 0);
}

static void finestra_ridotta(void) {
    /* Meta' dimensione: scala 0.5, nessuna banda. */
    WinqGeom g = { .win_w = 640, .win_h = 400, .guest_w = 1280, .guest_h = 800 };
    int x, y;
    CHECK(winq_win_to_guest(&g, 320, 200, &x, &y) && x == 640 && y == 400);
}

static void casi_degeneri(void) {
    WinqGeom zero = { 0, 0, 1280, 800 };
    int x, y;
    CHECK(!winq_win_to_guest(&zero, 0, 0, &x, &y));   /* finestra minimizzata */
    WinqGeom senza_guest = { 1280, 800, 0, 0 };
    CHECK(!winq_win_to_guest(&senza_guest, 10, 10, &x, &y));
}

int main(void) {
    proporzioni_identiche();
    finestra_piu_larga();
    finestra_piu_alta();
    finestra_ridotta();
    casi_degeneri();
    rettangolo_utile();
    disegno_e_tocco_concordano();

    if (fallimenti > 0) {
        fprintf(stderr, "test-winq-coord: %d/%d falliti\n", fallimenti, totali);
        return 1;
    }
    printf("test-winq-coord: tutti passati (%d/%d)\n", totali, totali);
    return 0;
}
