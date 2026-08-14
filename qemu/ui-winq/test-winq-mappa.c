/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-winq-mappa.c -- le prove del modulo puro della mappa, senza VM. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "winq-mappa.h"

static int falliti;

#define CHECK(c) do {                                                     \
    if (!(c)) {                                                           \
        printf("FALLITA riga %d: %s\n", __LINE__, #c);                    \
        falliti++;                                                        \
    }                                                                     \
} while (0)

/* Scrive un file temporaneo e ne ritorna il nome. Sempre lo stesso nome:
 * le prove sono sequenziali e non c'e' concorrenza. */
static const char *scrivi(const char *contenuto)
{
    static const char *nome = "test-mappa-tmp.txt";
    FILE *f = fopen(nome, "wb");
    if (!f) {
        printf("FALLITA: non riesco a scrivere %s\n", nome);
        falliti++;
        return nome;
    }
    fputs(contenuto, f);
    fclose(f);
    return nome;
}

static void prova_file_valido(void)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;
    const char *p = scrivi("# commento\n"
                           "\n"
                           "joystick WASD 200 750 120\n"
                           "touch Q 250 700\n"
                           "touch SPAZIO 500 850\n");
    CHECK(mappa_carica(m, p, err, sizeof(err), &riga) == true);
    mappa_distruggi(m);
}

static void prova_solo_commenti(void)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;
    const char *p = scrivi("# niente\n\n# niente\n");
    CHECK(mappa_carica(m, p, err, sizeof(err), &riga) == true);
    mappa_distruggi(m);
}

/* Ogni caso cattivo verifica ANCHE il numero di riga: un lettore che fallisce
 * senza dire dove costringe a cercare a mano in un file di trenta righe. */
static void prova_caso_cattivo(const char *contenuto, int riga_attesa)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;
    const char *p = scrivi(contenuto);
    CHECK(mappa_carica(m, p, err, sizeof(err), &riga) == false);
    CHECK(riga == riga_attesa);
    CHECK(err[0] != '\0');
    mappa_distruggi(m);
}

static void prova_file_inesistente(void)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;
    CHECK(mappa_carica(m, "non-esiste-affatto.txt", err, sizeof(err), &riga) == false);
    CHECK(err[0] != '\0');
    mappa_distruggi(m);
}

/* La mappa usata da tutte le prove della traduzione. */
static Mappa *mappa_di_prova(void)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;
    const char *p = scrivi("joystick WASD 200 750 100\n"
                           "touch Q 250 700\n"
                           "touch E 750 700\n");
    CHECK(mappa_carica(m, p, err, sizeof(err), &riga) == true);
    return m;
}

static void prova_tocco(void)
{
    Mappa *m = mappa_di_prova();
    MappaAzione az[MAPPA_AZIONI_MAX];
    int n;

    n = mappa_tasto(m, 'Q', true, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 1);
    CHECK(az[0].tipo == MAPPA_AZ_BEGIN);
    /* Q e' il tocco in posizione 0 in mappa_di_prova: un indice scambiato
     * con E (posizione 1) farebbe muovere il dito sbagliato nel guest. */
    CHECK(az[0].id == MAPPA_ID_TOCCO(0));
    CHECK(az[0].x_pm == 250 && az[0].y_pm == 700);

    /* La ripetizione automatica non produce niente. */
    n = mappa_tasto(m, 'Q', true, true, az, MAPPA_AZIONI_MAX);
    CHECK(n == 0);

    n = mappa_tasto(m, 'Q', false, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 1);
    CHECK(az[0].tipo == MAPPA_AZ_END);
    CHECK(az[0].id == MAPPA_ID_TOCCO(0));

    /* Un tasto non mappato ritorna zero: il chiamante lo manda al guest. */
    n = mappa_tasto(m, 'Z', true, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 0);

    mappa_distruggi(m);
}

static void prova_joystick(void)
{
    Mappa *m = mappa_di_prova();
    MappaAzione az[MAPPA_AZIONI_MAX];
    int n;

    /* Primo tasto: BEGIN al CENTRO, poi UPDATE alla posizione. Due azioni,
     * perche' diversi giochi accettano lo stick solo se il dito scende dentro
     * la sua area e poi si trascina. */
    n = mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 2);
    CHECK(az[0].tipo == MAPPA_AZ_BEGIN);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);
    CHECK(az[0].x_pm == 200 && az[0].y_pm == 750);
    CHECK(az[1].tipo == MAPPA_AZ_UPDATE);
    CHECK(az[1].id == MAPPA_ID_JOYSTICK);
    CHECK(az[1].x_pm == 200 && az[1].y_pm == 650);   /* su: y - raggio */

    /* Secondo tasto: solo UPDATE, MAI un secondo BEGIN. */
    n = mappa_tasto(m, 'D', true, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 1);
    CHECK(az[0].tipo == MAPPA_AZ_UPDATE);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);
    /* Diagonale normalizzata: 100 * 707 / 1000 = 71, entro +-1 permille. */
    CHECK(az[0].x_pm >= 200 + 70 && az[0].x_pm <= 200 + 72);
    CHECK(az[0].y_pm >= 750 - 72 && az[0].y_pm <= 750 - 70);

    /* Rilasciato uno dei due, l'altro resta: UPDATE, non END. */
    n = mappa_tasto(m, 'W', false, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 1);
    CHECK(az[0].tipo == MAPPA_AZ_UPDATE);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);
    CHECK(az[0].x_pm == 300 && az[0].y_pm == 750);   /* destra: x + raggio */

    /* Ultimo rilasciato: END. */
    n = mappa_tasto(m, 'D', false, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 1);
    CHECK(az[0].tipo == MAPPA_AZ_END);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);

    mappa_distruggi(m);
}

static void prova_joystick_opposti(void)
{
    Mappa *m = mappa_di_prova();
    MappaAzione az[MAPPA_AZIONI_MAX];
    int n;

    mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX);
    /* W+S insieme: direzione nulla, il dito torna al centro e resta giu'. */
    n = mappa_tasto(m, 'S', true, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 1);
    CHECK(az[0].tipo == MAPPA_AZ_UPDATE);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);
    CHECK(az[0].x_pm == 200 && az[0].y_pm == 750);

    mappa_distruggi(m);
}

/* Rilievo 1: nella versione rivista, m->joy_premuto[i] veniva scritto PRIMA
 * dei controlli su az_max. Un az_max insufficiente faceva ritornare zero
 * azioni ma lasciava lo stato interno gia' cambiato: sul ramo BEGIN il
 * tasto successivo del joystick trovava "qualcuno gia' premuto" e produceva
 * un secondo BEGIN spurio; sul ramo END il tasto risultava rilasciato ma
 * joy_giu restava vero, quindi l'END non usciva mai piu' -- il dito
 * incollato nel guest, il difetto peggiore descritto nel rilievo. La prova
 * non ispeziona lo stato interno (e' opaco fuori da questo file): lo
 * dimostra mostrando che una chiamata successiva con az_max pieno si
 * comporta ESATTAMENTE come se le chiamate con az_max insufficiente non
 * fossero mai avvenute. */
