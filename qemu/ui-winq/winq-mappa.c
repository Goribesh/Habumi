/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-mappa.c -- vedi winq-mappa.h per il confine che questo file rispetta. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winq-mappa.h"

typedef struct {
    int     vk;          /* virtual-key di Windows */
    int32_t x_pm, y_pm;
    bool    giu;         /* il dito sintetico e' appoggiato adesso */
} MappaTocco;

struct Mappa {
    MappaTocco tocchi[MAPPA_TOCCHI_MAX];
    int        n_tocchi;

    bool       joy_c_e;
    int        joy_vk[4];        /* su, sinistra, giu', destra */
    bool       joy_premuto[4];
    int32_t    joy_cx, joy_cy, joy_r;
    bool       joy_giu;

    /* Proporzioni dell'area cliente in pixel, zero finche' nessuno le ha
     * dette. Vedi mappa_imposta_proporzioni in winq-mappa.h: servono a tenere
     * ROTONDO il raggio del joystick, che il formato esprime in permille del
     * lato corto mentre le coordinate sono in permille dei due lati. */
    int32_t    prop_w, prop_h;

    /* modo impara */
    bool            imp_attivo;
    MappaImparaTipo imp_tipo;
    int             imp_vk;
    bool            imp_ha_centro;
    int32_t         imp_cx, imp_cy;

    /* Il raggio dell'ULTIMO modo impara del joystick che si e' concluso: quello
     * che il gesto ha MISURATO e quello che e' finito nella mappa. Sono diversi
     * quando raggio_dentro_area ha dovuto ridurlo, ed e' l'unico modo che il
     * chiamante ha di accorgersene -- vedi mappa_impara_raggio. */
    bool            imp_r_noto;
    int32_t         imp_r_misurato, imp_r_usato;
};

/* L'elenco CHIUSO dei nomi dei tasti. Un nome fuori da qui e' un errore di
 * lettura e non un tasto ignorato in silenzio: una mappa in cui una riga non
 * fa niente, e nessuno lo dice, e' peggio di una mappa rifiutata. */
static const struct { const char *nome; int vk; } tasti[] = {
    { "SPAZIO", 0x20 }, { "INVIO", 0x0D }, { "MAIUSC", 0x10 },
    { "CTRL", 0x11 }, { "ALT", 0x12 }, { "TAB", 0x09 }, { "ESC", 0x1B },
    { "SU", 0x26 }, { "GIU", 0x28 }, { "SINISTRA", 0x25 }, { "DESTRA", 0x27 },
    { "F1", 0x70 }, { "F2", 0x71 }, { "F3", 0x72 }, { "F4", 0x73 },
    { "F5", 0x74 }, { "F6", 0x75 }, { "F7", 0x76 }, { "F8", 0x77 },
    { "F9", 0x78 }, { "F10", 0x79 }, { "F11", 0x7A }, { "F12", 0x7B }
};

/* Ritorna il virtual-key, o -1 se il nome non e' riconosciuto. */
static int nome_a_vk(const char *s)
{
    size_t i;

    if (s[0] && !s[1]) {
        if (s[0] >= 'A' && s[0] <= 'Z') return s[0];
        if (s[0] >= '0' && s[0] <= '9') return s[0];
    }
    for (i = 0; i < sizeof(tasti) / sizeof(tasti[0]); i++) {
        if (!strcmp(s, tasti[i].nome)) return tasti[i].vk;
    }
    return -1;
}

/* Il giro inverso di nome_a_vk, per i tasti che hanno un nome per esteso.
 * Ritorna NULL per lettere e cifre, che si scrivono da se'. */
static const char *vk_a_nome(int vk)
{
    size_t i;
    for (i = 0; i < sizeof(tasti) / sizeof(tasti[0]); i++) {
        if (tasti[i].vk == vk) return tasti[i].nome;
    }
    return NULL;
}

Mappa *mappa_crea(void)
{
    Mappa *m = calloc(1, sizeof(*m));
    return m;
}

void mappa_distruggi(Mappa *m)
{
    free(m);
}

void mappa_imposta_proporzioni(Mappa *m, int larghezza_px, int altezza_px)
{
    /* Una misura non positiva (finestra ridotta a icona) si registra come
     * "non so": vedi winq-mappa.h. Non si tiene l'ultima buona, perche' allora
     * il modulo continuerebbe a rispondere su proporzioni che non sono piu'
     * quelle dell'area cliente e nessuno saprebbe da dove vengono. */
    if (larghezza_px <= 0 || altezza_px <= 0) {
        m->prop_w = 0;
        m->prop_h = 0;
        return;
    }
    m->prop_w = larghezza_px;
    m->prop_h = altezza_px;
}

/* Il lato corto in pixel, o zero se le proporzioni non sono note. */
static int32_t lato_corto(const Mappa *m)
{
    if (m->prop_w <= 0 || m->prop_h <= 0) return 0;
    return m->prop_w < m->prop_h ? m->prop_w : m->prop_h;
}

/* Una lunghezza in permille del LATO CORTO -> la stessa lunghezza in permille
 * della larghezza (pm_x) o dell'altezza (pm_y). v deve essere >= 0: il segno
 * lo mette il chiamante DOPO, o l'arrotondamento verso lo zero della divisione
 * intera renderebbe i due versi asimmetrici.
 *
 * Con le proporzioni non note si risponde v: e' il comportamento di prima che
 * questa conversione esistesse, cioe' quello giusto su un'area quadrata. */
