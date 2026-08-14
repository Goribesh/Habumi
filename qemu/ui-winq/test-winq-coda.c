/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-winq-coda.c -- le prove della coda fra i due thread.
 *
 * Come test-winq-coord.c: una macro CHECK con contatore invece di assert().
 * assert si ferma al primo fallimento e sparisce sotto NDEBUG, e qui -- dove un
 * errore e' SILENZIOSO -- conviene vedere tutti i casi rotti in un colpo.
 *
 * Nessun include di QEMU, ed e' la proprieta' che questa prova esiste anche per
 * difendere: vedi il commento in cima a winq-coda.h. */
#include <stdio.h>
#include <string.h>
#include "winq-coda.h"

static int passati, falliti;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            passati++;                                                       \
        } else {                                                             \
            falliti++;                                                       \
            printf("FALLITO %s:%d: ", __FILE__, __LINE__);                   \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
        }                                                                    \
    } while (0)

static WinqRec puntatore(uint32_t id, int x, int y, int tocco)
{
    WinqRec r;

    memset(&r, 0, sizeof(r));
    r.tipo = WINQ_REC_PUNTATORE;
    r.id = id;
    r.x = x;
    r.y = y;
    r.tocco = tocco;
    r.pos_valida = true;
    return r;
}

static void svuota(void)
{
    WinqRec r;

    while (winq_coda_prendi(&r)) {
        /* niente: serve solo a lasciare la coda vuota per la prova dopo */
    }
}

/* La coda vuota non consegna niente, e non inventa un record. */
static void prova_vuota(void)
{
    WinqRec r;

    memset(&r, 0, sizeof(r));
    r.tipo = 12345;
    CHECK(!winq_coda_prendi(&r), "una coda vuota ha consegnato qualcosa");
    CHECK(r.tipo == 12345, "prendi ha toccato *r pur non avendo niente da dare");
}

/* L'ORDINE E' L'INVARIANTE PRINCIPALE. Un BEGIN e il suo END devono uscire
 * nell'ordine in cui sono entrati: invertirli lascia Android convinto che un
 * dito sia ancora appoggiato, che e' il difetto piu' costoso di questo dominio
 * -- lo stesso che la compensazione di BTN_TOUCH esiste per prevenire. */
static void prova_ordine(void)
{
    WinqRec r;
    int i;

    svuota();
    for (i = 0; i < 50; i++) {
        int t = (i == 0) ? WINQ_TOCCO_BEGIN
                         : ((i == 49) ? WINQ_TOCCO_END : WINQ_TOCCO_UPDATE);
        WinqRec p = puntatore(7, i, i * 2, t);

        CHECK(winq_coda_metti(&p), "metti ha rifiutato il record %d", i);
    }
    for (i = 0; i < 50; i++) {
        CHECK(winq_coda_prendi(&r), "prendi ha finito presto al record %d", i);
        CHECK(r.x == i && r.y == i * 2, "record %d fuori ordine: x=%d y=%d",
              i, r.x, r.y);
    }
    CHECK(r.tocco == WINQ_TOCCO_END, "l'ultimo consegnato non era l'END");
    CHECK(!winq_coda_prendi(&r), "la coda doveva essere vuota");
}

/* La coda piena RIFIUTA, non sovrascrive: sovrascrivere il piu' vecchio
 * perderebbe proprio il BEGIN di cui l'END sta arrivando. */
static void prova_piena(void)
{
    WinqRec r;
    int i;

    svuota();
    for (i = 0; i < WINQ_CODA_QUANTI; i++) {
        WinqRec p = puntatore(1, i, 0, WINQ_TOCCO_UPDATE);

        CHECK(winq_coda_metti(&p), "metti ha rifiutato al record %d di %d",
              i, WINQ_CODA_QUANTI);
    }
    r = puntatore(1, 999, 0, WINQ_TOCCO_UPDATE);
    CHECK(!winq_coda_metti(&r), "la coda piena ha accettato un record in piu'");

    /* E il piu' vecchio e' ancora il piu' vecchio: nessuna sovrascrittura. */
    CHECK(winq_coda_prendi(&r), "la coda piena non consegna");
    CHECK(r.x == 0, "il piu' vecchio e' stato sovrascritto: x=%d", r.x);
}

/* Svuotata e riempita di nuovo: gli indici girano senza perdere niente. */
static void prova_giro(void)
{
    WinqRec r;
    int i;

    svuota();
    for (i = 0; i < WINQ_CODA_QUANTI * 3; i++) {
        WinqRec p = puntatore(2, i, 0, WINQ_TOCCO_UPDATE);

        CHECK(winq_coda_metti(&p), "giro: metti rifiutato a %d", i);
        CHECK(winq_coda_prendi(&r), "giro: prendi vuoto a %d", i);
        CHECK(r.x == i, "giro: valore sbagliato a %d: %d", i, r.x);
    }
}