static void prova_joystick_az_max_insufficiente(void)
{
    Mappa *m = mappa_di_prova();
    MappaAzione az[MAPPA_AZIONI_MAX];
    int n;

    /* Ramo BEGIN: servono 2 azioni (BEGIN+UPDATE). Con 0 o 1 non deve
     * scrivere niente. */
    n = mappa_tasto(m, 'W', true, false, az, 0);
    CHECK(n == 0);
    n = mappa_tasto(m, 'W', true, false, az, 1);
    CHECK(n == 0);

    /* Se una delle chiamate sopra avesse gia' segnato W come premuto senza
     * completare il BEGIN, qui il joystick sembrerebbe "gia' giu'" e questa
     * chiamata produrrebbe solo un UPDATE invece del BEGIN+UPDATE intero. */
    n = mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 2);
    CHECK(az[0].tipo == MAPPA_AZ_BEGIN);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);
    CHECK(az[1].tipo == MAPPA_AZ_UPDATE);
    CHECK(az[1].id == MAPPA_ID_JOYSTICK);

    /* Ramo END, il caso peggiore: il joystick e' adesso davvero giu'.
     * Rilasciare con az_max 0 non deve scrivere niente. Se lo stato fosse
     * mutato lo stesso, joy_giu diventerebbe falso senza che l'END sia mai
     * arrivato al chiamante: il dito resterebbe incollato nel guest, perche'
     * nessuno gli ha mai detto di sollevarlo. */
    n = mappa_tasto(m, 'W', false, false, az, 0);
    CHECK(n == 0);

    /* La chiamata piena deve produrre un END vero. Se la chiamata da zero
     * avesse gia' rilasciato W, questa tornerebbe 0 ("niente e' cambiato")
     * invece dell'END: quel 0 e' proprio il sintomo del dito incollato. */
    n = mappa_tasto(m, 'W', false, false, az, MAPPA_AZIONI_MAX);
    CHECK(n == 1);
    CHECK(az[0].tipo == MAPPA_AZ_END);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);

    mappa_distruggi(m);
}

static void prova_tasto_e_mio(void)
{
    Mappa *m = mappa_di_prova();

    CHECK(mappa_tasto_e_mio(m, 'W') == true);
    CHECK(mappa_tasto_e_mio(m, 'Q') == true);
    CHECK(mappa_tasto_e_mio(m, 'Z') == false);

    mappa_distruggi(m);
}

/* Rilievo C1 della revisione d'insieme: IL TASTO INCOLLATO NEL GUEST QUANDO LA
 * MAPPA SI ACCENDE.
 *
 * La sequenza: mappa spenta, l'utente tiene premuto W (il guest riceve KEY_W
 * DOWN come tasto normale), poi la mappa si accende -- fine di un modo impara,
 * o scelta di un profilo dal menu. L'utente rilascia W.
 *
 * Il chiamante sopprime un tasto quando mappa_tasto non ha prodotto azioni ma
 * mappa_tasto_e_mio dice che e' suo. Questa prova fissa le due risposte che
 * quella regola deve distinguere: su un RILASCIO a zero azioni il KEYUP va
 * consegnato al guest (altrimenti il guest resta con W premuto per sempre),
 * mentre su una PRESSIONE a zero azioni -- la ripetizione automatica -- va
 * soppresso (altrimenti tenere premuto W scrive "wwwww" nel gioco). */
static void prova_rilascio_dopo_accensione_a_tasto_gia_giu(void)
{
    Mappa *m = mappa_di_prova();
    MappaAzione az[MAPPA_AZIONI_MAX];

    /* La mappa e' appena stata caricata: per lei W non e' mai stato premuto,
     * anche se l'utente lo sta tenendo giu' da prima. Il rilascio non ha
     * dunque niente da fare... */
    CHECK(mappa_tasto(m, 'W', false, false, az, MAPPA_AZIONI_MAX) == 0);
    /* ...ma W E' un tasto della mappa: e' questa coppia di risposte che il
     * chiamante NON deve leggere come "sopprimi", o l'UP non arriva mai. */
    CHECK(mappa_tasto_e_mio(m, 'W') == true);

    /* Il caso opposto, quello per cui la soppressione esiste: la ripetizione
     * automatica e' sempre una PRESSIONE, e li' zero azioni vuol dire davvero
     * "mio, ma niente da fare". */
    CHECK(mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX) == 2);
    CHECK(mappa_tasto(m, 'W', true, true, az, MAPPA_AZIONI_MAX) == 0);
    CHECK(mappa_tasto_e_mio(m, 'W') == true);

    mappa_distruggi(m);
}

/* Misura IN PIXEL lo spostamento del dito nelle quattro direzioni pure e
 * verifica che sia atteso_px in tutte e quattro.
 *
 * I PIXEL SONO L'UNITA' IN CUI IL DIFETTO SI VEDE: in permille i numeri dei due
 * assi sono diversi anche quando tutto e' giusto, quindi confrontarli li' non
 * distingue uno stick rotondo da un'ellisse. La tolleranza di +-2 e'
 * l'arrotondamento delle due conversioni intere in fila, non un margine di
 * comodo. */
static void verifica_stick_rotondo(Mappa *m, int larghezza, int altezza,
                                   int cx_pm, int cy_pm, int atteso_px)
{
    MappaAzione az[MAPPA_AZIONI_MAX];
    /* su, sinistra, giu', destra */
    static const char dir[4] = { 'W', 'A', 'S', 'D' };
    int cx_px = mappa_pm_a_px(cx_pm, larghezza);
    int cy_px = mappa_pm_a_px(cy_pm, altezza);
    int i;

    for (i = 0; i < 4; i++) {
        int x_px, y_px, scarto;
        int n = mappa_tasto(m, dir[i], true, false, az, MAPPA_AZIONI_MAX);

        CHECK(n == 2);                       /* BEGIN al centro, poi UPDATE */
        x_px = mappa_pm_a_px(az[1].x_pm, larghezza);
        y_px = mappa_pm_a_px(az[1].y_pm, altezza);
        /* Una direzione pura muove un asse solo: lo scarto e' la somma dei due
         * moduli, e l'altro vale zero. */
        scarto = (x_px > cx_px ? x_px - cx_px : cx_px - x_px)
               + (y_px > cy_px ? y_px - cy_px : cy_px - y_px);
        CHECK(scarto >= atteso_px - 2 && scarto <= atteso_px + 2);
        mappa_tasto(m, dir[i], false, false, az, MAPPA_AZIONI_MAX);
    }
}

/* Rilievo I1: LO STICK ERA UN'ELLISSE.
 *
 * Il raggio e' in permille del LATO CORTO (spec sezione 4) proprio perche' lo
 * spostamento in PIXEL sia lo stesso nelle quattro direzioni. Senza
 * mappa_imposta_proporzioni il modulo sommava il raggio tale e quale sia a cx
 * (permille di larghezza) sia a cy (permille di altezza): su 1600x900 con
 * raggio 200, "destra" spostava 320 px e "su" 180. */
static void prova_joystick_rotondo_su_area_non_quadrata(void)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;

    CHECK(mappa_carica(m, scrivi("joystick WASD 500 500 200\n"),
                       err, sizeof(err), &riga) == true);
    mappa_imposta_proporzioni(m, 1600, 900);

    /* 200 permille del lato corto (900 px) sono 180 px. */
    verifica_stick_rotondo(m, 1600, 900, 500, 500, 180);

    mappa_distruggi(m);
}