static int32_t pm_x_da_corto(const Mappa *m, int32_t v)
{
    int32_t corto = lato_corto(m);
    if (!corto) return v;
    return (int32_t)(((int64_t)v * corto + m->prop_w / 2) / m->prop_w);
}

static int32_t pm_y_da_corto(const Mappa *m, int32_t v)
{
    int32_t corto = lato_corto(m);
    if (!corto) return v;
    return (int32_t)(((int64_t)v * corto + m->prop_h / 2) / m->prop_h);
}

/* Il giro inverso dei due qui sopra, per il modo impara: una lunghezza in
 * permille di larghezza (o di altezza) -> permille del lato corto. Serve a
 * misurare il raggio da due clic senza sommare mele e pere -- senza, lo stesso
 * gesto visivo darebbe raggi quasi doppi a seconda della direzione. */
static int32_t corto_da_pm_x(const Mappa *m, int32_t v)
{
    int32_t corto = lato_corto(m);
    if (!corto) return v;
    return (int32_t)(((int64_t)v * m->prop_w + corto / 2) / corto);
}

static int32_t corto_da_pm_y(const Mappa *m, int32_t v)
{
    int32_t corto = lato_corto(m);
    if (!corto) return v;
    return (int32_t)(((int64_t)v * m->prop_h + corto / 2) / corto);
}

int mappa_pm_a_px(int pm, int lato_px)
{
    int64_t px;

    if (lato_px <= 0) return 0;
    px = ((int64_t)pm * lato_px + 500) / 1000;
    /* La saturazione, non il troncamento: vedi winq-mappa.h. 1000 permille e'
     * legale nel formato e deve indicare l'ULTIMO pixel, non il primo fuori. */
    if (px > lato_px - 1) px = lato_px - 1;
    if (px < 0) px = 0;
    return (int)px;
}

int mappa_px_a_pm(int px, int lato_px)
{
    int64_t pm;

    if (lato_px <= 0) return 0;
    pm = ((int64_t)px * 1000 + lato_px / 2) / lato_px;
    if (pm > 1000) pm = 1000;
    if (pm < 0) pm = 0;
    return (int)pm;
}

static bool in_intervallo(int v) { return v >= 0 && v <= 1000; }

/* True se dopo l'ultimo campo letto non c'e' altro che spazi o un commento. */
static bool resto_vuoto(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return *s == '\0' || *s == '#';
}

