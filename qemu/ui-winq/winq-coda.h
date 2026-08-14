/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-coda.h -- la coda fra il thread della finestra e il ciclo principale.
 *
 * PERCHE' ESISTE. La pompa dei messaggi viveva dentro un timer del ciclo
 * principale di QEMU, quindi un ciclo modale di Windows -- tenere il bordo,
 * spostare la finestra -- fermava il ciclo principale e con lui la macchina
 * virtuale: MISURATO, un buco di 23.848 ms tenendo il bordo
 * contro zero buchi a riposo. La finestra e' passata su un thread proprio, e
 * questo modulo e' il confine fra i due.
 *
 * LA REGOLA CHE QUESTO MODULO FA RISPETTARE: il wndproc non chiama NIENTE di
 * QEMU. Traduce ogni messaggio in un record e lo accoda; il ciclo principale
 * drena e chiama. Un wndproc che prendesse il BQL e poi entrasse in
 * DefWindowProc ricostruirebbe il guasto, in silenzio.
 *
 * NIENTE QEMU QUI DENTRO, ed e' verificabile invece che promesso:
 * qemu/scripts/test-coda.sh compila winq-coda.c DA SOLO, senza l'albero di
 * QEMU e senza nessun altro sorgente di winq. Se un giorno questo modulo
 * cominciasse a chiamare QEMU, quella prova non compilerebbe piu'.
 *
 * windows.h invece si include, per la CRITICAL_SECTION. Il confine che conta e'
 * QEMU: tenere Win32 dentro fa si' che la prova eserciti LO STESSO codice del
 * prodotto, invece di una variante compilata apposta per essere provata --
 * che e' un modo di provare qualcos'altro.
 */
#ifndef WINQ_CODA_H
#define WINQ_CODA_H

#include <stdbool.h>
#include <stdint.h>

/* Quanti record ci stanno.
 *
 * Il digitalizzatore produce a 120 Hz, cioe' un evento ogni 8,3 ms: 256 record
 * sono circa DUE SECONDI di input continuo, mentre il consumatore drena a ogni
 * sveglia e comunque a ogni battito da 5 ms. E' un dimensionamento, non una
 * misura, e sta in un posto solo perche' cambiarlo resti una riga. */
#define WINQ_CODA_QUANTI 256

/* I tipi di record. Coprono tutto cio' che il wndproc faceva da se'. */
enum {
    WINQ_REC_PUNTATORE = 1,
    WINQ_REC_TASTO,
    WINQ_REC_ROTELLA,
    WINQ_REC_DIMENSIONE,
    WINQ_REC_ESPONI,
    WINQ_REC_CHIUSURA
};

/* Il tipo di evento del puntatore.
 *
 * I tre numeri sono ricopiati da InputMultiTouchType di QEMU invece di
 * includere l'enum, perche' includerlo porterebbe dentro QEMU e ucciderebbe la
 * proprieta' scritta in cima a questo file. Il rischio -- che un giorno QEMU li
 * rinumeri e nessuno se ne accorga -- e' chiuso in winq-ciclo.c da tre
 * QEMU_BUILD_BUG_ON: se divergono, la compilazione si ferma. */
enum {
    WINQ_TOCCO_BEGIN  = 0,
    WINQ_TOCCO_UPDATE = 1,
    WINQ_TOCCO_END    = 2
};

/* Un record.
 *
 * PIATTO E NON UNION, di proposito: sono una cinquantina di byte, la coda ne
 * tiene 256, e una union vorrebbe una disciplina sul campo discriminante che
 * qui non comprerebbe niente. Con un record piatto un campo letto per il tipo
 * sbagliato vale zero, non spazzatura. */
typedef struct WinqRec {
    int32_t  tipo;        /* WINQ_REC_* */

    /* WINQ_REC_PUNTATORE */
    uint32_t id;          /* l'identificativo di Windows, o WINQ_MOUSE_ID */
    int32_t  x, y;        /* coordinate dell'area CLIENTE */
    int32_t  tocco;       /* WINQ_TOCCO_* */
    bool     pos_valida;  /* false: il consumatore riusa l'ultima posizione nota */

    /* WINQ_REC_TASTO */
    uint64_t vk;          /* wParam */
    int64_t  info;        /* lParam */
    bool     premuto;

    /* WINQ_REC_ROTELLA */
    int32_t  delta;

    /* WINQ_REC_DIMENSIONE */
    int32_t  w, h;

    /* WINQ_REC_CHIUSURA */
    bool     guscio_ha_risposto;
    bool     inatteso;    /* il ciclo dei messaggi e' finito da solo: guasto */
} WinqRec;

/* Da chiamare una volta, prima di qualunque altra: inizializza il lucchetto. */
void winq_coda_init(void);

/* Il produttore, cioe' il thread della finestra. Ritorna false se la coda e'
 * PIENA: la politica di attesa NON e' qui, sta nel chiamante, perche'
 * aspettare e' una decisione di prodotto e questa e' una struttura dati. */
bool winq_coda_metti(const WinqRec *r);

/* Il consumatore, cioe' il ciclo principale. Ritorna false quando non c'e'
 * piu' niente, e in quel caso NON tocca *r. */
bool winq_coda_prendi(WinqRec *r);

/* Quanti record il produttore ha buttato perche' la coda era piena e la sua
 * attesa e' scaduta. Contati e non taciuti: uno scarto silenzioso e' il modo
 * in cui questo codice si e' gia' fatto male piu' volte. */
void winq_coda_conta_scarto(void);
unsigned long winq_coda_scartati(void);

#endif
