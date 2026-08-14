/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* dpi.c -- la consapevolezza del DPI, e la scala che ne segue.
 *
 * PERCHE' ESISTE. Lo schermo di questa macchina e' 2880x1920. Un processo che
 * non dichiara di conoscere il DPI ne vede 1440x960 -- MISURATO, non dedotto:
 * GetSystemMetrics restituisce 1440x960 a un processo non-aware e 2880x1920 a
 * uno per-monitor-v2, sulla stessa macchina nello stesso momento. Windows gli
 * mente, il processo disegna in quello spazio ridotto, e poi il sistema
 * ingrandisce di 2x cio' che ha disegnato. Sono DUE scalature in fila, e la
 * seconda e' quella che sfoca. Dichiarando la consapevolezza la finestra riceve
 * i pixel veri e la scalatura resta una sola.
 *
 * PERCHE' GetProcAddress E NON COLLEGAMENTO DIRETTO. SetProcessDpiAwarenessContext
 * esiste da Windows 10 1703, GetDpiForWindow e GetDpiForSystem da 1607.
 * Chiamarle per collegamento diretto metterebbe tre import di user32.dll
 * nell'eseguibile, e su un Windows che non le ha il caricatore rifiuta il
 * processo INTERO prima che esegua una riga: il ripiego non sarebbe "sfocato",
 * sarebbe "non parte". Risolverle a runtime costa tre GetProcAddress e lascia
 * il prodotto usabile, solo sfocato come prima -- DICHIARANDOLO, vedi
 * dpi_esito. E' la stessa ragione per cui le wgl* si caricano a runtime.
 *
 * PERCHE' NON UN MANIFEST, che sarebbe piu' robusto perche' si applica prima
 * che qualunque codice giri: richiederebbe di toccare la configurazione di
 * compilazione di QEMU per l'altro eseguibile, e qemu/scripts/innesta-winq.sh
 * e' oggi una copia di file piu' una patch di due punti. Un innesto che tocca
 * meno cose si riapplica su una versione nuova di QEMU senza sorprese.
 *
 * PERCHE' PER-MONITOR-V2 E NON SYSTEM-AWARE. Deciso dopo aver visto il prezzo
 * del rinvio: con system-aware il codice della geometria assumerebbe una scala
 * fissa, e innestare il per-monitor dopo costerebbe piu' che farlo ora. Il
 * codice in piu' e' poco -- una costante diversa e un gestore di WM_DPICHANGED
 * per finestra -- perche' entrambe le finestre hanno gia' un percorso di
 * ridimensionamento su cui appoggiarsi. */
#include <stdio.h>
#include <windows.h>
#include "guscio.h"

/* Il DPI di riferimento di Win32: a 96 un pixel logico e' un pixel fisico. */
#define DPI_RIFERIMENTO 96

/* BOOL, non DPI_AWARENESS_CONTEXT: e' SetTHREADDpiAwarenessContext a
 * restituire il contesto precedente. Con il tipo sbagliato l'esito si
 * testerebbe come puntatore, e su AArch64 i 32 bit alti di x0 dopo un
 * ritorno BOOL non sono specificati: un FALSE con spazzatura in alto
 * prenderebbe il ramo del successo e il log direbbe il contrario del vero. */
typedef BOOL (WINAPI *FnImpostaContesto)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *FnDpiFinestra)(HWND);
typedef UINT (WINAPI *FnDpiSistema)(void);

/* Il valore iniziale non e' una stringa vuota apposta: se qualcuno versasse
 * dpi_esito() nel registro senza aver chiamato dpi_dichiara(), la riga dice
 * quale e' l'errore invece di non dire niente. */
static char dpi_riga[256] =
    "DPI: dpi_dichiara() was never called, the window will stay blurry";
static FnDpiFinestra dpi_fn_finestra;
static FnDpiSistema dpi_fn_sistema;

