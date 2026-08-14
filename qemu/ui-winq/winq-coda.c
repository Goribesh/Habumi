/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-coda.c -- l'anello fra il thread della finestra e il ciclo principale.
 *
 * Vedi winq-coda.h per il perche' di questo modulo e per la regola che fa
 * rispettare. Qui c'e' solo il come.
 *
 * UN LUCCHETTO E NON UN ANELLO SENZA LUCCHETTI. Con un produttore e un
 * consumatore soli si potrebbe fare senza, con due indici atomici e una coppia
 * acquire/release. Non si fa, e non per pigrizia: un anello senza lucchetti e'
 * corretto solo se le barriere sono giuste, e una barriera sbagliata non da'
 * nessun sintomo su questa macchina e poi ne da' uno irriproducibile sulla
 * prossima -- cioe' esattamente la classe di guasto che costa di piu' in questo
 * progetto. La sezione critica qui dentro e' la copia di una cinquantina di
 * byte, presa una volta per evento di input: il costo sta sotto il rumore del
 * percorso che quell'evento attraversa comunque dentro virtio.
 *
 * CRITICAL_SECTION e non QemuMutex, perche' questo file non include QEMU: e' la
 * proprieta' che test-coda.sh rende eseguibile. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "winq-coda.h"

static CRITICAL_SECTION coda_lucchetto;
static bool coda_pronta;

/* L'anello.
 *
 * "quanti" e non due indici liberi: con la sola testa e la sola coda, PIENO e
 * VUOTO sono lo stesso stato, e distinguerli costa o uno slot sprecato o un
 * contatore. Il contatore si legge senza doverci pensare, e qui la differenza
 * fra pieno e vuoto e' la differenza fra scartare un evento e non scartarlo. */
static WinqRec coda_anello[WINQ_CODA_QUANTI];
static int coda_testa;      /* il prossimo da consegnare */
static int coda_quanti;     /* quanti ce ne sono adesso */
static unsigned long coda_scartati;

void winq_coda_init(void)
{
    InitializeCriticalSection(&coda_lucchetto);
    coda_testa = 0;
    coda_quanti = 0;
    coda_scartati = 0;
    coda_pronta = true;
}

bool winq_coda_metti(const WinqRec *r)
{
    bool esito;

    /* Prima di winq_coda_init il lucchetto non esiste, e prenderlo sarebbe un
     * accesso a memoria non inizializzata. Si rifiuta invece di rischiare: un
     * evento perso prima che la finestra esista non e' un evento. */
    if (!coda_pronta) {
        return false;
    }
    EnterCriticalSection(&coda_lucchetto);
    if (coda_quanti >= WINQ_CODA_QUANTI) {
        esito = false;
    } else {
        int posto = (coda_testa + coda_quanti) % WINQ_CODA_QUANTI;

        coda_anello[posto] = *r;
        coda_quanti++;
        esito = true;
    }
    LeaveCriticalSection(&coda_lucchetto);
    return esito;
}

bool winq_coda_prendi(WinqRec *r)
{
    bool esito;

    if (!coda_pronta) {
        return false;
    }
    EnterCriticalSection(&coda_lucchetto);
    if (!coda_quanti) {
        /* *r NON si tocca: il chiamante puo' averci qualcosa, e azzerarlo qui
         * sarebbe un effetto collaterale che nessuna firma dichiara. */
        esito = false;
    } else {
        *r = coda_anello[coda_testa];
        coda_testa = (coda_testa + 1) % WINQ_CODA_QUANTI;
        coda_quanti--;
        esito = true;
    }
    LeaveCriticalSection(&coda_lucchetto);
    return esito;
}

void winq_coda_conta_scarto(void)
{
    if (!coda_pronta) {
        return;
    }
    EnterCriticalSection(&coda_lucchetto);
    coda_scartati++;
    LeaveCriticalSection(&coda_lucchetto);
}

unsigned long winq_coda_scartati(void)
{
    unsigned long n;

    if (!coda_pronta) {
        return 0;
    }
    EnterCriticalSection(&coda_lucchetto);
    n = coda_scartati;
    LeaveCriticalSection(&coda_lucchetto);
    return n;
}