bool mappa_carica(Mappa *m, const char *percorso,
                  char *errore, size_t errore_n, int *riga)
{
    FILE *f;
    char linea[256];
    int n = 0;
    /* Le proporzioni dell'area cliente NON vengono dal file e sopravvivono a
     * ogni azzeramento qui dentro, riuscito o fallito: sono una proprieta'
     * della finestra. Perderle vorrebbe dire che scegliere un profilo dal menu
     * riporta di nascosto lo stick ellittico fino al primo ridimensionamento --
     * un guasto che comparirebbe e sparirebbe senza motivo apparente. */
    int32_t prop_w = m->prop_w, prop_h = m->prop_h;

    /* Si azzera SUBITO: qualunque cosa vada storta piu' sotto, la mappa che
     * resta e' vuota e non quella di prima. Vedi il commento nell'intestazione. */
    memset(m, 0, sizeof(*m));
    m->prop_w = prop_w;
    m->prop_h = prop_h;
    *riga = 0;
    errore[0] = '\0';

    f = fopen(percorso, "rb");
    if (!f) {
        snprintf(errore, errore_n, "non riesco ad aprire %s", percorso);
        return false;
    }

    while (fgets(linea, sizeof(linea), f)) {
        char parola[64], tasti_joy[16];
        int a, b, c, consumati;
        char *p = linea;

        n++;
        /* IL CONTATORE CONTA LE RIGHE FISICHE, non le chiamate a fgets, e per
         * riuscirci deve accorgersi quando una riga non ci sta nel buffer.
         * Senza questo controllo la CODA di una riga lunga veniva analizzata
         * come una riga a se': il numero di riga si sfasava per tutto il resto
         * del file e -- peggio -- un semplice COMMENTO lungo faceva rifiutare
         * il file con "riga non riconosciuta", perche' la sua coda non
         * cominciava piu' con '#'.
         *
         * L'assenza di '\n' non basta da sola: l'ultima riga di un file puo'
         * legittimamente non averlo. Si guarda il byte successivo invece di
         * feof(), che dopo una fgets che ha riempito il buffer esatto non e'
         * ancora acceso. */
        if (!strchr(linea, '\n')) {
            int prossimo = fgetc(f);

            if (prossimo != EOF) {
                ungetc(prossimo, f);
                snprintf(errore, errore_n, "riga troppo lunga (oltre %d caratteri)",
                         (int)sizeof(linea) - 1);
                goto male;
            }
        }
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        /* I campi in piu' si scoprono con %n e non con una sscanf che conta i
         * campi: "%15s %d %d %d %63s" su una riga "tocco" prenderebbe "tocco"
         * col primo %s e fallirebbe subito sul %d, quindi non scatterebbe MAI
         * e una riga "tocco Q 100 100 100" verrebbe accettata. %n dice dove
         * l'analisi si e' fermata, e si guarda cosa resta. */
        consumati = 0;
        if (sscanf(p, "touch %63s %d %d %n", parola, &a, &b, &consumati) == 3
            && consumati > 0) {
            int vk = nome_a_vk(parola), i;
            if (!resto_vuoto(p + consumati)) {
                snprintf(errore, errore_n, "campi in piu'");
                goto male;
            }
            if (vk < 0) {
                snprintf(errore, errore_n, "tasto sconosciuto: %s", parola);
                goto male;
            }
            /* Un tasto che e' GIA' fra i quattro del joystick non puo' anche
             * avere un tocco: mappa_tasto prova sempre prima il ramo del
             * joystick, quindi il tocco non scatterebbe mai e la riga che lo
             * dichiara sarebbe una mappa che funziona a meta' in silenzio.
             * Si rifiuta il file, non si inventa un ordine di precedenza. */
            if (m->joy_c_e) {
                for (i = 0; i < 4; i++) {
                    if (m->joy_vk[i] == vk) {
                        snprintf(errore, errore_n,
                                 "tasto %s e' anche nel joystick", parola);
                        goto male;
                    }
                }
            }
            if (!in_intervallo(a) || !in_intervallo(b)) {
                snprintf(errore, errore_n, "coordinata fuori da 0..1000");
                goto male;
            }
            for (i = 0; i < m->n_tocchi; i++) {
                if (m->tocchi[i].vk == vk) {
                    snprintf(errore, errore_n, "tasto %s gia' assegnato", parola);
                    goto male;
                }
            }
            if (m->n_tocchi >= MAPPA_TOCCHI_MAX) {
                snprintf(errore, errore_n, "piu' di %d tocchi", MAPPA_TOCCHI_MAX);
                goto male;
            }
            m->tocchi[m->n_tocchi].vk = vk;
            m->tocchi[m->n_tocchi].x_pm = a;
            m->tocchi[m->n_tocchi].y_pm = b;
            m->n_tocchi++;
            continue;
        }

        consumati = 0;
        if (sscanf(p, "joystick %15s %d %d %d %n",
                   tasti_joy, &a, &b, &c, &consumati) == 4 && consumati > 0) {
            int i, j;
            int32_t rx, ry;
            if (!resto_vuoto(p + consumati)) {
                snprintf(errore, errore_n, "campi in piu'");
                goto male;
            }
            if (m->joy_c_e) {
                snprintf(errore, errore_n, "un solo joystick per profilo");
                goto male;
            }
            if (strlen(tasti_joy) != 4) {
                snprintf(errore, errore_n, "il joystick vuole quattro tasti");
                goto male;
            }
            for (i = 0; i < 4; i++) {
                char uno[2]; uno[0] = tasti_joy[i]; uno[1] = '\0';
                m->joy_vk[i] = nome_a_vk(uno);
                if (m->joy_vk[i] < 0) {
                    snprintf(errore, errore_n, "tasto sconosciuto: %s", uno);
                    goto male;
                }
            }
            /* I quattro tasti devono essere tutti DIVERSI. "joystick WWWW"
             * passava ogni altro controllo e produceva tre indici su quattro
             * MORTI: mappa_tasto trova il primo che combacia e ritorna, quindi
             * W sarebbe solo "su" e le altre tre direzioni non esisterebbero,
             * senza che niente lo dica. */
            for (i = 0; i < 4; i++) {
                for (j = i + 1; j < 4; j++) {
                    if (m->joy_vk[i] == m->joy_vk[j]) {
                        snprintf(errore, errore_n,
                                 "i quattro tasti del joystick devono essere "
                                 "diversi: %c ripetuto", tasti_joy[i]);
                        goto male;
                    }
                }
            }
            /* Stesso controllo del ramo "tocco" qui sopra, ma nel verso
             * opposto: il file puo' dichiarare prima i tocchi e poi il
             * joystick che li contiene, e la collisione va rifiutata in
             * entrambi gli ordini. */
            for (i = 0; i < 4; i++) {
                for (j = 0; j < m->n_tocchi; j++) {
                    if (m->tocchi[j].vk == m->joy_vk[i]) {
                        char uno[2]; uno[0] = tasti_joy[i]; uno[1] = '\0';
                        snprintf(errore, errore_n,
                                 "key %s is also in a touch", uno);
                        goto male;
                    }
                }
            }
            if (!in_intervallo(a) || !in_intervallo(b) || !in_intervallo(c)) {
                snprintf(errore, errore_n, "coordinata fuori da 0..1000");
                goto male;
            }
            /* IL CERCHIO SI VALIDA INTERO, non un campo alla volta. Con i tre
             * campi controllati separatamente "joystick WASD 200 750 250"
             * passava: premendo A il dito finiva a -50 permille, la conversione
             * dava una coordinata cliente negativa e il record veniva SCARTATO
             * a valle. Su, giu' e destra funzionavano, sinistra no, e nessuno
             * lo diceva. Si rifiuta il file col numero di riga, come per ogni
             * altra coordinata fuori intervallo (spec sezione 12).
             *
             * E SI VALIDA PER ASSE, con le STESSE due conversioni che
             * joy_posizione usera' a runtime. Il raggio e' in permille del LATO
             * CORTO, cx e cy in permille dei rispettivi assi: confrontarli
             * direttamente somma mele e pere. Su un'area 1600x900
             * "joystick WASD 150 500 200" veniva rifiutato con "il joystick
             * esce dall'area", ma premendo A il dito finisce a
             * 150 - pm_x_da_corto(200) = 37 permille, cioe' ben dentro: un
             * profilo scritto a mano e perfettamente valido smetteva di
             * caricarsi PER INTERO, con un messaggio che non era vero.
             *
             * Con le proporzioni non ancora note pm_*_da_corto rispondono il
             * raggio tale e quale, cioe' il caso QUADRATO: e' esattamente il
             * controllo di prima, che li' e' quello giusto. */
            rx = pm_x_da_corto(m, c);
            ry = pm_y_da_corto(m, c);
            if (a - rx < 0 || a + rx > 1000 || b - ry < 0 || b + ry > 1000) {
                snprintf(errore, errore_n,
                         "il joystick esce dall'area: cx+-r e cy+-r devono "
                         "stare in 0..1000");
                goto male;
            }
            m->joy_cx = a; m->joy_cy = b; m->joy_r = c;
            m->joy_c_e = true;
            continue;
        }

        snprintf(errore, errore_n, "riga non riconosciuta");
    male:
        *riga = n;
        fclose(f);
        memset(m, 0, sizeof(*m));
        m->prop_w = prop_w;
        m->prop_h = prop_h;
        return false;
    }

    fclose(f);
    return true;
}