void dpi_dichiara(void)
{
    HMODULE u = GetModuleHandleA("user32.dll");
    FnImpostaContesto imposta;

    if (!u) {
        snprintf(dpi_riga, sizeof(dpi_riga),
                 "DPI: user32.dll not reachable (%lu), continuing WITHOUT "
                 "awareness: the window stays blurry as before",
                 GetLastError());
        return;
    }

    /* Le due letture si risolvono comunque, anche se la dichiarazione
     * fallisse: senza consapevolezza GetDpiForWindow risponde 96, che e' il
     * valore giusto per un processo a cui Windows sta mentendo. Cosi' il resto
     * del codice non ha un secondo caso da trattare. */
    dpi_fn_finestra = (FnDpiFinestra)(void *)GetProcAddress(u,
                                                           "GetDpiForWindow");
    dpi_fn_sistema = (FnDpiSistema)(void *)GetProcAddress(u,
                                                          "GetDpiForSystem");

    imposta = (FnImpostaContesto)(void *)
        GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (!imposta) {
        snprintf(dpi_riga, sizeof(dpi_riga),
                 "DPI: SetProcessDpiAwarenessContext is not there (Windows 10 "
                 "1703), continuing WITHOUT awareness: the product stays "
                 "usable, just blurry as before");
        return;
    }
    if (imposta(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        snprintf(dpi_riga, sizeof(dpi_riga),
                 "DPI: declared per-monitor-v2, the window will get physical "
                 "pixels and there is a single scaling step");
        return;
    }
    /* IL RIPIEGO, e quanto vale davvero. SetProcessDpiAwarenessContext e il
     * contesto v2 sono arrivati insieme in Windows 10 1703, quindi questo ramo
     * non copre una versione di Windows piu' vecchia -- copre il caso in cui la
     * funzione ci sia e rifiuti QUEL contesto, che e' l'unico modo in cui puo'
     * fallire senza che la funzione manchi. Costa tre righe e lascia il
     * prodotto nitido invece che sfocato, quindi si tiene; non si finge che sia
     * una compatibilita' che non e'. */
    if (imposta(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) {
        snprintf(dpi_riga, sizeof(dpi_riga),
                 "DPI: per-monitor-v2 rifiutato, dichiarato system-aware come "
                 "ripiego: i pixel sono veri, ma un passaggio su uno schermo con "
                 "scala diversa non sara' seguito");
        return;
    }
    {
        DWORD err = GetLastError();

        /* ERROR_ACCESS_DENIED (5): SetProcessDpiAwarenessContext fallisce cosi'
         * quando la consapevolezza del processo e' GIA' stata impostata da
         * qualcun altro (in questo eseguibile, non c'e' un "altro" oggi, ma la
         * funzione non lo sa e restituisce lo stesso errore). In quel caso il
         * processo E' consapevole, e questo messaggio non va letto come
         * "sfocato": lo sarebbe solo se l'errore fosse un altro. */
        if (err == ERROR_ACCESS_DENIED) {
            snprintf(dpi_riga, sizeof(dpi_riga),
                     "DPI: no awareness accepted (%lu): "
                     "ERROR_ACCESS_DENIED means it was already set by "
                     "someone else, so the process IS aware, not "
                     "blurry", err);
        } else {
            snprintf(dpi_riga, sizeof(dpi_riga),
                     "DPI: no awareness accepted (%lu), continuing "
                     "without it: the product stays usable, just blurry as "
                     "before", err);
        }
    }
}

const char *dpi_esito(void)
{
    return dpi_riga;
}

UINT dpi_di_finestra(HWND h)
{
    UINT d = dpi_fn_finestra ? dpi_fn_finestra(h) : 0;

    /* Zero e' cio' che GetDpiForWindow ritorna per un handle non valido. Si
     * ricade sul riferimento invece di propagarlo: uno zero moltiplicato per
     * ogni costante di layout darebbe controlli di dimensione nulla, cioe' una
     * finestra vuota -- un sintomo che somiglia a un guasto del disegno e non a
     * una divisione. */
    return d ? d : DPI_RIFERIMENTO;
}

UINT dpi_di_sistema(void)
{
    UINT d = dpi_fn_sistema ? dpi_fn_sistema() : 0;

    return d ? d : DPI_RIFERIMENTO;
}

int dpi_scala(int valore, UINT dpi)
{
    if (dpi == 0) {
        dpi = DPI_RIFERIMENTO;
    }
    /* + DPI_RIFERIMENTO / 2 e' l'arrotondamento al piu' vicino, non un
     * ornamento: a 150% un troncamento farebbe di 12 un 17 invece di 18 e di
     * 100 un 149 invece di 150, e un pixel perso per controllo diventa una
     * colonna storta. long long non serve a evitare uno straripamento --
     * valore * dpi resta ben dentro un int a 32 bit anche nel caso peggiore di
     * questo prodotto (8192 * 384, la larghezza massima per una scala 400%) --
     * costa zero e rende il calcolo indipendente dalla larghezza di int, nel
     * caso qualcuno lo riusi altrove con numeri piu' grandi. */
    return (int)(((long long)valore * dpi + DPI_RIFERIMENTO / 2) /
                 DPI_RIFERIMENTO);
}

void dpi_applica_scala_guest(Config *c, UINT dpi)
{
    int w, h;

    if (!c->scala_guest) {
        return;
    }
    w = dpi_scala(c->larghezza, dpi);
    h = dpi_scala(c->altezza, dpi);

    /* 8192 e' il tetto che config_carica impone a larghezza e altezza, e quello
     * che winq_tubo_esegui fa rispettare al comando di risoluzione. Si rifiuta
     * l'aumento INTERO invece di limitare la dimensione che sfonda: limitarne
     * una sola cambierebbe le proporzioni del guest, e Android disegnerebbe in
     * un rapporto che non e' quello dello schermo -- un guasto peggiore, e piu'
     * difficile da riconoscere, di "la chiave non ha fatto niente". */
    if (w > 8192 || h > 8192) {
        registro_riga(REG_GUSCIO, "guest_scale=yes ignored: %dx%d at scale "
                      "%u/96 would be %dx%d, and 8192 is the per-side maximum. "
                      "Staying at %dx%d.", c->larghezza, c->altezza,
                      (unsigned)dpi, w, h, c->larghezza, c->altezza);
        return;
    }
    if (w == c->larghezza && h == c->altezza) {
        return;   /* schermo non scalato: niente da dire */
    }
    registro_riga(REG_GUSCIO, "guest_scale=yes: the guest will render %dx%d instead "
                  "of %dx%d (scale %u/96). The cost is MEASURED (see "
                  "the key's own comment), and the "
                  "density Android derives from the EDID stays the one of "
                  "the old resolution until the density key corrects it "
                  "(see VM_PRONTO in main.c).",
                  w, h, c->larghezza, c->altezza, (unsigned)dpi);
    c->larghezza = w;
    c->altezza = h;
}

void dpi_ruota(int w, int h, bool verso_orizzontale, int *bw, int *bh)
{
    int lungo;
    int corto;

    if (!bw || !bh) {
        return;
    }
    *bw = 0;
    *bh = 0;
    if (w <= 0 || h <= 0) {
        /* Una risoluzione assurda non si manda: QEMU la rifiuterebbe, oppure --
         * peggio -- l'accetterebbe. */
        return;
    }
    lungo = w > h ? w : h;
    corto = w > h ? h : w;
    /* IL BERSAGLIO DIPENDE SOLO DAL VERSO, non da come e' girato adesso: e' la
     * differenza fra questa funzione e lo scambio che c'era prima. Uno schermo
     * quadrato da' lo stesso bersaglio nei due versi, e non e' un errore. */
    *bw = verso_orizzontale ? lungo : corto;
    *bh = verso_orizzontale ? corto : lungo;
}