/* Rilievo N4a: LE PROPORZIONI DEVONO SOPRAVVIVERE A mappa_carica.
 *
 * mappa_carica azzera la struttura, e le proporzioni non vengono dal file:
 * salvarle e rimetterle e' l'unica ragione per cui SCEGLIERE UN PROFILO DAL
 * MENU non riapre l'ellisse fino al primo ridimensionamento -- un guasto che
 * comparirebbe e sparirebbe senza motivo apparente. Nessuna prova lo
 * verificava, perche' tutte le altre impostano le proporzioni DOPO il
 * caricamento: togliendo quelle righe la suite passava lo stesso.
 *
 * Qui le proporzioni si impostano UNA VOLTA SOLA, PRIMA, e non si toccano mai
 * piu'. Si coprono ENTRAMBI i punti in cui mappa_carica le salva e le rimette:
 * quello del caricamento riuscito e quello del ramo di errore. */
static void prova_proporzioni_sopravvivono_al_caricamento(void)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;

    mappa_imposta_proporzioni(m, 1600, 900);

    CHECK(mappa_carica(m, scrivi("joystick WASD 500 500 200\n"),
                       err, sizeof(err), &riga) == true);
    verifica_stick_rotondo(m, 1600, 900, 500, 500, 180);

    /* Anche un caricamento FALLITO azzera la struttura, e anche li' le
     * proporzioni vanno rimesse: perdendole, il profilo caricato subito dopo
     * sarebbe ellittico e nessuno collegherebbe le due cose. */
    CHECK(mappa_carica(m, scrivi("touch NONESISTE 1 1\n"),
                       err, sizeof(err), &riga) == false);
    CHECK(mappa_carica(m, scrivi("joystick WASD 500 500 200\n"),
                       err, sizeof(err), &riga) == true);
    verifica_stick_rotondo(m, 1600, 900, 500, 500, 180);

    mappa_distruggi(m);
}

/* Rilievo N3, una REGRESSIONE: il cerchio si valida PER ASSE.
 *
 * La verifica confrontava il raggio -- permille del LATO CORTO -- con cx e cy,
 * che sono permille dei rispettivi assi. Su 1600x900
 * "joystick WASD 150 500 200" veniva RIFIUTATO con "il joystick esce
 * dall'area", ma premendo A il dito finisce a 150 - 113 = 37 permille, cioe'
 * ben dentro: un profilo scritto a mano e valido smetteva di caricarsi PER
 * INTERO, e il messaggio non era vero. */
static void prova_joystick_stretto_valido_su_area_non_quadrata(void)
{
    Mappa *m = mappa_crea(), *quadrata = mappa_crea(), *fuori = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;

    mappa_imposta_proporzioni(m, 1600, 900);
    CHECK(mappa_carica(m, scrivi("joystick WASD 150 500 200\n"),
                       err, sizeof(err), &riga) == true);
    /* E il dito ci sta davvero: 200 permille del lato corto (900) valgono 113
     * permille di 1600, quindi "sinistra" lo porta a 37 e non a -50. */
    CHECK(mappa_tasto(m, 'A', true, false, az, MAPPA_AZIONI_MAX) == 2);
    CHECK(az[1].x_pm >= 36 && az[1].x_pm <= 38);
    CHECK(az[1].y_pm == 500);

    /* LO STESSO FILE su un'area QUADRATA (proporzioni mai impostate) resta
     * rifiutato, ed e' giusto: li' 150 - 200 sarebbe davvero -50. E' questa
     * coppia a dimostrare che la validazione GUARDA le proporzioni: una
     * verifica che le ignorasse darebbe la stessa risposta a entrambe. */
    CHECK(mappa_carica(quadrata, scrivi("joystick WASD 150 500 200\n"),
                       err, sizeof(err), &riga) == false);
    CHECK(riga == 1);

    /* E sulla stessa area non quadrata si rifiuta cio' che esce DAVVERO: qui il
     * lato corto e' l'altezza, quindi sull'asse y il raggio vale 200 permille
     * pieni e 100 - 200 e' fuori. Senza questa meta', una validazione che
     * accettasse tutto passerebbe la prova qui sopra. */
    mappa_imposta_proporzioni(fuori, 1600, 900);
    CHECK(mappa_carica(fuori, scrivi("joystick WASD 500 100 200\n"),
                       err, sizeof(err), &riga) == false);
    CHECK(riga == 1);
    CHECK(strstr(err, "esce dall'area") != NULL);

    mappa_distruggi(m);
    mappa_distruggi(quadrata);
    mappa_distruggi(fuori);
}

/* Rilievi N1 e N4b: LA RIDUZIONE DEL RAGGIO NON ERA NE' PROVATA NE' DETTA.
 *
 * raggio_dentro_area non veniva fatta scattare da nessuna prova. Il caso
 * frequente non e' vistoso: lo stick di un gioco sta in un ANGOLO, li' il
 * massimo consentito e' piccolo, quindi lo stick funziona ma "tira" a una
 * distanza diversa da quella appena tracciata col mouse, e non c'era niente da
 * cui capirlo. */
static void prova_impara_raggio_ridotto(void)
{
    Mappa *m = mappa_crea(), *m2 = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1, misurato = -1, usato = -1;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));

    /* Proporzioni non impostate: area quadrata. Centro a 100 dal bordo
     * sinistro, bordo cliccato a 300 di distanza: il massimo consentito e' 100. */
    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK) == true);
    /* Prima che un apprendimento si concluda non c'e' niente da riportare: un
     * numero vecchio riportato come nuovo farebbe scrivere "raggio ridotto" su
     * un gesto che non ha ridotto niente. */
    CHECK(mappa_impara_raggio(m, &misurato, &usato) == false);
    CHECK(mappa_impara_punto(m, 100, 500) == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m, 400, 500) == MAPPA_ESITO_FATTO);

    CHECK(mappa_impara_raggio(m, &misurato, &usato) == true);
    CHECK(misurato == 300);
    CHECK(usato == 100);

    /* Ed e' il raggio RIDOTTO a essere finito nella mappa: A porta il dito
     * esattamente a zero, non fuori. */
    CHECK(mappa_tasto(m, 'A', true, false, az, MAPPA_AZIONI_MAX) == 2);
    CHECK(az[1].x_pm == 0 && az[1].y_pm == 500);
    mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);

    /* Il motivo per cui la riduzione esiste: cio' che si scrive si rilegge. */
    CHECK(mappa_salva(m, "test-mappa-ridotta.txt", err, sizeof(err)) == true);
    CHECK(mappa_carica(m2, "test-mappa-ridotta.txt", err, sizeof(err), &riga)
          == true);
    remove("test-mappa-ridotta.txt");

    /* Un gesto che NON sfora non deve risultare ridotto, o il chiamante
     * scriverebbe quella riga a ogni apprendimento e smetterebbe di essere
     * un'informazione. */
    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK) == true);
    CHECK(mappa_impara_punto(m, 500, 500) == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m, 600, 500) == MAPPA_ESITO_FATTO);
    CHECK(mappa_impara_raggio(m, &misurato, &usato) == true);
    CHECK(misurato == 100 && usato == 100);

    mappa_distruggi(m);
    mappa_distruggi(m2);
}

/* La riduzione si calcola PER ASSE, come la validazione del lettore.
 *
 * Su 1600x900 il margine di 100 permille di LARGHEZZA vale 178 permille del
 * lato corto, non 100: un massimo preso senza convertire ridurrebbe a 100, e lo
 * stick verrebbe quasi la meta' di quello che ci starebbe -- piccolo in
 * silenzio, che e' lo stesso guasto del rilievo N1 con un altro numero. */
