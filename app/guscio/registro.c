/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* registro.c -- il pozzo del log.
 *
 * PERCHE' E' UN MODULO SUO e non parte di finestra.c: tre sorgenti scrivono qui
 * -- la seriale del guest, lo stderr di QEMU, le decisioni del guscio -- e una
 * sola le mostra. Tenendoli distinti si verifica che la RACCOLTA funzioni senza
 * aprire una finestra, e in questo progetto ogni prova con la VM costa due
 * minuti di avvio.
 *
 * PERCHE' UN ANELLO E NON UNA LISTA CHE CRESCE: una sessione lunga produce
 * decine di migliaia di righe di seriale, e nessuno le rileggera'. L'anello
 * tiene le ultime e non chiede memoria a runtime, quindi non ha un modo di
 * fallire.
 *
 * PERCHE' registro_nuove CONSUMA: la finestra la chiama su un timer. Se ridesse
 * tutto a ogni chiamata, il pannello duplicherebbe il proprio contenuto ogni
 * volta -- un difetto che si vede solo dopo qualche secondo di uso, cioe' tardi. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

#define REG_RIGHE     4096
#define REG_LUNGHEZZA 256

/* Dove finisce la copia su file. Accanto alla seriale del guest, che il prodotto
 * scrive gia' li': chi diagnostica vuole le due cose nella stessa cartella.
 * Percorso relativo perche' il guscio gira con la cartella corrente sulla radice
 * del progetto -- lo stesso presupposto delle immagini sulla riga di comando di
 * QEMU (vedi vm_argomenti). Si sovrascrive GUSCIO_REGISTRO_FILE per spostarlo. */
#define REG_FILE_DEFAULT "guest/logs/registro-guscio.log"

static char reg_anello[REG_RIGHE][REG_LUNGHEZZA];
static int reg_scritte;              /* quante righe in tutto sono state scritte */
static int reg_consegnate;           /* fino a quale e' arrivato chi legge */
static CRITICAL_SECTION reg_lucchetto;
static bool reg_aperto;

/* LA COPIA SU FILE ESISTE PERCHE' IL PANNELLO MUORE COL PROCESSO.
 *
 * MISURATO : durante una prova su Venus, QEMU e' morto. La ragione
 * era nel suo stderr, che il guscio raccoglie qui e mostra nel pannello -- e il
 * pannello si e' chiuso insieme al processo. L'informazione era stata catturata e
 * perduta nello stesso istante, e per riaverla serviva un altro avvio.
 *
 * PERCHE' SI SVUOTA A OGNI RIGA, che e' il punto e non un dettaglio: un file
 * bufferizzato perde proprio le ULTIME righe quando il processo muore male --
 * cioe' quelle che dicono perche'. Il costo e' una fflush per riga di log; il
 * guadagno e' che il caso peggiore diventa diagnosticabile. Senza lo svuotamento
 * questo file darebbe la stessa illusione di prima, con piu' codice.
 *
 * PERCHE' UN TEMPO RELATIVO invece dell'ora: chi legge dopo un crash vuole sapere
 * QUANDO nel corso dell'avvio, per confrontarlo con la seriale del guest, che pure
 * conta dal proprio inizio. L'ora del giorno non si allinea a niente. */
static FILE *reg_file;
static ULONGLONG reg_avvio_ms;

/* Crea le cartelle che portano a "percorso", se non ci sono gia'.
 *
 * Serve perche' nell'albero di sviluppo guest/logs esiste da sempre -- ce
 * l'hanno messa le build -- mentre nel PACCHETTO DI RILASCIO no: contiene solo
 * i file elencati in contenuto.txt, e una cartella di registri vuota non e' un
 * file. Misurato scompattando la release in una cartella nuova: la fopen piu'
 * sotto falliva, e il prodotto girava SENZA REGISTRO. Il registro e' proprio il
 * file che si chiede a chi segnala un problema, ed e' quello con cui abbiamo
 * diagnosticato il doppio clic muto -- perderlo sull'installazione di un altro
 * e' perderlo esattamente dove serve.
 *
 * CreateDirectoryA non crea i genitori: su "guest/logs" con "guest" assente
 * fallisce con ERROR_PATH_NOT_FOUND. Quindi si cammina il percorso pezzo per
 * pezzo. Anche archivio_ruota ne beneficia: la sua "guest/logs/archivio"
 * falliva per la stessa ragione.
 *
 * Non ritorna niente e non si lamenta: siamo dentro registro_apri, il registro
 * non e' ancora aperto e non puo' lamentarsi di se stesso. Se la creazione non
 * riesce, la fopen fallira' come faceva prima e l'avvio prosegue lo stesso --
 * un aiuto alla diagnosi che impedisse di avviare sarebbe peggio della sua
 * assenza, che e' la stessa regola gia' scritta qui sotto. */