/* La posizione del dito del joystick, in permille.
 *
 * La direzione e' la somma dei versori dei tasti premuti, NORMALIZZATA. Senza
 * normalizzare, W+D darebbe 1,41 volte il raggio e il dito uscirebbe dall'area
 * dello stick: le quattro direzioni pure funzionerebbero e le diagonali no,
 * che e' il modo peggiore di rompersi perche' sembra un difetto del gioco.
 *
 * 707/1000 e' 1/sqrt(2) in aritmetica intera: l'errore e' sotto il permille,
 * che e' la tolleranza dichiarata nella spec. */
static void joy_posizione(const Mappa *m, int32_t *x, int32_t *y)
{
    int dx = 0, dy = 0;
    int32_t d = m->joy_r;

    if (m->joy_premuto[0]) dy -= 1;   /* su */
    if (m->joy_premuto[1]) dx -= 1;   /* sinistra */
    if (m->joy_premuto[2]) dy += 1;   /* giu' */
    if (m->joy_premuto[3]) dx += 1;   /* destra */

    if (dx && dy) {
        d = (d * 707 + 500) / 1000;
    }
    /* Il raggio e' in permille del LATO CORTO e va convertito PER ASSE, o lo
     * stick e' un'ellisse: su un'area 1600x900 con raggio 120, sommarlo tale e
     * quale a cx e a cy sposta il dito di 192 px a destra e di 108 px in su.
     * La normalizzazione delle diagonali qui sopra serve proprio a tenere il
     * dito su un CERCHIO, e senza questa conversione il cerchio si riapriva in
     * ellisse subito dopo, in pixel. Vedi mappa_imposta_proporzioni. */
    *x = m->joy_cx + dx * pm_x_da_corto(m, d);
    *y = m->joy_cy + dy * pm_y_da_corto(m, d);
}

/* Come sarebbe joy_qualcuno_premuto DOPO aver scritto joy_premuto[i] a
 * "premuto", ma calcolato SENZA ancora scriverlo. mappa_tasto lo usa per
 * sapere quante azioni servira' emettere prima di controllare az_max: se si
 * mutasse lo stato prima e ci si accorgesse solo dopo che az_max non basta,
 * la funzione tornerebbe zero azioni con m->joy_premuto (e a volte joy_giu)
 * gia' cambiati, e la chiamata successiva vedrebbe uno stato che il
 * chiamante non ha mai osservato -- il dito incollato o il BEGIN spurio
 * descritti nel rilievo. */
static bool joy_qualcuno_premuto_dopo(const Mappa *m, int i, bool premuto)
{
    int k;

    if (premuto) return true;
    for (k = 0; k < 4; k++) {
        if (k != i && m->joy_premuto[k]) return true;
    }
    return false;
}