/* Ogni tipo di record passa intero, campo per campo. Un campo perso qui
 * diventa un tasto che non arriva o una finestra che non si ridimensiona. */
static void prova_tipi(void)
{
    WinqRec r, u;

    svuota();

    memset(&u, 0, sizeof(u));
    u.tipo = WINQ_REC_TASTO;
    u.vk = 0x41;
    u.info = 0x1e0001;
    u.premuto = true;
    CHECK(winq_coda_metti(&u), "tasto rifiutato");

    memset(&u, 0, sizeof(u));
    u.tipo = WINQ_REC_ROTELLA;
    u.delta = -120;
    CHECK(winq_coda_metti(&u), "rotella rifiutata");

    memset(&u, 0, sizeof(u));
    u.tipo = WINQ_REC_DIMENSIONE;
    u.w = 2560;
    u.h = 1600;
    CHECK(winq_coda_metti(&u), "dimensione rifiutata");

    memset(&u, 0, sizeof(u));
    u.tipo = WINQ_REC_ESPONI;
    CHECK(winq_coda_metti(&u), "esponi rifiutato");

    memset(&u, 0, sizeof(u));
    u.tipo = WINQ_REC_CHIUSURA;
    u.guscio_ha_risposto = true;
    u.inatteso = false;
    CHECK(winq_coda_metti(&u), "chiusura rifiutata");

    CHECK(winq_coda_prendi(&r) && r.tipo == WINQ_REC_TASTO && r.vk == 0x41 &&
          r.info == 0x1e0001 && r.premuto, "il tasto non e' tornato intero");
    CHECK(winq_coda_prendi(&r) && r.tipo == WINQ_REC_ROTELLA && r.delta == -120,
          "la rotella non e' tornata intera");
    CHECK(winq_coda_prendi(&r) && r.tipo == WINQ_REC_DIMENSIONE &&
          r.w == 2560 && r.h == 1600, "la dimensione non e' tornata intera");
    CHECK(winq_coda_prendi(&r) && r.tipo == WINQ_REC_ESPONI,
          "esponi non e' tornato");
    CHECK(winq_coda_prendi(&r) && r.tipo == WINQ_REC_CHIUSURA &&
          r.guscio_ha_risposto && !r.inatteso,
          "la chiusura non e' tornata intera");
}

/* I due booleani della chiusura sono distinti, e devono restarlo: uno dice
 * "l'utente ha chiuso", l'altro "il ciclo dei messaggi e' morto da solo", e il
 * consumatore prende due strade diverse. */
static void prova_chiusura_inattesa(void)
{
    WinqRec r, u;

    svuota();
    memset(&u, 0, sizeof(u));
    u.tipo = WINQ_REC_CHIUSURA;
    u.guscio_ha_risposto = false;
    u.inatteso = true;
    CHECK(winq_coda_metti(&u), "chiusura inattesa rifiutata");
    CHECK(winq_coda_prendi(&r) && r.inatteso && !r.guscio_ha_risposto,
          "i due booleani della chiusura si sono confusi");
}

/* Il flag della posizione non valida sopravvive: e' il ramo con GetPointerInfo
 * fallita, dove il consumatore deve riusare l'ultima posizione nota dello slot.
 * Se si perdesse, un dito si staccherebbe alle coordinate (0,0). */
static void prova_posizione_non_valida(void)
{
    WinqRec r, u;

    svuota();
    u = puntatore(9, 0, 0, WINQ_TOCCO_END);
    u.pos_valida = false;
    CHECK(winq_coda_metti(&u), "record senza posizione rifiutato");
    CHECK(winq_coda_prendi(&r) && !r.pos_valida,
          "pos_valida e' stata persa per strada");
}

static void prova_scartati(void)
{
    unsigned long prima = winq_coda_scartati();

    winq_coda_conta_scarto();
    winq_coda_conta_scarto();
    CHECK(winq_coda_scartati() == prima + 2,
          "il contatore degli scarti non avanza: %lu", winq_coda_scartati());
}

int main(void)
{
    winq_coda_init();

    prova_vuota();
    prova_ordine();
    prova_piena();
    prova_giro();
    prova_tipi();
    prova_chiusura_inattesa();
    prova_posizione_non_valida();
    prova_scartati();

    printf("test-winq-coda: %d su %d passati\n", passati, passati + falliti);
    return falliti ? 1 : 0;
}