static void prova_impara_raggio_ridotto_per_asse(void)
{
    Mappa *m = mappa_crea(), *m2 = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1, misurato = -1, usato = -1;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));
    mappa_imposta_proporzioni(m, 1600, 900);
    mappa_imposta_proporzioni(m2, 1600, 900);

    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK) == true);
    CHECK(mappa_impara_punto(m, 100, 500) == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m, 400, 500) == MAPPA_ESITO_FATTO);
    CHECK(mappa_impara_raggio(m, &misurato, &usato) == true);
    CHECK(misurato == 533);   /* 300 permille di 1600 px, in permille di 900 */
    CHECK(usato == 178);      /* 100 permille di 1600 px, idem */

    /* Il dito arriva esattamente sul bordo dell'area, non prima e non dopo. */
    CHECK(mappa_tasto(m, 'A', true, false, az, MAPPA_AZIONI_MAX) == 2);
    CHECK(az[1].x_pm == 0);
    mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);

    /* E il file scritto si rilegge CON LE STESSE PROPORZIONI: e' li' che un
     * raggio piu' grande di un permille verrebbe rifiutato dal lettore. */
    CHECK(mappa_salva(m, "test-mappa-ridotta-asse.txt", err, sizeof(err))
          == true);
    CHECK(mappa_carica(m2, "test-mappa-ridotta-asse.txt", err, sizeof(err),
                       &riga) == true);
    remove("test-mappa-ridotta-asse.txt");

    mappa_distruggi(m);
    mappa_distruggi(m2);
}

/* Rilievo N1: IL RAGGIO ZERO NON SI SCRIVE.
 *
 * Con centro a (0,500) -- lo stick disegnato a filo del bordo sinistro -- il
 * massimo consentito e' zero, e il modulo scriveva "joystick WASD 0 500 0": un
 * file che si rilegge senza un errore e uno stick MORTO, con BEGIN e UPDATE
 * nello stesso punto, nessuna direzione e nessuna riga che lo dica. */
static void prova_impara_raggio_zero_rifiutato(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1, misurato = -1, usato = -1;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));

    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK) == true);
    CHECK(mappa_impara_punto(m, 0, 500) == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m, 200, 500) == MAPPA_ESITO_FALLITO);
    /* Il chiamante puo' dire PERCHE': il gesto misurava 200 e non ne restava
     * niente. */
    CHECK(mappa_impara_raggio(m, &misurato, &usato) == true);
    CHECK(misurato == 200 && usato == 0);
    /* E NIENTE e' stato scritto. Uno stick morto avrebbe preso W, A, S e D
     * togliendoli al guest senza dare niente in cambio. */
    CHECK(mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX) == 0);
    CHECK(mappa_tasto_e_mio(m, 'W') == false);
    /* Il modo impara e' CHIUSO, come per ogni MAPPA_ESITO_FALLITO: il clic
     * successivo non trova piu' niente. */
    CHECK(mappa_impara_punto(m, 500, 500) == MAPPA_ESITO_FALLITO);

    /* Stesso rifiuto coi due clic nello stesso punto: il raggio misurato e'
     * zero e lo stick sarebbe morto allo stesso modo. */
    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK) == true);
    CHECK(mappa_impara_punto(m, 500, 500) == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m, 500, 500) == MAPPA_ESITO_FALLITO);
    CHECK(mappa_impara_raggio(m, &misurato, &usato) == true);
    CHECK(misurato == 0 && usato == 0);
    CHECK(mappa_tasto_e_mio(m, 'W') == false);

    mappa_distruggi(m);
}

/* Rilievo N4c: PROPORZIONI MAI IMPOSTATE, O A ZERO.
 *
 * E' il modulo appena creato e la finestra ridotta a icona. Il contratto
 * (winq-mappa.h) dice che allora ci si comporta come su un'area QUADRATA -- il
 * raggio si somma tale e quale -- e soprattutto che non si divide per zero: le
 * conversioni per asse hanno il lato corto al denominatore, e nessuna prova
 * fissava questo caso. */
static void prova_proporzioni_non_impostate_o_a_zero(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1, misurato = -1, usato = -1, k;

    for (k = 0; k < 4; k++) {
        /* k == 0: mai impostate. Poi le tre forme di area vuota, ognuna dopo
         * proporzioni VERE: il ritorno a zero deve CANCELLARLE e non lasciare
         * in piedi le ultime buone (vedi mappa_imposta_proporzioni). */
        if (k > 0) mappa_imposta_proporzioni(m, 1600, 900);
        if (k == 1) mappa_imposta_proporzioni(m, 0, 0);
        if (k == 2) mappa_imposta_proporzioni(m, 1600, 0);
        if (k == 3) mappa_imposta_proporzioni(m, 0, 900);

        CHECK(mappa_carica(m, scrivi("joystick WASD 500 500 200\n"),
                           err, sizeof(err), &riga) == true);
        /* Comportamento QUADRATO: il raggio si somma tale e quale a entrambi
         * gli assi, in permille, senza conversioni e senza divisioni. */
        CHECK(mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX) == 2);
        CHECK(az[1].x_pm == 500 && az[1].y_pm == 300);
        mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);
        CHECK(mappa_tasto(m, 'D', true, false, az, MAPPA_AZIONI_MAX) == 2);
        CHECK(az[1].x_pm == 700 && az[1].y_pm == 500);
        mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);

        /* E il modo impara misura in permille senza dividere per zero. */
        CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK) == true);
        CHECK(mappa_impara_punto(m, 400, 500) == MAPPA_ESITO_ANCORA);
        CHECK(mappa_impara_punto(m, 400, 350) == MAPPA_ESITO_FATTO);
        CHECK(mappa_impara_raggio(m, &misurato, &usato) == true);
        CHECK(misurato == 150 && usato == 150);
    }

    mappa_distruggi(m);
}

/* Lo stesso vizio nel verso opposto: il modo impara misurava il raggio
 * sommando permille-di-larghezza e permille-di-altezza, quindi lo stesso gesto
 * visivo dava raggi quasi doppi a seconda della direzione -- 113 verso destra
 * contro 200 verso l'alto, sulla stessa distanza in pixel. */
static void prova_impara_raggio_uguale_in_ogni_direzione(void)
{
    Mappa *m = mappa_crea(), *m2 = mappa_crea();
    char err[128];
    int riga = -1;
    const int larghezza = 1600, altezza = 900, raggio_px = 180;
    const int cx_px = 800, cy_px = 450;
    MappaAzione az[MAPPA_AZIONI_MAX];
    int r_destra, r_sopra;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));
    CHECK(mappa_carica(m2, scrivi("# vuota\n"), err, sizeof(err), &riga));
    mappa_imposta_proporzioni(m, larghezza, altezza);
    mappa_imposta_proporzioni(m2, larghezza, altezza);

    /* Centro, poi un bordo a 180 px A DESTRA. */
    mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK);
    CHECK(mappa_impara_punto(m, mappa_px_a_pm(cx_px, larghezza),
                             mappa_px_a_pm(cy_px, altezza))
          == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m, mappa_px_a_pm(cx_px + raggio_px, larghezza),
                             mappa_px_a_pm(cy_px, altezza))
          == MAPPA_ESITO_FATTO);

    /* Centro, poi un bordo a 180 px SOPRA: stessa distanza in pixel. */
    mappa_impara_inizia(m2, MAPPA_IMPARA_JOYSTICK);
    CHECK(mappa_impara_punto(m2, mappa_px_a_pm(cx_px, larghezza),
                             mappa_px_a_pm(cy_px, altezza))
          == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m2, mappa_px_a_pm(cx_px, larghezza),
                             mappa_px_a_pm(cy_px - raggio_px, altezza))
          == MAPPA_ESITO_FATTO);

    /* Il raggio non e' osservabile direttamente (la struttura e' opaca): lo si
     * legge dallo spostamento verticale prodotto da W, che e' in permille di
     * altezza in entrambe le mappe e quindi confrontabile. */
    CHECK(mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX) == 2);
    r_destra = 500 - az[1].y_pm;
    CHECK(mappa_tasto(m2, 'W', true, false, az, MAPPA_AZIONI_MAX) == 2);
    r_sopra = 500 - az[1].y_pm;

    CHECK(r_destra >= r_sopra - 2 && r_destra <= r_sopra + 2);
    /* E il valore giusto: 180 px su un'altezza di 900 sono 200 permille. */
    CHECK(r_sopra >= 198 && r_sopra <= 202);

    mappa_distruggi(m);
    mappa_distruggi(m2);
}