int mappa_tasto(Mappa *m, int vk, bool premuto, bool ripetizione,
                MappaAzione *az, int az_max)
{
    int n = 0, i;

    /* Windows manda WM_KEYDOWN a raffica mentre il tasto e' giu'. Senza questa
     * riga un tocco mantenuto diventerebbe una raffica di BEGIN. */
    if (ripetizione) {
        return 0;
    }

    if (m->joy_c_e) {
        for (i = 0; i < 4; i++) {
            bool giu_dopo;
            int necessarie;

            if (m->joy_vk[i] != vk) continue;
            if (m->joy_premuto[i] == premuto) return 0;  /* niente e' cambiato */

            /* az_max si controlla QUI, prima di toccare m->joy_premuto: il
             * ramo dei tocchi qui sotto lo fa gia' cosi', e su questo ramo
             * l'ordine sbagliato e' il modo in cui un rilascio rifiutato
             * lascia joy_giu vero per sempre (nessun END raggiunge mai il
             * chiamante) e una pressione rifiutata fa apparire il tasto
             * successivo come "il primo", con un secondo BEGIN spurio. */
            giu_dopo = joy_qualcuno_premuto_dopo(m, i, premuto);
            necessarie = (!m->joy_giu && giu_dopo) ? 2 : 1;
            if (az_max < necessarie) return 0;

            m->joy_premuto[i] = premuto;

            if (!m->joy_giu && giu_dopo) {
                az[n].tipo = MAPPA_AZ_BEGIN;
                az[n].id = MAPPA_ID_JOYSTICK;
                az[n].x_pm = m->joy_cx;
                az[n].y_pm = m->joy_cy;
                n++;
                az[n].tipo = MAPPA_AZ_UPDATE;
                az[n].id = MAPPA_ID_JOYSTICK;
                joy_posizione(m, &az[n].x_pm, &az[n].y_pm);
                n++;
                m->joy_giu = true;
            } else if (m->joy_giu && !giu_dopo) {
                az[n].tipo = MAPPA_AZ_END;
                az[n].id = MAPPA_ID_JOYSTICK;
                n++;
                m->joy_giu = false;
            } else if (m->joy_giu) {
                az[n].tipo = MAPPA_AZ_UPDATE;
                az[n].id = MAPPA_ID_JOYSTICK;
                joy_posizione(m, &az[n].x_pm, &az[n].y_pm);
                n++;
            }
            return n;
        }
    }

    for (i = 0; i < m->n_tocchi; i++) {
        if (m->tocchi[i].vk != vk) continue;
        if (m->tocchi[i].giu == premuto) return 0;
        if (az_max < 1) return 0;

        m->tocchi[i].giu = premuto;
        az[0].tipo = premuto ? MAPPA_AZ_BEGIN : MAPPA_AZ_END;
        az[0].id = MAPPA_ID_TOCCO(i);
        az[0].x_pm = m->tocchi[i].x_pm;
        az[0].y_pm = m->tocchi[i].y_pm;
        return 1;
    }

    return 0;   /* non mappato: il chiamante lo manda al guest come tasto */
}

bool mappa_tasto_e_mio(const Mappa *m, int vk)
{
    int i;

    if (m->joy_c_e) {
        for (i = 0; i < 4; i++) {
            if (m->joy_vk[i] == vk) return true;
        }
    }
    for (i = 0; i < m->n_tocchi; i++) {
        if (m->tocchi[i].vk == vk) return true;
    }
    return false;
}

int mappa_rilascia_tutto(Mappa *m, MappaAzione *az, int az_max)
{
    int n = 0, i;

    if (m->joy_giu && n < az_max) {
        az[n].tipo = MAPPA_AZ_END;
        az[n].id = MAPPA_ID_JOYSTICK;
        n++;
        m->joy_giu = false;
        for (i = 0; i < 4; i++) m->joy_premuto[i] = false;
    }
    for (i = 0; i < m->n_tocchi && n < az_max; i++) {
        if (!m->tocchi[i].giu) continue;
        az[n].tipo = MAPPA_AZ_END;
        az[n].id = MAPPA_ID_TOCCO(i);
        n++;
        m->tocchi[i].giu = false;
    }
    return n;
}

bool mappa_impara_inizia(Mappa *m, MappaImparaTipo tipo)
{
    /* Il joystick usa SEMPRE W, A, S, D (vedi mappa_impara_punto): se uno di
     * questi e' gia' un tocco, il conflitto e' gia' noto qui, prima di
     * qualunque clic. Entrare comunque in modo impara produrrebbe un joystick
     * che mappa_carica rifiuterebbe non appena il file venisse salvato e
     * riletto: si rifiuta l'ingresso in modo impara, non si aspetta il clic
     * per scoprire il conflitto. */
    if (tipo == MAPPA_IMPARA_JOYSTICK) {
        static const int joy_vk[4] = { 'W', 'A', 'S', 'D' };
        int i, j;
        for (i = 0; i < 4; i++) {
            for (j = 0; j < m->n_tocchi; j++) {
                if (m->tocchi[j].vk == joy_vk[i]) return false;
            }
        }
    }
    m->imp_attivo = true;
    m->imp_tipo = tipo;
    m->imp_vk = -1;
    m->imp_ha_centro = false;
    /* Si dimentica il raggio dell'apprendimento PRECEDENTE: senza,
     * mappa_impara_raggio riporterebbe un vecchio numero come se fosse di
     * questo giro, e il registro direbbe "raggio ridotto" su un gesto che non
     * ha ridotto niente. */
    m->imp_r_noto = false;
    return true;
}

bool mappa_impara_raggio(const Mappa *m, int *misurato, int *usato)
{
    if (!m->imp_r_noto) return false;
    if (misurato) *misurato = (int)m->imp_r_misurato;
    if (usato) *usato = (int)m->imp_r_usato;
    return true;
}

bool mappa_impara_annulla(Mappa *m)
{
    bool c_era = m->imp_attivo;
    m->imp_attivo = false;
    return c_era;
}

