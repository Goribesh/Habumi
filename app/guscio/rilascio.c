/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* rilascio.c -- l'orchestrazione del trascinamento.
 *
 * PERCHE' UN MODULO SUO: e' l'unico posto che sa che un rilascio puo' contenere
 * DUE azioni. apk.c sa installare, file.c sa copiare, e nessuno dei due sa
 * dell'altro; finestra.c riceve il messaggio e mostra il dialogo, e non sa cosa
 * sia adb. Qui vive cio' che li tiene insieme: la classificazione, il testo che
 * l'utente legge, e il lucchetto UNICO -- perche' due adb in parallelo sullo
 * stesso dispositivo non danno un errore chiaro, ne danno uno confuso (vedi il
 * commento in apk.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include "guscio.h"

bool rilascio_tetto_superato(int voci, int file_totali)
{
    return voci > RIL_MAX_VOCI || file_totali > RIL_MAX_ALBERO;
}

/* --- Il rilascio vero: classificare, e poi eseguire su un thread --------- */

bool rilascio_prepara(HDROP h, Piano *p)
{
    UINT quante;
    UINT i;
    int voci = 0;

    if (!h || !p) {
        return false;
    }
    memset(p, 0, sizeof(*p));

    quante = DragQueryFileW(h, 0xFFFFFFFF, NULL, 0);
    for (i = 0; i < quante; i++) {
        wchar_t largo[MAX_PATH];
        char stretto[MAX_PATH];
        DWORD attributi;
        bool cartella;

        if (DragQueryFileW(h, i, largo, MAX_PATH) == 0) {
            continue;
        }
        if (!file_nome_trasportabile(largo)) {
            /* Il nome NON si stampa nel registro: stamparlo vorrebbe dire
             * convertirlo, cioe' fare esattamente la conversione che stiamo
             * rifiutando, e la riga mostrerebbe un nome diverso da quello che
             * l'utente vede. Si dice QUALE voce, non come si chiama. */
            registro_riga(REG_GUSCIO, "drop: entry %u has a name this "
                          "channel cannot carry, skipping",
                          (unsigned)(i + 1));
            continue;
        }
        if (WideCharToMultiByte(CP_ACP, 0, largo, -1, stretto, MAX_PATH,
                                NULL, NULL) == 0) {
            registro_riga(REG_GUSCIO, "drop: entry %u does not convert, "
                          "skipping", (unsigned)(i + 1));
            continue;
        }

        voci++;
        if (rilascio_tetto_superato(voci, p->file_totali)) {
            registro_riga(REG_GUSCIO, "drop: more than %d entries, the WHOLE "
                          "drop refused: try again with fewer files",
                          RIL_MAX_VOCI);
            return false;
        }

        attributi = GetFileAttributesW(largo);
        cartella = (attributi != INVALID_FILE_ATTRIBUTES) &&
                   (attributi & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (!cartella && apk_e_apk(stretto)) {
            WIN32_FILE_ATTRIBUTE_DATA dati;

            snprintf(p->apk[p->voci_apk], MAX_PATH, "%s", stretto);
            if (GetFileAttributesExW(largo, GetFileExInfoStandard, &dati)) {
                p->byte_apk[p->voci_apk] =
                    ((long long)dati.nFileSizeHigh << 32) |
                    (long long)dati.nFileSizeLow;
            }
            p->voci_apk++;
            /* I byte di un APK NON entrano in p->byte: quel totale e' cio' che
             * il dialogo annuncia come "copiare N file (X)", e un'app che si
             * installa non e' un file che si copia. Servono solo alla
             * scadenza. */
            continue;
        }

        if (cartella) {
            int dentro = 0;
            long long byte = 0;
            RilAlbero esito = rilascio_percorri(largo,
                                               RIL_MAX_ALBERO - p->file_totali,
                                               &dentro, &byte);

            if (esito == RIL_ALBERO_TETTO) {
                registro_riga(REG_GUSCIO, "drop: more than %d files in the "
                              "dragged folders, the WHOLE drop refused",
                              RIL_MAX_ALBERO);
                return false;
            }
            if (esito == RIL_ALBERO_INCERTO) {
                /* Rifiutare e' l'unica scelta onesta: adb copierebbe piu' di
                 * quanto abbiamo contato, quindi il dialogo dichiarerebbe meno
                 * del vero e la scadenza sarebbe calcolata su un lavoro piu'
                 * piccolo di quello reale. */
                registro_riga(REG_GUSCIO, "drop: the folder could not be "
                              "counted in full (too deep, or a "
                              "subfolder cannot be read): the WHOLE drop "
                              "WHOLE, because copying part of it while announcing the "
                              "resto sarebbe peggio");
                return false;
            }
            p->file_totali += dentro;
            p->byte += byte;
            p->byte_file[p->voci_file] = byte;
            p->file_voce[p->voci_file] = dentro;
        } else {
            WIN32_FILE_ATTRIBUTE_DATA dati;

            if (GetFileAttributesExW(largo, GetFileExInfoStandard, &dati)) {
                long long byte = ((long long)dati.nFileSizeHigh << 32) |
                                 (long long)dati.nFileSizeLow;

                p->byte += byte;
                p->byte_file[p->voci_file] = byte;
            }
            p->file_totali++;
            p->file_voce[p->voci_file] = 1;
        }
        snprintf(p->file[p->voci_file], MAX_PATH, "%s", stretto);
        p->voci_file++;
    }

    if (p->voci_apk == 0 && p->voci_file == 0) {
        return false;
    }
    /* Cartelle che non contengono file (vuote, o con dentro solo altre cartelle):
     * il conteggio e' ZERO e il dialogo non avrebbe niente da chiedere. Senza
     * questa riga il rilascio sarebbe un no-op MUTO, e prima di questo lavoro un
     * rilascio ignorato produceva sempre una riga -- in questo prodotto la
     * diagnosi e' il registro, quindi la proprieta' va conservata. */
    if (p->voci_apk == 0 && p->file_totali == 0) {
        registro_riga(REG_GUSCIO, "drop: the dragged folders contain no "
                                  "files, nothing to copy");
        return false;
    }
    return true;
}

typedef struct {
    const Config *c;
    Piano p;
} RilLavoro;

/* 0 = niente in corso. Interlocked e non un bool perche' il controllo e la presa
 * devono essere UNA operazione: fra un "se e' libero" e un "lo prendo" ci sta un
 * secondo trascinamento. Ed e' UNO per entrambe le azioni: un install e un push
 * in parallelo sono lo stesso caso di due install, e apk.c documenta che due adb
 * in parallelo sullo stesso dispositivo non danno un errore chiaro, ne danno uno
 * confuso. */
static volatile LONG ril_in_corso;

static void esegui_un_push(const Config *c, const char *percorso,
                           long long byte, int file)
{
    char comando[MAX_PATH + 64];
    char uscita[2048];
    char esito[256];
    unsigned scadenza;

    if (!file_comando_push(comando, sizeof(comando), percorso)) {
        registro_riga(REG_GUSCIO, "drop: path too long, skipping %s",
                      percorso);
        return;
    }
    /* La scadenza si commisura al lavoro: adb non stampa il progresso su un tubo
     * (misurato, vedi app/misure/pf-misure-preliminari.md), quindi non c'e' un
     * avanzamento da sorvegliare e il tempo si calcola dai byte E dal numero di
     * file -- una cartella di file piccoli costa per file, non per byte. */
    scadenza = adb_scadenza_trasferimento(byte, file);
    registro_riga(REG_GUSCIO, "drop: copying %s (%u s allowed)", percorso,
                  scadenza / 1000);
    uscita[0] = '\0';
    if (!adb_esegui_a_lungo(c, comando, uscita, sizeof(uscita), scadenza)) {
        /* Fallito. NON si tenta di ripulire: MISURATO, un adb
         * ucciso a meta' non lascia alcun file nel guest, quindi non c'e' niente
         * da togliere. Gli altri modi di fallimento non sono stati misurati, e
         * per questo il prodotto NON promette di aver ripulito: dice cosa ha
         * risposto adb, e basta. */
        if (adb_ultima_riga(uscita, esito, sizeof(esito))) {
            registro_riga(REG_GUSCIO, "drop: copy FAILED -- %s", esito);
        } else {
            registro_riga(REG_GUSCIO, "drop: copy of %s FAILED, and adb did "
                          "not say why", percorso);
        }
        return;
    }
    if (adb_ultima_riga(uscita, esito, sizeof(esito))) {
        registro_riga(REG_GUSCIO, "drop: %s", esito);
    } else {
        registro_riga(REG_GUSCIO, "drop: adb said nothing "
                                  "(device not connected?)");
    }
    /* NIENTE richiesta di scansione a MediaStore, e non e' una dimenticanza:
     * MISURATO, un file copiato in /sdcard/Download compare in
     * MediaStore entro quattro secondi DA SE', perche' /sdcard e' montata da
     * MediaProvider via FUSE e ogni scrittura le passa davanti. Un comando in
     * piu' qui costerebbe 100-300 ms per file e nessuno saprebbe che e' inutile.
     * Riserva della misura: e' provato l'ingresso in MediaStore.Files, non che un
     * mp3 vero venga tipizzato nella collezione audio. */
}

static DWORD WINAPI ril_thread(LPVOID arg)
{
    RilLavoro *l = (RilLavoro *)arg;
    int i;

    /* UNA verifica per rilascio, non per file, e non e' prudenza generica.
     *
     * MISURATO sul prodotto vivo: system_server e' morto durante
     * un install (difetto noto e aperto, RuntimeException dentro
     * BinderProxy.transactNative), Android si e' ripreso da se' ma la
     * connessione adb era caduta, e il trascinamento successivo rispondeva
     * "adb: error: connect failed: closed" -- un messaggio su cui l'utente non
     * puo' agire. Il guardiano di rilascio_avvia non bastava: vm_stato() diceva
     * VM_PRONTO, ed era vero, perche' e' lo stato della VM e non quello del
     * canale.
     *
     * adb_pronto RICONNETTE da se' se il primo getprop fallisce (vedi adb.c),
     * quindi qui non serve altro impianto: si riusa cio' che e' gia' provato.
     * Sta sul thread e non nel wndproc perche' costa 100-600 ms. */
    if (!adb_pronto(l->c)) {
        registro_riga(REG_GUSCIO, "drop: the guest is not answering adb, "
                      "nothing copied. If Android has just recovered from a "
                      "crash, try again in a few seconds");
        free(l);
        InterlockedExchange(&ril_in_corso, 0);
        return 0;
    }

    /* Install PRIMA: se si trascina un'app e i suoi dati, quello e' l'ordine
     * giusto, e un'app senza dati e' uno stato che si capisce mentre dei dati
     * senza l'app no. */
    for (i = 0; i < l->p.voci_apk; i++) {
        char comando[MAX_PATH + 32];
        char uscita[2048];
        char esito[256];

        if (!apk_comando(comando, sizeof(comando), l->p.apk[i])) {
            registro_riga(REG_GUSCIO, "drop: path too long, skipping "
                          "%s", l->p.apk[i]);
            continue;
        }
        registro_riga(REG_GUSCIO, "drop: installing %s", l->p.apk[i]);
        uscita[0] = '\0';
        /* A lungo e non grezzo: un APK grosso sfonda i 20 s assoluti e veniva
         * ucciso a meta' installazione -- difetto latente del trascinamento di
         * prima, che si chiude qui.
         *
         * MA LA FORMULA E' MISURATA SU push, NON SU install, e va detto: il tempo
         * di un'installazione e' dominato da verifica e dexopt, che non stanno nei
         * byte dell'APK. I due termini della scadenza (byte e numero di file)
         * restano una sovrastima ragionevole -- 20 s di base piu' il tempo di
         * trasferire l'APK -- ma nessuno ha misurato quanto costi dexopt su questa
         * macchina. Se un'installazione venisse uccisa a meta', questo commento e'
         * il posto dove si vede che quel numero non era suo. */
        adb_esegui_a_lungo(l->c, comando, uscita, sizeof(uscita),
                           adb_scadenza_trasferimento(l->p.byte_apk[i], 1));
        /* Si guarda l'USCITA e non il codice di ritorno: adb install esce 0 anche
         * quando stampa Failure. */
        if (adb_ultima_riga(uscita, esito, sizeof(esito))) {
            registro_riga(REG_GUSCIO, "drop: %s", esito);
        } else {
            registro_riga(REG_GUSCIO, "drop: adb said nothing "
                                      "(device not connected?)");
        }
    }

    for (i = 0; i < l->p.voci_file; i++) {
        esegui_un_push(l->c, l->p.file[i], l->p.byte_file[i],
                       l->p.file_voce[i]);
    }

    free(l);
    InterlockedExchange(&ril_in_corso, 0);
    return 0;
}

bool rilascio_avvia(const Config *c, const Piano *p, VmStato stato)
{
    RilLavoro *l;
    HANDLE t;

    if (!c || !p || (p->voci_apk == 0 && p->voci_file == 0)) {
        return false;
    }
    /* IL GUARDIANO. Prima di questo controllo, trascinare durante l'avvio
     * lanciava adb verso un dispositivo che non c'e' e l'utente leggeva un
     * "device not found" che non gli diceva di riprovare fra venti secondi. Lo
     * stato ARRIVA: questo modulo non chiama vm_stato(), cosi' non dipende dalla
     * macchina a stati e il guardiano si prova senza linkarla. */
    if (stato != VM_PRONTO) {
        registro_riga(REG_GUSCIO, "drop: Android is not ready yet, "
                                  "try again when the phase says ready");
        return false;
    }
    if (InterlockedCompareExchange(&ril_in_corso, 1, 0) != 0) {
        registro_riga(REG_GUSCIO, "drop: a transfer is already in "
                                  "progress");
        return false;
    }
    l = (RilLavoro *)calloc(1, sizeof(*l));
    if (!l) {
        InterlockedExchange(&ril_in_corso, 0);
        return false;
    }
    l->c = c;
    /* Il Piano si COPIA: quello del chiamante vive sulla pila del wndproc, che
     * muore appena il messaggio e' servito -- cioe' quasi certamente prima che
     * adb risponda. */
    l->p = *p;
    t = CreateThread(NULL, 0, ril_thread, l, 0, NULL);
    if (!t) {
        free(l);
        InterlockedExchange(&ril_in_corso, 0);
        registro_riga(REG_GUSCIO, "drop: could not start the "
                                  "thread");
        return false;
    }
    /* Si chiude subito la maniglia: il thread continua e nessuno lo aspetta.
     * Tenerla aperta senza mai aspettarla sarebbe una perdita silenziosa. */
    CloseHandle(t);
    return true;
}

/* Profondita' massima della ricorsione. Un albero piu' profondo di cosi' non si
 * percorre: la ricorsione vive sullo stack del wndproc, e MAX_PATH mette un
 * limite pratico molto prima. */
#define RIL_PROFONDITA 32

/* PERCHE' IL CONTROLLO DI _snwprintf E' "n < 0 || n >= MAX_PATH" E NON SOLO
 * "n < 0". VERIFICATO su questa toolchain con un programma di prova: se l'uscita
 * riempie ESATTAMENTE il buffer, _snwprintf ritorna count (non un negativo) e NON
 * scrive il terminatore. Col solo controllo del negativo, un percorso di
 * esattamente MAX_PATH caratteri passava non terminato, e la ricorsione lo
 * rileggeva andando oltre il buffer: percorso spazzatura, FindFirstFileW che
 * fallisce, e albero contato per difetto in silenzio. */
static bool scrivi_percorso(wchar_t *dest, const wchar_t *fmt,
                            const wchar_t *a, const wchar_t *b)
{
    int n = b ? _snwprintf(dest, MAX_PATH, fmt, a, b)
              : _snwprintf(dest, MAX_PATH, fmt, a);

    if (n < 0 || n >= MAX_PATH) {
        dest[0] = L'\0';
        return false;
    }
    return true;
}

/* Il peggiore fra due esiti: INCERTO batte TETTO, che batte COMPLETO. Serve
 * perche' un ramo incerto non deve essere nascosto da un ramo che ha solo
 * sfondato il tetto. */
static RilAlbero peggiore(RilAlbero a, RilAlbero b)
{
    if (a == RIL_ALBERO_INCERTO || b == RIL_ALBERO_INCERTO) {
        return RIL_ALBERO_INCERTO;
    }
    if (a == RIL_ALBERO_TETTO || b == RIL_ALBERO_TETTO) {
        return RIL_ALBERO_TETTO;
    }
    return RIL_ALBERO_COMPLETO;
}

static RilAlbero percorri_da(const wchar_t *cartella, int tetto, int profondita,
                             int *file, long long *byte)
{
    wchar_t modello[MAX_PATH];
    WIN32_FIND_DATAW trovato;
    HANDLE h;
    RilAlbero esito = RIL_ALBERO_COMPLETO;

    if (profondita > RIL_PROFONDITA) {
        /* Non "zero file": i file piu' sotto esistono e adb li copierebbe. */
        return RIL_ALBERO_INCERTO;
    }
    if (!scrivi_percorso(modello, L"%s\\*", cartella, NULL)) {
        return RIL_ALBERO_INCERTO;
    }
    h = FindFirstFileW(modello, &trovato);
    if (h == INVALID_HANDLE_VALUE) {
        /* Cartella inesistente o non leggibile. Una cartella VUOTA non finisce
         * qui: con "\*" l'enumerazione trova comunque "." e "..". Quindi questo
         * ramo significa "non si e' potuto contare", non "non c'era niente". */
        return RIL_ALBERO_INCERTO;
    }
    do {
        wchar_t figlio[MAX_PATH];

        if (wcscmp(trovato.cFileName, L".") == 0 ||
            wcscmp(trovato.cFileName, L"..") == 0) {
            continue;
        }
        /* I collegamenti si saltano: una giunzione che punta a un antenato
         * farebbe girare questa ricorsione per sempre. Si REGISTRA, perche' cosa
         * faccia adb push con un reparse point non e' misurato: se lo seguisse,
         * copierebbe piu' di quanto abbiamo dichiarato. */
        if (trovato.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            registro_riga(REG_GUSCIO, "drop: skipping a link inside the "
                          "folder. If adb followed it, it would copy more than "
                          "the dialog announced");
            continue;
        }
        if (!scrivi_percorso(figlio, L"%s\\%s", cartella, trovato.cFileName)) {
            /* Saltare in silenzio vorrebbe dire contare per difetto. */
            esito = RIL_ALBERO_INCERTO;
            break;
        }
        if (trovato.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RilAlbero sotto = percorri_da(figlio, tetto, profondita + 1, file,
                                          byte);

            if (sotto != RIL_ALBERO_COMPLETO) {
                esito = peggiore(esito, sotto);
                break;
            }
            continue;
        }
        (*file)++;
        *byte += ((long long)trovato.nFileSizeHigh << 32) |
                 (long long)trovato.nFileSizeLow;
        if (*file > tetto) {
            esito = peggiore(esito, RIL_ALBERO_TETTO);
            break;
        }
    } while (FindNextFileW(h, &trovato));
    FindClose(h);
    return esito;
}

RilAlbero rilascio_percorri(const wchar_t *cartella, int tetto, int *file,
                            long long *byte)
{
    if (!file || !byte) {
        return RIL_ALBERO_INCERTO;
    }
    *file = 0;
    *byte = 0;
    if (!cartella) {
        return RIL_ALBERO_INCERTO;
    }
    return percorri_da(cartella, tetto, 0, file, byte);
}

int rilascio_taglia(char *dest, int max, long long byte)
{
    static const char *unita[] = { "KB", "MB", "GB", "TB" };
    long long soglia = 1024;
    int i;
    int n;

    if (!dest || max <= 0) {
        return 0;
    }
    dest[0] = '\0';
    if (byte < 0) {
        byte = 0;
    }
    if (byte < 1024) {
        n = snprintf(dest, (size_t)max, "%lld byte", byte);
        if (n < 0 || n >= max) {
            dest[0] = '\0';
            return 0;
        }
        return n;
    }
    /* Si sale finche' il valore sta sotto 1024 nell'unita' corrente. Una cifra
     * decimale sola: chi legge deve decidere se e' tanto, non fare i conti. */
    for (i = 0; i < 3; i++) {
        if (byte < soglia * 1024) {
            break;
        }
        soglia *= 1024;
    }
    n = snprintf(dest, (size_t)max, "%lld,%lld %s", byte / soglia,
                 (byte % soglia) * 10 / soglia, unita[i]);
    if (n < 0 || n >= max) {
        dest[0] = '\0';
        return 0;
    }
    return n;
}

int rilascio_messaggio(char *dest, int max, const Piano *p)
{
    char taglia[32];
    int n;

    if (!dest || max <= 0) {
        return 0;
    }
    dest[0] = '\0';
    if (!p || (p->voci_apk == 0 && p->file_totali == 0)) {
        /* Un dialogo che chiede il permesso di non fare niente non si mostra. */
        return 0;
    }
    rilascio_taglia(taglia, sizeof(taglia), p->byte);

    /* "file" in italiano non cambia al plurale, e non e' una dimenticanza: chi
     * passasse a "files" scriverebbe in inglese in un prodotto che parla
     * italiano. Solo "applicazione" ha il plurale. */
    if (p->file_totali == 0) {
        n = snprintf(dest, (size_t)max, "Installare %d %s in Android?",
                     p->voci_apk,
                     p->voci_apk == 1 ? "applicazione" : "applicazioni");
    } else if (p->voci_apk == 0) {
        n = snprintf(dest, (size_t)max,
                     "Copiare %d file (%s) in /sdcard/Download?\n\n%s",
                     p->file_totali, taglia, RIL_AVVISO);
    } else {
        n = snprintf(dest, (size_t)max,
                     "Installare %d %s e copiare %d file (%s) in Android?"
                     "\n\n%s",
                     p->voci_apk,
                     p->voci_apk == 1 ? "applicazione" : "applicazioni",
                     p->file_totali, taglia, RIL_AVVISO);
    }
    if (n < 0 || n >= max) {
        /* Un messaggio troncato chiederebbe il permesso per meno di quello che
         * sta per fare: meglio nessun dialogo, e il chiamante non installa. */
        dest[0] = '\0';
        return 0;
    }
    return n;
}