/* Rilievo I6: la spec (sezione 13) pretende che pixel->permille->pixel torni al
 * punto di partenza entro un pixel, e nessuno l'aveva mai provato -- era falsa,
 * perche' entrambe le conversioni troncavano: su 2880 px, 2879 diventava 999
 * permille e tornava 2877. Si arrotonda nei due versi, e qui si prova su ogni
 * pixel di alcune larghezze reali. */
static void prova_giro_pixel_permille_pixel(void)
{
    static const int lati[] = { 1, 2, 3, 720, 900, 1600, 2880 };
    size_t k;

    for (k = 0; k < sizeof(lati) / sizeof(lati[0]); k++) {
        int lato = lati[k], px, peggiore = 0;

        for (px = 0; px < lato; px++) {
            int pm = mappa_px_a_pm(px, lato);
            int ritorno = mappa_pm_a_px(pm, lato);
            int scarto = ritorno > px ? ritorno - px : px - ritorno;

            if (scarto > peggiore) peggiore = scarto;
            /* Fuori da 0..1000 il permille non sarebbe nemmeno scrivibile nel
             * file: il giro deve restare dentro il formato. */
            CHECK(pm >= 0 && pm <= 1000);
        }
        CHECK(peggiore <= 1);
    }

    /* La saturazione (rilievo I3b): 1000 permille e' legale e deve indicare
     * l'ULTIMO pixel. Senza, "touch SPAZIO 500 1000" dava un pixel FUORI
     * dall'area cliente e il tocco veniva rifiutato a valle -- non scattava
     * mai, in silenzio. */
    CHECK(mappa_pm_a_px(1000, 1600) == 1599);
    CHECK(mappa_pm_a_px(1000, 900) == 899);
    CHECK(mappa_pm_a_px(0, 1600) == 0);
    /* Area cliente vuota (finestra ridotta a icona): zero, non una divisione
     * per zero. */
    CHECK(mappa_pm_a_px(500, 0) == 0);
    CHECK(mappa_px_a_pm(100, 0) == 0);
}

static void prova_rilascia_tutto(void)
{
    Mappa *m = mappa_di_prova();
    MappaAzione az[MAPPA_AZIONI_MAX];
    int n;

    mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX);
    mappa_tasto(m, 'Q', true, false, az, MAPPA_AZIONI_MAX);
    mappa_tasto(m, 'E', true, false, az, MAPPA_AZIONI_MAX);

    n = mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);
    CHECK(n == 3);
    CHECK(az[0].tipo == MAPPA_AZ_END);
    CHECK(az[0].id == MAPPA_ID_JOYSTICK);
    CHECK(az[1].tipo == MAPPA_AZ_END);
    CHECK(az[1].id == MAPPA_ID_TOCCO(0));   /* Q, tocco in posizione 0 */
    CHECK(az[2].tipo == MAPPA_AZ_END);
    CHECK(az[2].id == MAPPA_ID_TOCCO(1));   /* E, tocco in posizione 1 */

    /* Chiamata due volte di fila, la seconda non produce niente. */
    n = mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);
    CHECK(n == 0);

    mappa_distruggi(m);
}

/* Il caso che dimensiona MAPPA_AZIONI_MAX: tutti i tocchi piu' il joystick giu'
 * insieme, cioe' 33 END in una chiamata sola.
 *
 * I 32 tasti sono scelti in modo da NON contenere W, A, S, D: se li
 * contenessero, quei quattro apparterrebbero sia al joystick sia ai tocchi e il
 * conto atteso diventerebbe ambiguo -- e una prova con un'asserzione sfocata su
 * un numero non prova il numero. Le lettere disponibili sono 26 meno quelle
 * quattro, cioe' 22, piu' le dieci cifre: esattamente 32. */
static const char TASTI_32[] = "BCEFGHIJKLMNOPQRTUVXYZ0123456789";

static void prova_rilascia_tutto_al_massimo(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128], testo[4096];
    int riga = -1, i, n, len = 0;

    CHECK(sizeof(TASTI_32) - 1 == MAPPA_TOCCHI_MAX);

    len += snprintf(testo + len, sizeof(testo) - len, "joystick WASD 500 500 100\n");
    for (i = 0; i < MAPPA_TOCCHI_MAX; i++) {
        len += snprintf(testo + len, sizeof(testo) - len,
                        "touch %c %d %d\n", TASTI_32[i], 100 + i, 200);
    }
    CHECK(mappa_carica(m, scrivi(testo), err, sizeof(err), &riga) == true);

    mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX);
    for (i = 0; i < MAPPA_TOCCHI_MAX; i++) {
        mappa_tasto(m, TASTI_32[i], true, false, az, MAPPA_AZIONI_MAX);
    }
    n = mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);
    CHECK(n == MAPPA_TOCCHI_MAX + 1);   /* 33: nessun troncamento */

    /* Il chiamante usa az[].id per assegnare gli slot del tocco multiplo nel
     * guest: due diti con lo stesso identificativo si pesterebbero i piedi
     * sullo stesso slot. Qui e' il caso peggiore, 33 identificativi in una
     * volta sola, il posto giusto per accorgersi di una collisione. */
    {
        bool tutti_diversi = true;
        int j;
        for (i = 0; i < n && tutti_diversi; i++) {
            for (j = i + 1; j < n; j++) {
                if (az[i].id == az[j].id) { tutti_diversi = false; break; }
            }
        }
        CHECK(tutti_diversi);
    }

    mappa_distruggi(m);
}

static void prova_caricamento_fallito_svuota(void)
{
    Mappa *m = mappa_di_prova();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;

    CHECK(mappa_tasto(m, 'Q', true, false, az, MAPPA_AZIONI_MAX) == 1);
    mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);

    CHECK(mappa_carica(m, scrivi("touch NONESISTE 1 1\n"),
                       err, sizeof(err), &riga) == false);
    /* Q era nella mappa PRECEDENTE: dopo il fallimento non deve piu' esserci. */
    CHECK(mappa_tasto(m, 'Q', true, false, az, MAPPA_AZIONI_MAX) == 0);

    mappa_distruggi(m);
}