bool mappa_impara_tasto(Mappa *m, int vk)
{
    int i;

    if (!m->imp_attivo) return false;
    /* Si accetta solo cio' che il lettore sa riscrivere: altrimenti il modo
     * impara produrrebbe un file che poi si rifiuta di leggere. */
    if (!((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') ||
          vk_a_nome(vk) != NULL)) {
        return false;
    }
    /* Se il joystick e' definito, non si puo' assegnare un tocco ai suoi quattro
     * tasti: il file che ne uscirebbe rifiuterebbe poi di leggersi. La prova
     * prova_impara_rifiuta_joystick chiude quel giro. */
    if (m->joy_c_e) {
        for (i = 0; i < 4; i++) {
            if (m->joy_vk[i] == vk) return false;
        }
    }
    m->imp_vk = vk;
    return true;
}

/* Distanza fra due punti, in permille del LATO CORTO -- l'unita' del raggio.
 *
 * I due delta arrivano in unita' DIVERSE: dx e' in permille della larghezza,
 * dy in permille dell'altezza. Sommarne i quadrati cosi' com'erano mescolava
 * mele e pere, e su un'area non quadrata lo stesso gesto visivo dava raggi
 * quasi doppi a seconda della direzione in cui lo si faceva -- uno stick che
 * viene diverso a ogni apprendimento, senza che si capisca perche'. Si
 * riportano prima entrambi all'unita' del raggio. */
static int32_t distanza(const Mappa *m, int32_t ax, int32_t ay,
                        int32_t bx, int32_t by)
{
    /* Il valore assoluto PRIMA della conversione: quelle funzioni arrotondano,
     * e la divisione intera arrotonda verso lo zero, quindi su un delta
     * negativo darebbero un modulo diverso dallo stesso delta positivo. */
    int64_t dx = corto_da_pm_x(m, bx > ax ? bx - ax : ax - bx);
    int64_t dy = corto_da_pm_y(m, by > ay ? by - ay : ay - by);
    int64_t q = dx * dx + dy * dy;
    int32_t r = 0;
    while ((int64_t)(r + 1) * (r + 1) <= q) r++;
    return r;
}

/* Il raggio ridotto a quello piu' grande che tiene il cerchio dentro 0..1000
 * su entrambi gli assi. Zero significa "non c'e' spazio per nessun cerchio":
 * il chiamante (mappa_impara_punto) rifiuta il clic invece di scrivere uno
 * stick morto.
 *
 * mappa_carica RIFIUTA un joystick che esce dall'area (vedi li'), e questo
 * modo non deve poter scrivere un file che poi si rifiuta di leggere: e' la
 * stessa regola gia' applicata ai tasti in mappa_impara_inizia e
 * mappa_impara_tasto. Si RIDUCE invece di rifiutare il clic, perche' un bordo
 * cliccato lontano dal centro e' un gesto ambiguo e il cerchio piu' grande
 * possibile e' la lettura piu' vicina a quello che e' stato chiesto.
 *
 * PER ASSE, come la validazione in mappa_carica e per la stessa ragione: cx e
 * cy sono in permille dei rispettivi assi, il raggio in permille del LATO
 * CORTO. Il margine si misura sugli assi e va riportato all'unita' del raggio
 * PRIMA di prendere il minimo, o su un'area non quadrata si riduce di un
 * fattore sbagliato -- troppo su un asse, troppo poco sull'altro, e nel secondo
 * caso il file scritto viene poi rifiutato dal lettore. */
static int32_t raggio_dentro_area(const Mappa *m, int32_t cx, int32_t cy,
                                  int32_t r)
{
    /* I due margini restano nell'unita' del proprio asse: e' quella in cui
     * mappa_carica li confrontera'. */
    int32_t lim_x = cx < 1000 - cx ? cx : 1000 - cx;
    int32_t lim_y = cy < 1000 - cy ? cy : 1000 - cy;
    int32_t mx, my, massimo;

    /* Un centro fuori da 0..1000 non arriva qui -- mappa_px_a_pm satura e
     * mappa_carica valida -- ma un margine negativo vorrebbe dire "nessun
     * cerchio ci sta", e va trattato come zero invece di finire dentro le
     * conversioni, dove cambierebbe segno all'arrotondamento. */
    if (lim_x < 0) lim_x = 0;
    if (lim_y < 0) lim_y = 0;
    mx = corto_da_pm_x(m, lim_x);
    my = corto_da_pm_y(m, lim_y);
    massimo = mx < my ? mx : my;
    /* corto_da_pm_* ARROTONDA, quindi il candidato puo' sforare di un permille
     * una volta riconvertito. Si scende finche' non ci sta per davvero secondo
     * la conversione ESATTA che usera' mappa_carica: senza, il modo impara
     * tornerebbe a produrre, per un solo permille, un file che il lettore
     * rifiuta -- il guasto che questa funzione esiste per impedire. Il ciclo
     * gira zero o una volta. */
    while (massimo > 0 && (pm_x_da_corto(m, massimo) > lim_x ||
                           pm_y_da_corto(m, massimo) > lim_y)) {
        massimo--;
    }
    return r > massimo ? massimo : r;
}

MappaImparaEsito mappa_impara_punto(Mappa *m, int x_pm, int y_pm)
{
    int i;

    if (!m->imp_attivo) return MAPPA_ESITO_FALLITO;

    if (m->imp_tipo == MAPPA_IMPARA_JOYSTICK) {
        int32_t misurato, usato;

        if (!m->imp_ha_centro) {
            m->imp_cx = x_pm;
            m->imp_cy = y_pm;
            m->imp_ha_centro = true;
            return MAPPA_ESITO_ANCORA;  /* serve il secondo clic */
        }
        misurato = distanza(m, m->imp_cx, m->imp_cy, x_pm, y_pm);
        usato = raggio_dentro_area(m, m->imp_cx, m->imp_cy, misurato);
        /* I due numeri si registrano SEMPRE, anche sul ramo che fallisce qui
         * sotto: sono l'unico modo che il chiamante ha di sapere che il raggio
         * e' stato ridotto, e senza una riga di registro la riduzione e'
         * invisibile. Il caso non degenere e' il piu' frequente: lo stick di un
         * gioco sta in un ANGOLO, li' il massimo consentito e' piccolo, e lo
         * stick funziona ma "tira" a una distanza diversa da quella misurata.
         * Vedi mappa_impara_raggio. */
        m->imp_r_noto = true;
        m->imp_r_misurato = misurato;
        m->imp_r_usato = usato;
        if (usato <= 0) {
            /* UNO STICK CON RAGGIO ZERO E' MORTO: BEGIN e UPDATE cadono nello
             * stesso punto, il file si rilegge senza un errore e il gioco non
             * riceve nessuna direzione. Si RIFIUTA il clic invece di scriverlo:
             * un fallimento che il chiamante deve dire e' meglio di una mappa
             * che si carica e non fa niente. Ci si arriva col centro esattamente
             * su un bordo (nessun raggio ci sta) o coi due clic nello stesso
             * punto (niente da misurare). */
            m->imp_attivo = false;
            return MAPPA_ESITO_FALLITO;
        }
        m->joy_c_e = true;
        m->joy_vk[0] = 'W'; m->joy_vk[1] = 'A';
        m->joy_vk[2] = 'S'; m->joy_vk[3] = 'D';
        m->joy_cx = m->imp_cx;
        m->joy_cy = m->imp_cy;
        m->joy_r = usato;
        m->imp_attivo = false;
        return MAPPA_ESITO_FATTO;
    }

    /* Il tasto non e' ancora arrivato: il modo impara resta APERTO. Non e' un
     * fallimento, ma il chiamante deve dirlo -- un clic che non produce niente
     * e non scrive niente nel registro sembra un guasto. */
    if (m->imp_vk < 0) return MAPPA_ESITO_ANCORA;

    /* SOSTITUISCE se il tasto c'e' gia': due righe per lo stesso tasto sono un
     * errore di lettura, e questo modo non deve poter scrivere un file
     * illeggibile. */
    for (i = 0; i < m->n_tocchi; i++) {
        if (m->tocchi[i].vk == m->imp_vk) {
            m->tocchi[i].x_pm = x_pm;
            m->tocchi[i].y_pm = y_pm;
            m->imp_attivo = false;
            return MAPPA_ESITO_FATTO;
        }
    }
    /* Non c'e' piu' posto: il modo impara si CHIUDE senza scrivere niente. E'
     * l'unico ramo che si chiude fallendo, ed e' quello che un ritorno a due
     * valori confondeva con "serve un altro clic". */
    if (m->n_tocchi >= MAPPA_TOCCHI_MAX) {
        m->imp_attivo = false;
        return MAPPA_ESITO_FALLITO;
    }
    m->tocchi[m->n_tocchi].vk = m->imp_vk;
    m->tocchi[m->n_tocchi].x_pm = x_pm;
    m->tocchi[m->n_tocchi].y_pm = y_pm;
    m->tocchi[m->n_tocchi].giu = false;
    m->n_tocchi++;
    m->imp_attivo = false;
    return MAPPA_ESITO_FATTO;
}

/* Il blocco di commenti in TESTA al file esistente, allocato con malloc e
 * terminato da '\n', oppure NULL se il file non c'e' o non ne ha.
 *
 * Si legge PRIMA che mappa_salva apra il file in scrittura: "wb" lo tronca, e
 * rileggerlo dopo vorrebbe dire rileggerlo vuoto.
 *
 * Si copiano BYTE, senza analizzarli: le fini riga restano quelle che erano.
 *
 * UNA RIGA DI COMMENTO PIU' LUNGA DEL BUFFER CHIUDE LA TESTA e non ci entra.
 * Qui arriverebbe a pezzi, e tenerne solo il primo emetterebbe una riga lunga
 * quanto tutto il buffer e senza fine riga: cioe' esattamente il file che
 * mappa_carica rifiuta con "riga troppo lunga" -- lo stesso modulo che scrive
 * un file che non sa rileggere, e per giunta con la coda del commento persa.
 * Il limite e' quello del LETTORE (vedi mappa_carica: una riga fisica deve
 * starci in questo stesso buffer, fine riga compresa) e vale per i commenti
 * come per i dati, quindi un file che mappa_carica ha accettato non puo'
 * contenerne una: il caso nasce solo da una modifica a mano fatta fra il
 * caricamento e il salvataggio. Limite dichiarato nella spec, sezione 14. */
static char *testa_commenti(const char *percorso)
{
    FILE *f = fopen(percorso, "rb");
    char linea[256];
    long fine = 0;
    char *testo;
    size_t letti;

    if (!f) return NULL;

    while (fgets(linea, sizeof(linea), f)) {
        const char *p = linea;
        bool intera = strchr(linea, '\n') != NULL;

        if (!intera) {
            /* L'assenza di '\n' ha due cause diverse, e vanno distinte come in
             * mappa_carica: l'ULTIMA riga di un file puo' legittimamente non
             * averlo -- e allora e' tutta qui, e un commento va conservato --
             * oppure la riga e' piu' lunga del buffer. Le separa il byte
             * successivo; feof() qui non e' ancora acceso. */
            int prossimo = fgetc(f);

            if (prossimo == EOF) {
                intera = true;
            } else {
                ungetc(prossimo, f);
            }
        }
        while (*p == ' ' || *p == '\t') p++;
        if (!intera) {
            break;      /* riga troppo lunga: la testa finisce PRIMA di questa */
        }
        if (*p == '#') {
            /* ftell e non un contatore nostro: e' dove la lettura e' arrivata
             * davvero, quindi comprende anche le righe vuote gia' scavalcate
             * qui sotto. */
            fine = ftell(f);
            continue;
        }
        /* Una riga vuota non chiude la testa e non la allunga da sola: se
         * dopo arriva un altro commento, ftell la ingloba; se dopo arrivano i
         * dati, resta fuori. */
        if (*p == '\n' || *p == '\r' || *p == '\0') continue;
        break;      /* prima riga di dati: la testa finisce qui */
    }

    if (fine <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    /* +2: il terminatore, e il '\n' da aggiungere se il file finiva senza --
     * senza quello la prima riga di dati si attaccherebbe all'ultimo commento
     * e il file non si rileggerebbe piu'. */
    testo = malloc((size_t)fine + 2);
    if (!testo) {
        fclose(f);
        return NULL;
    }
    letti = fread(testo, 1, (size_t)fine, f);
    fclose(f);
    if (letti && testo[letti - 1] != '\n') {
        testo[letti++] = '\n';
    }
    testo[letti] = '\0';
    return testo;
}

bool mappa_salva(Mappa *m, const char *percorso, char *errore, size_t errore_n)
{
    /* PRIMA di aprire in scrittura: vedi testa_commenti. */
    char *testa = testa_commenti(percorso);
    FILE *f = fopen(percorso, "wb");
    bool bene;
    int i;

    errore[0] = '\0';
    if (!f) {
        free(testa);
        snprintf(errore, errore_n, "non riesco a scrivere %s", percorso);
        return false;
    }
    /* "wb" e non "w": su Windows "w" tradurrebbe ogni \n in \r\n, e il formato
     * dichiarato ha fine riga LF.
     *
     * I commenti in testa al file che c'era si riemettono tali e quali: senza,
     * il primo "impara un tasto" cancellava in silenzio le sei righe che
     * documentano il formato in runtime/keymaps/generico.txt, e con loro
     * qualunque annotazione dell'utente. L'intestazione fissa serve solo
     * quando non c'era niente da conservare. */
    if (testa) {
        fputs(testa, f);
        free(testa);
    } else {
        fputs("# mappa dei tasti -- scritta dal modo impara\n"
              "# Coordinate in permille dell'area cliente (0..1000), non in pixel.\n"
              "\n", f);
    }
    if (m->joy_c_e) {
        fprintf(f, "joystick %c%c%c%c %d %d %d\n",
                m->joy_vk[0], m->joy_vk[1], m->joy_vk[2], m->joy_vk[3],
                m->joy_cx, m->joy_cy, m->joy_r);
    }
    for (i = 0; i < m->n_tocchi; i++) {
        const char *nome = vk_a_nome(m->tocchi[i].vk);
        if (nome) {
            fprintf(f, "touch %s %d %d\n", nome,
                    m->tocchi[i].x_pm, m->tocchi[i].y_pm);
        } else {
            fprintf(f, "touch %c %d %d\n", (char)m->tocchi[i].vk,
                    m->tocchi[i].x_pm, m->tocchi[i].y_pm);
        }
    }
    /* SI CONTROLLA CHE LA SCRITTURA SIA ANDATA A BUON FINE, e non basta che
     * fopen sia riuscita. fputs e fprintf non dicono niente: l'errore resta
     * appiccicato al flusso e si legge con ferror. E buona parte dei byte non
     * ha ancora toccato il disco quando si arriva qui -- e' fclose a svuotare
     * il buffer, quindi un disco pieno o una chiavetta staccata falliscono
     * proprio LI', e ignorare il suo esito e' ignorare l'errore piu' probabile.
     *
     * Il "wb" qui sopra ha gia' TRONCATO il file: se si torna true senza
     * guardare, sul disco resta un file mutilato e il registro dice "mappa
     * salvata". Costa piu' di prima che il file conservi anche i commenti
     * dell'utente, che dalla mappa in memoria non si possono ricostruire. */
    bene = (ferror(f) == 0);
    if (fclose(f) != 0) {
        bene = false;
    }
    if (!bene) {
        snprintf(errore, errore_n,
                 "scrittura di %s non riuscita: il file e' rimasto a meta'",
                 percorso);
        return false;
    }
    return true;
}