static void reg_crea_cartelle(const char *percorso)
{
    char copia[MAX_PATH];
    size_t i;

    if (!percorso || !*percorso) {
        return;
    }
    if (strlen(percorso) >= sizeof(copia)) {
        return;
    }
    strcpy(copia, percorso);

    /* Si parte da 1: un percorso assoluto comincia con "\" o "/", e provare a
     * creare la radice non ha senso. L'ultimo pezzo e' il NOME DEL FILE e non
     * va creato come cartella, percio' il ciclo non guarda il terminatore. */
    for (i = 1; copia[i] != '\0'; i++) {
        if (copia[i] == '/' || copia[i] == '\\') {
            char segno = copia[i];

            copia[i] = '\0';
            /* "C:" da solo non e' una cartella creabile e non serve crearla. */
            if (!(i == 2 && copia[1] == ':')) {
                CreateDirectoryA(copia, NULL);
            }
            copia[i] = segno;
        }
    }
}

/* Il lucchetto non e' prudenza: il thread che legge lo stderr di QEMU scrive
 * qui mentre il thread principale legge per mostrare. E' l'unico punto del
 * guscio in cui due thread si incontrano, e per questo e' l'unico lucchetto. */
void registro_apri(void)
{
    if (reg_aperto) {
        return;
    }
    InitializeCriticalSection(&reg_lucchetto);
    reg_scritte = 0;
    reg_consegnate = 0;
    reg_avvio_ms = GetTickCount64();

    /* "w" e non "a": un registro che si accumula fra le sessioni fa prendere per
     * nuovo un messaggio di due avvii prima -- e' la trappola dei "log vecchi che
     * ingannano" gia' documentata in qemu/HOST-WINDOW.md, che qui si evita per
     * costruzione invece di raccomandare all'utente di guardare l'mtime.
     *
     * Se non si apre non si fa NIENTE, e in particolare non si registra la cosa:
     * il registro e' il posto dove si registra, e non puo' lamentarsi di se stesso.
     * Un aiuto alla diagnosi che impedisse di avviare il prodotto sarebbe peggio
     * della sua assenza. */
    {
        const char *percorso = getenv("GUSCIO_REGISTRO_FILE");
        SYSTEMTIME ora;

        if (!percorso || !*percorso) {
            percorso = REG_FILE_DEFAULT;
        }
        /* Il registro del giro precedente si SPOSTA prima di riaprire: la "w" qui
         * sopra lo distruggeva, e con esso la sola traccia su disco di un crash
         * raro. Il file attivo resta pulito come prima -- cambia solo che quello
         * di prima sopravvive.
         *
         * L'esito NON si registra, e non e' una dimenticanza: siamo dentro
         * registro_apri, il registro non e' ancora aperto, e il registro non puo'
         * lamentarsi di se stesso. Se l'archiviazione non riesce, il file attivo
         * viene cancellato comunque e l'avvio prosegue. */
        /* Prima di tutto il resto: senza le cartelle, sia l'archiviazione sia
         * la fopen falliscono in silenzio (vedi reg_crea_cartelle sopra). */
        reg_crea_cartelle(percorso);
        GetLocalTime(&ora);
        archivio_ruota(percorso, ARCH_CARTELLA, "registro-guscio", ARCH_QUANTI,
                       &ora);
        reg_file = fopen(percorso, "w");
        /* Il registro resta aperto per tutta la sessione, ed e' il primo file
         * che archivio_ruota deve poter spostare al prossimo avvio. Se QEMU o
         * il server di adb ne ereditassero una copia, quella rotazione
         * fallirebbe e la sessione nuova scriverebbe in coda a quella vecchia.
         * Vedi archivio_non_ereditare in guscio.h. */
        archivio_non_ereditare(reg_file);
    }
    reg_aperto = true;
}

void registro_chiudi(void)
{
    if (!reg_aperto) {
        return;
    }
    /* un lavoro successivo aggiunge un thread che legge lo stderr di QEMU e scrive qui
     * tramite registro_riga: puo' essere dentro EnterCriticalSection proprio
     * mentre la finestra chiude. Distruggere una sezione critica posseduta da
     * un altro thread e' comportamento non definito nella API Win32. Si prende
     * il lucchetto, si segna la chiusura DENTRO la sezione critica, si
     * rilascia, e solo dopo si distrugge. */
    EnterCriticalSection(&reg_lucchetto);
    reg_aperto = false;
    /* Il file si chiude DENTRO la sezione critica, per la stessa ragione per cui
     * reg_aperto si segna qui: il thread dello stderr di QEMU puo' essere dentro
     * registro_riga in questo momento, e chiudergli il FILE sotto le mani sarebbe
     * una scrittura su un puntatore liberato. */
    if (reg_file) {
        fclose(reg_file);
        reg_file = NULL;
    }
    LeaveCriticalSection(&reg_lucchetto);
    DeleteCriticalSection(&reg_lucchetto);
}