static void prova_impara_tocco(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;

    CHECK(mappa_carica(m, scrivi("touch Q 250 700\n"), err, sizeof(err), &riga));

    /* Un tasto nuovo si aggiunge. */
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    CHECK(mappa_impara_tasto(m, 'R') == true);
    CHECK(mappa_impara_punto(m, 400, 300) == MAPPA_ESITO_FATTO);
    CHECK(mappa_tasto(m, 'R', true, false, az, MAPPA_AZIONI_MAX) == 1);
    CHECK(az[0].x_pm == 400 && az[0].y_pm == 300);
    mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);

    /* Un tasto gia' assegnato si SOSTITUISCE. */
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    CHECK(mappa_impara_tasto(m, 'Q') == true);
    CHECK(mappa_impara_punto(m, 111, 222) == MAPPA_ESITO_FATTO);
    CHECK(mappa_tasto(m, 'Q', true, false, az, MAPPA_AZIONI_MAX) == 1);
    CHECK(az[0].x_pm == 111 && az[0].y_pm == 222);

    mappa_distruggi(m);
}

/* Rilievo I5: mappa_impara_punto ritornava false CON DUE SIGNIFICATI, e il
 * chiamante non poteva distinguerli. Con i tocchi gia' al tetto il modulo
 * chiudeva il modo impara e ritornava false; il chiamante leggeva "non ancora
 * completo" e restava con il proprio flag acceso, i due stati divergevano e da
 * li' in poi ogni clic cadeva nel vuoto senza una riga di registro. Qui si
 * provano ENTRAMBI i rami che prima erano lo stesso valore. */
static void prova_impara_punto_ancora_e_fallito(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128], testo[4096];
    int riga = -1, i, len = 0;

    CHECK(mappa_carica(m, scrivi("touch Q 250 700\n"), err, sizeof(err), &riga));

    /* ANCORA: il clic e' arrivato prima del tasto. Il modo impara resta
     * APERTO, e infatti il tasto si puo' ancora dare e l'assegnazione si
     * conclude. */
    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO) == true);
    CHECK(mappa_impara_punto(m, 400, 300) == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_tasto(m, 'R') == true);
    CHECK(mappa_impara_punto(m, 400, 300) == MAPPA_ESITO_FATTO);
    CHECK(mappa_tasto(m, 'R', true, false, az, MAPPA_AZIONI_MAX) == 1);
    mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);

    /* FALLITO: mappa piena. Il modo impara si CHIUDE da se' senza scrivere
     * niente, e il chiamante deve poterlo sapere. */
    for (i = 0; i < MAPPA_TOCCHI_MAX; i++) {
        len += snprintf(testo + len, sizeof(testo) - len,
                        "touch %c %d %d\n", TASTI_32[i], 100 + i, 200);
    }
    CHECK(mappa_carica(m, scrivi(testo), err, sizeof(err), &riga) == true);

    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO) == true);
    CHECK(mappa_impara_tasto(m, 'W') == true);   /* W non e' fra i 32 */
    CHECK(mappa_impara_punto(m, 400, 300) == MAPPA_ESITO_FALLITO);
    /* Il modo impara e' davvero chiuso: un secondo clic non trova piu' niente
     * (prima restava aperto solo nella testa del chiamante), e nemmeno il
     * tasto si puo' piu' dare. */
    CHECK(mappa_impara_punto(m, 400, 300) == MAPPA_ESITO_FALLITO);
    CHECK(mappa_impara_tasto(m, 'W') == false);
    /* E niente e' stato scritto: W resta non mappato. */
    CHECK(mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX) == 0);

    mappa_distruggi(m);
}

/* Rilievo I2: mappa_salva cancellava i commenti dell'utente. Il profilo che il
 * prodotto spedisce, runtime/keymaps/generico.txt, ha sei righe di commento che
 * documentano il formato: il primo "impara un tasto" le faceva sparire. */
static void prova_salva_conserva_commenti_in_testa(void)
{
    Mappa *m = mappa_crea(), *m2 = mappa_crea();
    char err[128], riletto[2048];
    int riga = -1;
    size_t letti;
    FILE *f;
    static const char *nome = "test-mappa-commenti.txt";

    f = fopen(nome, "wb");
    CHECK(f != NULL);
    if (!f) return;
    fputs("# prima riga: il formato\n"
          "# seconda riga: annotazione dell'utente\n"
          "\n"
          "# terza, dopo una riga vuota\n"
          "touch Q 250 700\n"
          "# questo sta IN MEZZO e non si conserva (limite dichiarato)\n"
          "touch E 750 700\n", f);
    fclose(f);

    CHECK(mappa_carica(m, nome, err, sizeof(err), &riga) == true);
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    CHECK(mappa_impara_tasto(m, 'R') == true);
    CHECK(mappa_impara_punto(m, 400, 300) == MAPPA_ESITO_FATTO);
    CHECK(mappa_salva(m, nome, err, sizeof(err)) == true);

    f = fopen(nome, "rb");
    CHECK(f != NULL);
    if (!f) return;
    letti = fread(riletto, 1, sizeof(riletto) - 1, f);
    riletto[letti] = '\0';
    fclose(f);

    CHECK(strstr(riletto, "# prima riga: il formato") != NULL);
    CHECK(strstr(riletto, "# seconda riga: annotazione dell'utente") != NULL);
    CHECK(strstr(riletto, "# terza, dopo una riga vuota") != NULL);
    /* L'intestazione fissa NON si aggiunge sopra i commenti che c'erano: il
     * file crescerebbe di due righe a ogni salvataggio. */
    CHECK(strstr(riletto, "scritta dal modo impara") == NULL);
    /* E cio' che si e' scritto si rilegge: i commenti conservati non devono
     * poter rompere il lettore. */
    CHECK(mappa_carica(m2, nome, err, sizeof(err), &riga) == true);

    /* Su un file che NON esiste si scrive l'intestazione fissa di sempre. */
    remove("test-mappa-senza-testa.txt");
    CHECK(mappa_salva(m, "test-mappa-senza-testa.txt", err, sizeof(err)) == true);
    f = fopen("test-mappa-senza-testa.txt", "rb");
    CHECK(f != NULL);
    if (f) {
        letti = fread(riletto, 1, sizeof(riletto) - 1, f);
        riletto[letti] = '\0';
        fclose(f);
        CHECK(strstr(riletto, "scritta dal modo impara") != NULL);
    }

    remove("test-mappa-senza-testa.txt");
    remove(nome);
    mappa_distruggi(m);
    mappa_distruggi(m2);
}

/* Rilievo N5: UNA RIGA DI COMMENTO PIU' LUNGA DEL BUFFER CHIUDE LA TESTA.
 *
 * Il commento diceva che si conservava intera; se ne conservava invece il primo
 * pezzo, con un '\n' appiccicato in fondo, e il resto andava perso. Il file che
 * ne usciva conteneva una riga lunga quanto tutto il buffer, cioe' proprio
 * quella che mappa_carica rifiuta con "riga troppo lunga": lo stesso modulo che
 * scrive un file che non sa rileggere. */
static void prova_salva_commento_troppo_lungo(void)
{
    Mappa *m = mappa_crea(), *m2 = mappa_crea();
    char err[128], testo[1024], riletto[2048];
    int riga = -1, i, len = 0;
    size_t letti;
    FILE *f;
    static const char *nome = "test-mappa-commento-lungo.txt";

    len += snprintf(testo + len, sizeof(testo) - len, "# corto e conservato\n#");
    for (i = 0; i < 400; i++) testo[len++] = 'x';
    len += snprintf(testo + len, sizeof(testo) - len, "\ntouch Q 250 700\n");

    f = fopen(nome, "wb");
    CHECK(f != NULL);
    if (!f) { mappa_distruggi(m); mappa_distruggi(m2); return; }
    fwrite(testo, 1, (size_t)len, f);
    fclose(f);

    /* mappa_carica rifiuta questo file, ed e' giusto: il limite di riga vale
     * anche per i commenti. Si arriva comunque a salvarci sopra perche' la
     * riga lunga puo' comparire DOPO il caricamento, scritta a mano. */
    CHECK(mappa_carica(m, nome, err, sizeof(err), &riga) == false);
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    CHECK(mappa_impara_tasto(m, 'R') == true);
    CHECK(mappa_impara_punto(m, 400, 300) == MAPPA_ESITO_FATTO);
    CHECK(mappa_salva(m, nome, err, sizeof(err)) == true);

    f = fopen(nome, "rb");
    CHECK(f != NULL);
    if (!f) { mappa_distruggi(m); mappa_distruggi(m2); return; }
    letti = fread(riletto, 1, sizeof(riletto) - 1, f);
    riletto[letti] = '\0';
    fclose(f);

    /* Il commento corto che stava PRIMA si conserva; quello troppo lungo no,
     * nemmeno a pezzi. */
    CHECK(strstr(riletto, "# corto e conservato") != NULL);
    CHECK(strstr(riletto, "xxxxxxxx") == NULL);
    /* E cio' che conta davvero: il file riscritto si rilegge. Prima no. */
    CHECK(mappa_carica(m2, nome, err, sizeof(err), &riga) == true);

    /* L'ULTIMA riga di un file puo' legittimamente non avere il '\n', e quella
     * non e' "troppo lunga": un commento cosi' va CONSERVATO. E' la meta' che
     * un controllo sul solo '\n' mancante butterebbe via, ed e' il caso di un
     * file di soli commenti scritto a mano. */
    f = fopen(nome, "wb");
    CHECK(f != NULL);
    if (!f) { mappa_distruggi(m); mappa_distruggi(m2); return; }
    fputs("# senza fine riga finale", f);
    fclose(f);
    CHECK(mappa_salva(m, nome, err, sizeof(err)) == true);
    f = fopen(nome, "rb");
    CHECK(f != NULL);
    if (f) {
        letti = fread(riletto, 1, sizeof(riletto) - 1, f);
        riletto[letti] = '\0';
        fclose(f);
        /* Col '\n' aggiunto, o la prima riga di dati gli si attaccherebbe. */
        CHECK(strstr(riletto, "# senza fine riga finale\ntouch R") != NULL);
    }
    CHECK(mappa_carica(m2, nome, err, sizeof(err), &riga) == true);

    remove(nome);
    mappa_distruggi(m);
    mappa_distruggi(m2);
}

/* Contatore di riga: mappa_carica incrementava a ogni fgets e non a ogni fine
 * riga vera. Una riga fisica piu' lunga del buffer non sfasava solo il numero
 * di riga: la sua CODA veniva analizzata come una riga a se', quindi un
 * COMMENTO lungo faceva rifiutare il file con "riga non riconosciuta" -- il
 * messaggio piu' fuorviante possibile su una riga che il formato ammette. */
static void prova_riga_troppo_lunga(void)
{
    Mappa *m = mappa_crea();
    char err[128], testo[1024];
    int riga = -1, i, len = 0;

    len += snprintf(testo + len, sizeof(testo) - len, "touch Q 250 700\n#");
    for (i = 0; i < 400; i++) testo[len++] = 'x';
    len += snprintf(testo + len, sizeof(testo) - len, "\ntouch E 750 700\n");

    CHECK(mappa_carica(m, scrivi(testo), err, sizeof(err), &riga) == false);
    CHECK(riga == 2);
    CHECK(strstr(err, "troppo lunga") != NULL);

    /* L'ULTIMA riga di un file puo' legittimamente non avere il '\n' finale:
     * quello non e' "troppo lunga". */
    CHECK(mappa_carica(m, scrivi("touch Q 250 700"), err, sizeof(err), &riga)
          == true);

    mappa_distruggi(m);
}

static void prova_impara_joystick(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));

    mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK);
    /* Primo clic: il centro. Non chiude ancora. */
    CHECK(mappa_impara_punto(m, 200, 750) == MAPPA_ESITO_ANCORA);
    /* Secondo clic: un punto sul bordo, a 100 permille di distanza. */
    CHECK(mappa_impara_punto(m, 300, 750) == MAPPA_ESITO_FATTO);

    CHECK(mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX) == 2);
    CHECK(az[0].x_pm == 200 && az[0].y_pm == 750);
    CHECK(az[1].x_pm == 200 && az[1].y_pm == 650);

    mappa_distruggi(m);
}

static void prova_impara_annulla(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;

    CHECK(mappa_carica(m, scrivi("touch Q 250 700\n"), err, sizeof(err), &riga));
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    CHECK(mappa_impara_tasto(m, 'R') == true);
    CHECK(mappa_impara_annulla(m) == true);
    CHECK(mappa_impara_annulla(m) == false);
    /* R non deve essere finito nella mappa. */
    CHECK(mappa_tasto(m, 'R', true, false, az, MAPPA_AZIONI_MAX) == 0);
    /* E Q deve essere rimasto. */
    CHECK(mappa_tasto(m, 'Q', true, false, az, MAPPA_AZIONI_MAX) == 1);

    mappa_distruggi(m);
}

/* Vincolo: non si deve poter creare una mappa che mappa_carica rifiuta. Se un
 * joystick e' definito, il modo impara non puo' assegnare un tocco ai suoi
 * quattro tasti: quello che viene scritto non sarebbe rileggibile. */
static void prova_impara_rifiuta_joystick(void)
{
    Mappa *m = mappa_crea();
    char err[128];
    int riga = -1;

    CHECK(mappa_carica(m, scrivi("joystick WASD 200 750 100\n"), err, sizeof(err), &riga));

    /* I quattro tasti del joystick (W, A, S, D) devono essere rifiutati. */
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    CHECK(mappa_impara_tasto(m, 'W') == false);
    CHECK(mappa_impara_tasto(m, 'A') == false);
    CHECK(mappa_impara_tasto(m, 'S') == false);
    CHECK(mappa_impara_tasto(m, 'D') == false);
    /* Gli altri tasti vanno bene. */
    CHECK(mappa_impara_tasto(m, 'Q') == true);

    mappa_distruggi(m);
}

/* Rilievo 1 della revisione: il controllo va anche nel verso opposto. Prima
 * della correzione, mappa_impara_punto fissava i quattro tasti del joystick a
 * W/A/S/D senza guardare m->tocchi, quindi si poteva imparare un tocco su W e
 * poi imparare il joystick sopra: mappa_salva scriveva un file che
 * mappa_carica rifiutava subito dopo. Il joystick usa SEMPRE W, A, S, D, quindi
 * il conflitto si conosce gia' quando si chiede di entrare in modo impara, e
 * mappa_impara_inizia deve rifiutarsi di farlo. */