static const char *reg_prefisso(RegSorgente s)
{
    switch (s) {
    case REG_QEMU:  return "[qemu]  ";
    case REG_GUEST: return "[guest] ";
    default:        return "[shell]";
    }
}

void registro_riga(RegSorgente sorgente, const char *fmt, ...)
{
    va_list ap;
    char *dest;
    int n;

    if (!reg_aperto) {
        return;
    }
    EnterCriticalSection(&reg_lucchetto);
    dest = reg_anello[reg_scritte % REG_RIGHE];
    n = snprintf(dest, REG_LUNGHEZZA, "%s ", reg_prefisso(sorgente));
    if (n < 0 || n >= REG_LUNGHEZZA) {
        n = 0;
    }
    va_start(ap, fmt);
    vsnprintf(dest + n, (size_t)(REG_LUNGHEZZA - n), fmt, ap);
    va_end(ap);
    reg_scritte++;
    /* La copia su file, col tempo trascorso davanti. Sta DENTRO la sezione critica
     * perche' due thread scrivono qui, e fprintf su uno stesso FILE da due thread
     * senza protezione mescola le righe. La fflush e' cio' che rende il file utile
     * quando il processo muore male: vedi il commento sopra reg_file. */
    if (reg_file) {
        fprintf(reg_file, "[%6llu ms] %s\n",
                (unsigned long long)(GetTickCount64() - reg_avvio_ms), dest);
        fflush(reg_file);
    }
    /* Se chi legge e' rimasto troppo indietro le righe piu' vecchie sono state
     * sovrascritte: si sposta il suo segnaposto invece di consegnare spazzatura. */
    if (reg_scritte - reg_consegnate > REG_RIGHE) {
        reg_consegnate = reg_scritte - REG_RIGHE;
    }
    LeaveCriticalSection(&reg_lucchetto);
}

int registro_nuove(char *buf, int max)
{
    int scritti = 0;

    if (!reg_aperto || max <= 1) {
        if (max > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    buf[0] = '\0';
    EnterCriticalSection(&reg_lucchetto);
    while (reg_consegnate < reg_scritte) {
        const char *riga = reg_anello[reg_consegnate % REG_RIGHE];
        int len = (int)strlen(riga);

        /* +2 per "\r\n", +1 per il terminatore. Se non ci sta e si e' gia'
         * copiato qualcosa in questa chiamata, ci si ferma senza consumare:
         * la prossima chiamata la ritrovera' con il buffer libero. */
        if (scritti + len + 3 > max) {
            if (scritti == 0) {
                /* Nulla e' ancora entrato in QUESTA chiamata: fermarsi senza
                 * consumare vorrebbe dire che la prossima chiamata incontra
                 * la stessa riga e la stessa condizione, all'infinito -- uno
                 * stallo silenzioso del pannello, non un ritardo. Si copia
                 * troncata cio' che ci sta (il progresso conta piu' della
                 * riga intera), si CONSUME comunque, e si segna il taglio con
                 * "..." perche' una riga tagliata in silenzio e' un'altra
                 * bugia per chi legge il pannello. */
                int disponibile = max - 1;   /* spazio per il testo, terminatore escluso */

                if (disponibile > 3) {
                    int copiati = disponibile - 3;
                    if (copiati > len) {
                        copiati = len;
                    }
                    memcpy(buf, riga, (size_t)copiati);
                    memcpy(buf + copiati, "...", 3);
                    scritti = copiati + 3;
                } else {
                    /* Buffer troppo piccolo persino per i tre punti: si copia
                     * quel che c'e' spazio, senza finta di ellissi. */
                    memcpy(buf, riga, (size_t)disponibile);
                    scritti = disponibile;
                }
                reg_consegnate++;
            }
            break;
        }
        memcpy(buf + scritti, riga, (size_t)len);
        scritti += len;
        buf[scritti++] = '\r';
        buf[scritti++] = '\n';
        reg_consegnate++;
    }
    buf[scritti] = '\0';
    LeaveCriticalSection(&reg_lucchetto);
    return scritti;
}