static void prova_impara_rifiuta_joystick_verso_opposto(void)
{
    Mappa *m = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));

    /* Si impara un tocco su W su una mappa vuota: nessun joystick esiste
     * ancora, quindi e' accettato. */
    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO) == true);
    CHECK(mappa_impara_tasto(m, 'W') == true);
    CHECK(mappa_impara_punto(m, 250, 700) == MAPPA_ESITO_FATTO);

    /* Adesso W e' un tocco: chiedere di imparare il joystick (che
     * userebbe W come uno dei suoi quattro tasti) deve fallire subito. */
    CHECK(mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK) == false);

    /* La mappa deve essere rimasta esattamente com'era prima del tentativo
     * rifiutato: il tocco su W funziona ancora... */
    CHECK(mappa_tasto(m, 'W', true, false, az, MAPPA_AZIONI_MAX) == 1);
    CHECK(az[0].x_pm == 250 && az[0].y_pm == 700);
    mappa_rilascia_tutto(m, az, MAPPA_AZIONI_MAX);

    /* ...e nessun joystick e' stato definito: A non e' ne' un tocco ne' parte
     * di un joystick, quindi deve restare non mappato. */
    CHECK(mappa_tasto(m, 'A', true, false, az, MAPPA_AZIONI_MAX) == 0);

    mappa_distruggi(m);
}

/* La prova che conta: cio' che il modo impara scrive, il lettore lo rilegge. */
static void prova_salva_e_rileggi(void)
{
    Mappa *m = mappa_crea(), *m2 = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));
    mappa_impara_inizia(m, MAPPA_IMPARA_JOYSTICK);
    CHECK(mappa_impara_punto(m, 200, 750) == MAPPA_ESITO_ANCORA);
    CHECK(mappa_impara_punto(m, 300, 750) == MAPPA_ESITO_FATTO);
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    mappa_impara_tasto(m, 'Q');
    CHECK(mappa_impara_punto(m, 250, 700) == MAPPA_ESITO_FATTO);

    CHECK(mappa_salva(m, "test-mappa-salvata.txt", err, sizeof(err)) == true);
    CHECK(mappa_carica(m2, "test-mappa-salvata.txt", err, sizeof(err), &riga) == true);
    CHECK(mappa_tasto(m2, 'Q', true, false, az, MAPPA_AZIONI_MAX) == 1);
    CHECK(az[0].x_pm == 250 && az[0].y_pm == 700);
    CHECK(mappa_tasto(m2, 'W', true, false, az, MAPPA_AZIONI_MAX) == 2);

    remove("test-mappa-salvata.txt");
    mappa_distruggi(m);
    mappa_distruggi(m2);
}

/* Rilievo 3 della revisione: prova_salva_e_rileggi usa solo 'Q' e il
 * joystick, cioe' tasti che si scrivono e si leggono da se'. I tasti con nome
 * per esteso passano da vk_a_nome in scrittura e da nome_a_vk in lettura --
 * un giro diverso, mai provato da nessuna delle prove sopra. 0x20 e' il vk di
 * SPAZIO nella tabella tasti[] di winq-mappa.c: se vk_a_nome o nome_a_vk si
 * disallineassero su un nome per esteso, questa prova (e non solo quella con
 * 'Q') se ne accorgerebbe. */
static void prova_salva_e_rileggi_nome_esteso(void)
{
    Mappa *m = mappa_crea(), *m2 = mappa_crea();
    MappaAzione az[MAPPA_AZIONI_MAX];
    char err[128];
    int riga = -1;
    const int vk_spazio = 0x20;

    CHECK(mappa_carica(m, scrivi("# vuota\n"), err, sizeof(err), &riga));
    mappa_impara_inizia(m, MAPPA_IMPARA_TOCCO);
    CHECK(mappa_impara_tasto(m, vk_spazio) == true);
    CHECK(mappa_impara_punto(m, 500, 850) == MAPPA_ESITO_FATTO);

    CHECK(mappa_salva(m, "test-mappa-salvata-nome.txt", err, sizeof(err)) == true);
    CHECK(mappa_carica(m2, "test-mappa-salvata-nome.txt", err, sizeof(err), &riga) == true);
    CHECK(mappa_tasto(m2, vk_spazio, true, false, az, MAPPA_AZIONI_MAX) == 1);
    CHECK(az[0].x_pm == 500 && az[0].y_pm == 850);

    remove("test-mappa-salvata-nome.txt");
    mappa_distruggi(m);
    mappa_distruggi(m2);
}

int main(void)
{
    prova_file_valido();
    prova_solo_commenti();
    prova_caso_cattivo("touch NONESISTE 100 100\n", 1);
    prova_caso_cattivo("touch Q -1 100\n", 1);
    prova_caso_cattivo("touch Q 1001 100\n", 1);
    prova_caso_cattivo("touch Q 100\n", 1);
    prova_caso_cattivo("touch Q 100 100 100\n", 1);
    prova_caso_cattivo("touch Q 100 100\ntouch Q 200 200\n", 2);
    prova_caso_cattivo("joystick WASD 1 1 1\njoystick WASD 2 2 2\n", 2);
    prova_caso_cattivo("# commento\nrigaqualsiasi\n", 2);
    /* Rilievo 2: un tasto in comune fra joystick e tocco si rifiuta, in
     * entrambi gli ordini possibili di dichiarazione nel file. */
    prova_caso_cattivo("joystick WASD 200 750 100\n"
                       "touch W 100 100\n", 2);
    prova_caso_cattivo("touch W 100 100\n"
                       "joystick WASD 200 750 100\n", 2);
    /* Rilievo M2: i quattro tasti del joystick devono essere tutti diversi, o
     * tre indici su quattro sono morti senza che niente lo dica. */
    prova_caso_cattivo("joystick WWWW 200 750 120\n", 1);
    prova_caso_cattivo("joystick WASW 200 750 120\n", 1);
    /* Rilievo I3a: i tre campi sono in intervallo ma il CERCHIO esce
     * dall'area, e la direzione che esce non funziona in silenzio. Un caso
     * per il bordo sinistro e uno per quello inferiore: sono controlli
     * diversi, e uno solo dei due lascerebbe passare l'altro. */
    prova_caso_cattivo("joystick WASD 200 750 250\n", 1);
    prova_caso_cattivo("joystick WASD 500 900 150\n", 1);
    prova_file_inesistente();
    prova_joystick_stretto_valido_su_area_non_quadrata();
    prova_proporzioni_sopravvivono_al_caricamento();
    prova_proporzioni_non_impostate_o_a_zero();
    prova_riga_troppo_lunga();
    prova_tocco();
    prova_joystick();
    prova_joystick_opposti();
    prova_joystick_rotondo_su_area_non_quadrata();
    prova_joystick_az_max_insufficiente();
    prova_tasto_e_mio();
    prova_rilascio_dopo_accensione_a_tasto_gia_giu();
    prova_giro_pixel_permille_pixel();
    prova_rilascia_tutto();
    prova_rilascia_tutto_al_massimo();
    prova_caricamento_fallito_svuota();
    prova_impara_tocco();
    prova_impara_joystick();
    prova_impara_punto_ancora_e_fallito();
    prova_impara_raggio_uguale_in_ogni_direzione();
    prova_impara_raggio_ridotto();
    prova_impara_raggio_ridotto_per_asse();
    prova_impara_raggio_zero_rifiutato();
    prova_impara_annulla();
    prova_impara_rifiuta_joystick();
    prova_impara_rifiuta_joystick_verso_opposto();
    prova_salva_e_rileggi();
    prova_salva_e_rileggi_nome_esteso();
    prova_salva_conserva_commenti_in_testa();
    prova_salva_commento_troppo_lungo();

    remove("test-mappa-tmp.txt");
    if (falliti) {
        printf("%d prove fallite\n", falliti);
        return 1;
    }
    printf("tutte le prove passate\n");
    return 0;
}
