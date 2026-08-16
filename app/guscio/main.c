/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* main.c -- ingresso del guscio: istanza unica, cablaggio, ciclo dei messaggi.
 *
 * MODI SENZA VM, perche' ogni prova con la VM costa due minuti di avvio:
 *   --argomenti  assembla la riga di comando di QEMU, la stampa, esce
 *   --config     stampa la configurazione risolta e l'origine di ogni valore */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"
#include "scarica.h"

#define GUSCIO_MUTEX  "AndroidRuntimeGuscio.istanza"

static Config g_c;

/* L'esito di guscio_radice_imposta (vedi il commento in cima a main() per il
 * perche'), tenuto qui e non locale a main() perche' modo_argomenti e
 * modo_config aprono ANCH'ESSI il registro, ognuno per conto proprio, e
 * ognuno deve poter scrivere la stessa riga senza ripetere il calcolo --
 * calcolato una volta sola, in cima a main(), prima del ramo che sceglie il
 * modo. */
static bool g_radice_ok;
static char g_radice[MAX_PATH];
static char g_radice_provato[MAX_PATH];

/* L'evento con nome che winq segnala quando l'utente chiude la finestra di
 * Android. Vedi il commento dove si crea, in main(), per il perche'. */
static HANDLE g_chiusura;
/* Alzato non appena l'utente chiede di chiudere (vedi WM_APP+1 nel ciclo dei
 * messaggi): un cambio di variante che arrivasse a chiusura gia' iniziata
 * (il thread dello scaricamento non sa che l'utente ha chiuso) non deve
 * riaprire la VM ne' riscrivere config.txt -- vedi variante_richiedi
 * piu' sotto. */
static bool g_chiudendo;

/* Lo stato acceso/spento della mappa dei tasti sta QUI e in nessun altro
 * posto.
 *
 * Se la scorciatoia vivesse in winq e il bottone nel guscio, i due si
 * disallineerebbero al primo uso: il bottone direbbe "spenta" con la mappa
 * accesa. Un solo proprietario, una sola strada per cambiarlo -- ed e' per
 * questo che mappa_accendi/mappa_spegni (sotto) sono l'UNICO punto che
 * scrive mappa_accesa: il bottone, il menu e la scorciatoia passano tutti
 * di li'.
 *
 * RegisterHotKey e non un tasto intercettato nel wndproc, perche' la
 * finestra del guscio NON HA MAI IL FUOCO: il fuoco sta sulla finestra di
 * winq, che e' una finestra di primo livello separata (verificato, nessun
 * SetParent). Una scorciatoia legata al fuoco non scatterebbe mai.
 *
 * Costo dichiarato: Ctrl+Alt+T e' sottratta a tutto Windows, non solo al
 * guest. */
#define ID_HOTKEY_TASTI 1
static bool mappa_accesa;
static char mappa_profilo[MAX_PATH];

/* Il menu dei profili elenca i file .txt dentro runtime/keymaps. I comandi di
 * apprendimento agiscono sempre sul profilo scelto per ULTIMO dal menu,
 * anche se la mappa e' spenta in questo momento: e' il senso di "sul
 * profilo attualmente scelto" nel compito. */
#define MAPPA_CARTELLA          "runtime/keymaps"
#define MAPPA_MENU_MAX          64
#define ID_MENU_SPEGNI          2001
#define ID_MENU_IMPARA_TOCCO    2002
#define ID_MENU_IMPARA_JOYSTICK 2003
#define ID_MENU_PROFILO_BASE    3000
/* I percorsi completi elencati nell'ultimo menu mostrato, indicizzati come
 * (id_scelto - ID_MENU_PROFILO_BASE): TrackPopupMenu e' bloccante sullo
 * stesso thread, quindi la tabella e' valida dalla costruzione del menu
 * alla scelta che lo richiude, e non serve oltre. */
static char mappa_menu_percorsi[MAPPA_MENU_MAX][MAX_PATH];

/* Carica la configurazione e applica la scala del guest, in un posto solo.
 *
 * TRE CHIAMANTI: modo_argomenti, modo_config e main. Prima ognuno ripeteva la
 * costruzione del percorso e config_carica, e con dpi_applica_scala_guest
 * sarebbero diventate tre copie di tre righe. La duplicazione gemella (il
 * percorso di adb.exe) e' gia' stata unificata una volta in
 * guscio_percorso_accanto proprio dopo che due copie erano divergite: non se ne
 * crea una terza forma.
 *
 * La scala si prende da dpi_di_sistema e non da dpi_di_finestra perche' due dei
 * tre chiamanti non aprono nessuna finestra, e in quelli un HWND non esiste. E'
 * anche il valore giusto: la risoluzione del guest si decide una volta, prima
 * che la finestra di Android esista, quindi la densita' dello schermo primario
 * all'avvio e' l'unica informazione disponibile. Su piu' schermi con scale
 * diverse questo e' un limite, ed e' dichiarato in app/GUSCIO.md. */
static void configurazione_risolta(Config *c)
{
    char percorso[MAX_PATH];

    /* Accanto all'ESEGUIBILE, non alla cartella corrente: e' il Bloccante 3
     * della revisione finale. guscio_percorso_accanto non dipende dalla cwd
     * per niente (usa GetModuleFileNameA), quindi legge config.txt
     * anche se la radice del prodotto non fosse stata riconosciuta all'avvio
     * (vedi guscio_radice_imposta in adb.c, chiamata in cima a main()). Le
     * immagini in guest/images/..., invece, sono percorsi relativi: quelle
     * SI' dipendono dalla cwd, ed e' per loro che main() impone la radice
     * come cartella corrente prima di arrivare qui. */
    guscio_percorso_accanto("config.txt", percorso, sizeof(percorso));
    config_carica(c, percorso);
    dpi_applica_scala_guest(c, dpi_di_sistema());
}

/* Scrive nel registro quale radice il prodotto ha scelto per se stesso --
 * o, se guscio_radice_imposta (chiamata in cima a main(), PRIMA di
 * registro_apri) non l'ha verificata, quale percorso ha cercato senza
 * trovarlo. Tre chiamanti, come configurazione_risolta: modo_argomenti,
 * modo_config e main aprono ciascuno il registro per conto proprio, e
 * ognuno chiama questa riga SUBITO DOPO la propria registro_apri(), cosi'
 * l'esito della radice e' la prima cosa scritta -- prima ancora della riga
 * del DPI in main(). */
static void radice_registra(void)
{
    if (g_radice_ok) {
        registro_riga(REG_GUSCIO, "product root: %s", g_radice);
    } else {
        registro_riga(REG_GUSCIO, "product root NOT VERIFIED: "
                      "looked for %s, not found", g_radice_provato);
    }
}

/* Callback di EnumWindows per istanza_unica: confronta la classe della
 * finestra invece di fidarsi di un lookup per nome. Ritorna FALSE (si ferma)
 * appena trovata, TRUE (si continua) altrimenti, com'e' il contratto Win32
 * di EnumWindowsProc. */
static BOOL CALLBACK istanza_trova_finestra(HWND h, LPARAM lp)
{
    char classe[64];

    if (GetClassNameA(h, classe, sizeof(classe)) &&
        !strcmp(classe, "AndroidRuntimeGuscio")) {
        *(HWND *)lp = h;
        return FALSE;
    }
    return TRUE;
}

/* ISTANZA UNICA, e non e' estetica: due QEMU sullo stesso data.img lo
 * CORROMPONO, e succede con un doppio clic. Il mutex con nome e' il modo Win32
 * di dirlo; la finestra della prima istanza si porta in primo piano perche'
 * l'utente ha chiesto di vederla, non di essere ignorato. */
static bool istanza_unica(void)
{
    HANDLE m = CreateMutexA(NULL, TRUE, GUSCIO_MUTEX);

    if (!m) {
        return true;   /* senza mutex si prosegue: peggio non partire */
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND altra = NULL;

        /* NON FindWindowA: la verifica di allora documenta che IN QUESTO
         * STESSO AMBIENTE FindWindowA con questa classe ha ritornato handle 0
         * mentre EnumWindows la trovava -- tanto che chi verificava lo
         * spegnimento ha dovuto riscrivere il proprio script per usare
         * EnumWindows, lasciando pero' il codice di produzione con
         * FindWindowA. L'effetto "la prima finestra torna in primo piano"
         * non era mai stato verificato per davvero. Si enumerano quindi le
         * finestre di primo livello davvero presenti, confrontando la classe
         * a mano, invece di fidarsi di un'API che qui si e' mostrata
         * inaffidabile. */
        EnumWindows(istanza_trova_finestra, (LPARAM)&altra);
        if (altra) {
            ShowWindow(altra, SW_RESTORE);
            SetForegroundWindow(altra);
        }
        return false;
    }
    return true;
}

static int modo_argomenti(void)
{
    char buf[4096];

    registro_apri();
    radice_registra();
    config_default(&g_c);
    /* config.txt si legge accanto all'ESEGUIBILE, non alla cartella
     * corrente: vedi guscio_percorso_accanto in adb.c per il perche' (e' il
     * Bloccante 3 della revisione finale: prima di questa correzione il file
     * che il prodotto spedisce non veniva mai letto). La radice stessa e'
     * gia' stata scelta in cima a main(), prima che si arrivasse qui. */
    configurazione_risolta(&g_c);
    if (!vm_argomenti(&g_c, buf, sizeof(buf))) {
        printf("gli argomenti non stanno nel buffer\n");
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

/* Toglie \r e \n dalla fine di s, in posto. Un accorgimento locale invece di
 * esportare pota() da config.c: un solo chiamante (l'uscita di "wm density"
 * versata nel registro in VM_PRONTO) non giustifica condividere quella
 * funzione fra file, e pota() toglie anche gli spazi in testa, che qui non
 * servono. */
static void taglia_fine_riga(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

/* hz e densita' hanno entrambe uno 0 con un significato speciale (automatico
 * per hz, non toccare per densita'), e stampare uno zero nudo in --config non
 * lo direbbe: sembrerebbe un valore come un altro. buf deve sopravvivere fino
 * alla printf che lo consuma: il chiamante lo dichiara e lo passa. */
static const char *forma_o_zero(int v, char *buf, int max, const char *zero)
{
    if (v == 0) {
        return zero;
    }
    snprintf(buf, max, "%d", v);
    return buf;
}

static int modo_config(void)
{
    char buf[8192];
    char hz_buf[16], densita_buf[16];

    registro_apri();
    radice_registra();
    /* Cosi' Habumi.exe --config diventa il modo di vedere la decisione
     * sul DPI senza avviare la VM: vedi il commento gemello in main(). */
    registro_riga(REG_GUSCIO, "%s", dpi_esito());
    config_default(&g_c);
    /* variante=%s e' qui perche' senza non si capisce a colpo d'occhio cosa
     * girera': i nomi sono ASIMMETRICI (la vanilla e' system.img senza
     * suffisso, vedi varianti.c), quindi "vcpu=6 memoria=6144 ..." da solo
     * non dice se questa configurazione avvia la vanilla o la gapps. E' il
     * gemello STATICO della riga di registro che vm_argomenti scrive a
     * runtime (vedi vm.c): --config esiste apposta per vedere la
     * configurazione risolta senza avviare niente, e senza questa chiave
     * mancherebbe l'unica cosa che lo direbbe. */
    printf("--- default\n  vcpu=%d memoria=%d %dx%d riprove=%d porta_adb=%d "
           "porta_appunti=%d gl=%s gl_flush_wait=%s audio=%s scala_guest=%s "
           "hz=%s densita=%s "
           "variante=%s\n",
           g_c.vcpu,
           g_c.memoria, g_c.larghezza, g_c.altezza, g_c.riprove,
           g_c.porta_adb, g_c.porta_appunti, g_c.gl,
           g_c.gl_attesa_flush ? "yes" : "no", g_c.audio ? "on" : "off",
           g_c.scala_guest ? "si" : "no",
           forma_o_zero(g_c.hz, hz_buf, sizeof(hz_buf), "auto"),
           forma_o_zero(g_c.densita, densita_buf, sizeof(densita_buf),
                        "non_tocca"),
           var_testo(g_c.variante));
    /* Stesso percorso accanto all'eseguibile di modo_argomenti e di main():
     * vedi guscio_percorso_accanto in adb.c per il perche'. */
    configurazione_risolta(&g_c);
    printf("--- risolta\n  vcpu=%d memoria=%d %dx%d riprove=%d porta_adb=%d "
           "porta_appunti=%d gl=%s gl_flush_wait=%s audio=%s scala_guest=%s "
           "hz=%s densita=%s "
           "variante=%s\n",
           g_c.vcpu,
           g_c.memoria, g_c.larghezza, g_c.altezza, g_c.riprove,
           g_c.porta_adb, g_c.porta_appunti, g_c.gl,
           g_c.gl_attesa_flush ? "yes" : "no", g_c.audio ? "on" : "off",
           g_c.scala_guest ? "si" : "no",
           forma_o_zero(g_c.hz, hz_buf, sizeof(hz_buf), "auto"),
           forma_o_zero(g_c.densita, densita_buf, sizeof(densita_buf),
                        "non_tocca"),
           var_testo(g_c.variante));
    printf("--- cosa e' stato ignorato\n");
    while (registro_nuove(buf, sizeof(buf))) {
        printf("%s", buf);
    }
    return 0;
}

/* --- la mappa dei tasti: stato, tubo, menu ------------------------------ */

/* Accende: manda il comando sul tubo e, SOLO se e' arrivato, aggiorna lo
 * stato e il bottone. Se il tubo non consegna, la mappa in QEMU non e'
 * cambiata: dire "accesa" qui sarebbe esattamente il disallineamento che lo
 * stato unico deve evitare. tubo_mappa_carica ha gia' scritto nel registro
 * la causa (tubo assente, tubo occupato, altro errore Windows); questa riga
 * aggiunge solo la CONSEGUENZA vista da qui, come fa il ramo ID_RUOTA per
 * tubo_risoluzione poco piu' sotto. */
static void mappa_accendi(const char *percorso)
{
    if (!tubo_mappa_carica(percorso)) {
        registro_riga(REG_GUSCIO, "keys: the command pipe did not "
                      "deliver the load of %s", percorso);
        return;
    }
    snprintf(mappa_profilo, sizeof(mappa_profilo), "%s", percorso);
    mappa_accesa = true;
    finestra_bottone_tasti(true);
    registro_riga(REG_GUSCIO, "keys: map on, profile %s", percorso);
}

static void mappa_spegni(void)
{
    if (!tubo_mappa_spegni()) {
        registro_riga(REG_GUSCIO, "keys: the command pipe did not "
                      "deliver the switch-off");
        return;
    }
    mappa_accesa = false;
    finestra_bottone_tasti(false);
    registro_riga(REG_GUSCIO, "keys: map off");
}

/* Chiamata dalla scorciatoia di sistema (Ctrl+Alt+T): accende o spegne in
 * base all'unico stato. Se la mappa e' spenta e nessun profilo e' mai stato
 * scelto dal menu, non c'e' niente da accendere: si registra e non si manda
 * nessun comando, invece di mandare "mappa carica" con un percorso vuoto. */
static void mappa_alterna(void)
{
    if (mappa_accesa) {
        mappa_spegni();
        return;
    }
    if (mappa_profilo[0] == '\0') {
        registro_riga(REG_GUSCIO, "keys: no profile chosen, open the "
                      "keys menu to pick a map");
        return;
    }
    mappa_accendi(mappa_profilo);
}

/* Manda "mappa impara <tipo> <percorso>" sul profilo attualmente scelto.
 *
 * Lo stato acceso/spento NON si tocca qui, di proposito, ed e' winq a rendere
 * la scelta giusta: entrando in modo impara spegne la mappa e rilascia i diti
 * da se', ma quando quel modo si chiude -- riuscito, fallito o annullato, tutti
 * e tre -- RIPRISTINA lo stato che c'era prima invece di deciderne uno nuovo
 * (winq-window.c, winq_mappa_impara_azzera). Un apprendimento e' quindi una
 * parentesi che si richiude sullo stesso stato: alla fine la mappa e' accesa se
 * e solo se lo era prima, che e' esattamente cio' che questo bottone gia' dice.
 *
 * Scrivere mappa_accesa = false qui sbaglierebbe proprio nel caso comune -- si
 * impara un tasto mentre la mappa e' accesa, e alla fine e' ancora accesa: il
 * bottone mentirebbe finche' l'utente non lo tocca di nuovo. E non ci sarebbe
 * modo di rimediare: il tubo e' A SENSO UNICO (come per la rotazione, vedi il
 * commento sul ramo ID_RUOTA piu' sotto), quindi lo spegnimento momentaneo dentro
 * winq qui non si vede ne' si potrebbe vedere. Lo stato resta di chi lo possiede
 * -- IL GUSCIO, spec sezione 10 -- e nessuno dei due lo cambia alle spalle
 * dell'altro. */
static void mappa_impara(const char *tipo)
{
    if (mappa_profilo[0] == '\0') {
        registro_riga(REG_GUSCIO, "keys: no profile chosen, there is "
                      "nothing to learn into without a file");
        return;
    }
    if (!tubo_mappa_impara(tipo, mappa_profilo)) {
        registro_riga(REG_GUSCIO, "keys: the command pipe did not "
                      "deliver the learn command (%s)", tipo);
        return;
    }
    registro_riga(REG_GUSCIO, "keys: learn mode (%s) requested on %s", tipo,
                  mappa_profilo);
}

/* L'etichetta del menu per un file: il nome senza ".txt". FindFirstFileA ha
 * gia' filtrato con il modello "*.txt", quindi ogni nome che arriva qui
 * finisce per quel suffisso di 4 caratteri. */
static void mappa_etichetta(const char *nomefile, char *dest, int max)
{
    size_t n = strlen(nomefile);

    if (n > 4) {
        n -= 4;
    }
    snprintf(dest, (size_t)max, "%.*s", (int)n, nomefile);
}

/* Costruisce il menu dei profili ed esegue la scelta dell'utente.
 *
 * SetForegroundWindow prima e PostMessageA(WM_NULL) dopo sono il rimedio
 * documentato Microsoft al menu che non si richiude quando la finestra
 * proprietaria non e' in primo piano -- e la finestra del guscio, per il
 * fuoco che non ha mai (vedi il commento su ID_HOTKEY_TASTI), e' esattamente
 * il caso a rischio. */
static void mappa_menu_mostra(HWND bottone)
{
    HMENU menu;
    WIN32_FIND_DATAA dati;
    HANDLE trova;
    int n = 0;
    RECT r;
    UINT scelta;

    menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    trova = FindFirstFileA(MAPPA_CARTELLA "\\*.txt", &dati);
    if (trova != INVALID_HANDLE_VALUE) {
        do {
            char etichetta[MAX_PATH];

            if (dati.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (n >= MAPPA_MENU_MAX) {
                break;
            }
            snprintf(mappa_menu_percorsi[n], MAX_PATH, "%s\\%s",
                     MAPPA_CARTELLA, dati.cFileName);
            mappa_etichetta(dati.cFileName, etichetta, sizeof(etichetta));
            AppendMenuA(menu, MF_STRING, ID_MENU_PROFILO_BASE + n, etichetta);
            n++;
        } while (FindNextFileA(trova, &dati));
        FindClose(trova);
    }
    if (n == 0) {
        /* Cartella assente o vuota: una voce sola, disabilitata, che dice
         * dove metterei i file. Un menu vuoto non spiega niente a chi lo
         * apre la prima volta. */
        AppendMenuA(menu, MF_STRING | MF_GRAYED, 0,
                    "put profiles in runtime\\keymaps\\*.txt");
    }
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_MENU_SPEGNI, "shut down");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    /* "learn" senza un profilo scelto manderebbe un percorso vuoto: le due
     * voci restano disabilitate finche' l'utente non ne ha scelto uno. */
    AppendMenuA(menu, MF_STRING | (mappa_profilo[0] ? MF_ENABLED : MF_GRAYED),
                ID_MENU_IMPARA_TOCCO, "learn a key");
    AppendMenuA(menu, MF_STRING | (mappa_profilo[0] ? MF_ENABLED : MF_GRAYED),
                ID_MENU_IMPARA_JOYSTICK, "learn the joystick");

    GetWindowRect(bottone, &r);
    SetForegroundWindow(finestra_handle());
    scelta = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN |
                                  TPM_TOPALIGN, r.left, r.bottom, 0,
                                  finestra_handle(), NULL);
    PostMessageA(finestra_handle(), WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (scelta) {
    case 0:
        break;
    case ID_MENU_SPEGNI:
        mappa_spegni();
        break;
    case ID_MENU_IMPARA_TOCCO:
        mappa_impara("touch");
        break;
    case ID_MENU_IMPARA_JOYSTICK:
        mappa_impara("joystick");
        break;
    default:
        if (scelta >= (UINT)ID_MENU_PROFILO_BASE &&
            scelta - (UINT)ID_MENU_PROFILO_BASE < (UINT)n) {
            mappa_accendi(mappa_menu_percorsi[scelta -
                                              (UINT)ID_MENU_PROFILO_BASE]);
        }
        break;
    }
}

/* --- la variante: rete, spazio, menu ------------------------------------
 *
 * QUESTA SEZIONE E' L'INTERFACCIA del selettore (Task sv-6): il bottone e il
 * menu decidono, var_cosa_manca/var_spazio_necessario/var_manifesto_piu_recente
 * (varianti.c, puro) e scarica_testo/scarica_file/scarica_sha256_file/
 * scarica_spazio_libero (scarica.c) fanno il lavoro. Qui si CABLANO insieme,
 * esattamente come la sezione della mappa dei tasti qui sopra cabla tubo.c e
 * il menu dei profili.
 *
 * PERCHE' UN THREAD SUO, e non quello della finestra: 1,17 GB su quel thread
 * bloccano il ciclo dei messaggi e Windows dichiara il programma "non
 * risponde". L'avanzamento torna alla finestra con registro_riga, che ha
 * gia' il proprio lucchetto (registro.c) ed e' gia' chiamato da altri
 * thread di questo stesso programma (rilascio.c, appunti.c, il thread dello
 * stderr di QEMU in vm.c): non serve un canale nuovo per mostrare
 * l'avanzamento "nella finestra", il pannello del registro lo e' gia'.
 *
 * Il cambio VERO -- fermare la VM, scrivere config.txt, riavviare --
 * invece NON puo' girare li': vm.c non ha alcun lucchetto sul proprio stato,
 * quindi ogni sua funzione va chiamata SEMPRE dal thread della finestra. Il
 * thread dello scaricamento fa tutto il resto (rete, tar.exe, variants.txt) e
 * poi POSTA un messaggio, esattamente come ap_consegna_a_windows fa per gli
 * appunti in arrivo dal guest.
 *
 * CHI FA COSA, perche' il confine e' il cuore di questa sezione:
 *
 *   THREAD DELLA FINESTRA -- il menu, la conferma e il controllo dello spazio
 *   (variante_conferma), la decisione di partire, il cambio vero
 *   (variante_richiedi/variante_applica), l'attesa all'uscita
 *   (variante_ferma). Tutto cio' che apre una finestra sta qui: una
 *   MessageBoxA che dichiari proprietaria una finestra di un ALTRO thread la
 *   disabilita da fuori mentre quella continua a ricevere i propri WM_TIMER.
 *
 *   THREAD DELLO SCARICAMENTO (variante_thread_scarica) -- rete, tar.exe,
 *   variants.txt. Non apre finestre e non tocca vm.c: parla solo col registro
 *   e con PostMessageA.
 *
 *   THREAD DELLA VERIFICA (variante_thread_classifica) -- gli sha256 delle due
 *   immagini, una volta sola all'avvio.
 *
 * I DUE THREAD SI ASPETTANO ALL'USCITA, vedi variante_ferma. Non e' prudenza:
 * scrivono nel registro, che il thread della finestra sta per chiudere. */

/* GUSCIO_MSG_VARIANTE_PRONTA (WM_APP+5) -- dal thread dello scaricamento al
 * gestore dei messaggi del guscio, quando il cambio e' pronto per essere
 * applicato -- e' andato in guscio.h accanto ai due degli appunti: adesso il
 * numero serve anche a finestra.c, che e' il posto da cui i messaggi del
 * guscio vengono consegnati (vedi finestra_messaggi_guscio). */

#define ID_MENU_VARIANTE_ALTRA   4001
#define ID_MENU_VARIANTE_ANNULLA 4002

/* I due nomi come compaiono nel menu: "GAPPS" maiuscolo, come lo scrive la
 * spec e come lo chiama chiunque -- var_testo() e' per config.txt e
 * per variants.txt, non per l'utente, e ritorna "gapps" minuscolo. */
static const char *VARIANTE_NOME_MENU[VAR_QUANTE] = { "vanilla", "GAPPS" };

/* Manifesto a URL fisso (sv-vincoli.md). Quello dell'ARCHIVIO no, e da questa
 * correzione in poi non si ricostruisce piu' qui dentro:
 * var_manifesto_piu_recente lo estrae gia' dal campo "url" della voce scelta
 * (vedi VarVoce.url in varianti.h).
 *
 * PRIMA si ricostruiva concatenando una base fissa al nome del file --
 * funzionava, VERIFICATO carattere per carattere contro il manifesto vero
 * -- ma era una trappola in cui si cade in silenzio al prossimo
 * tipo di immagine che si aggiunge: i percorsi di SourceForge NON sono
 * uguali fra i tipi di immagine (system ha il segmento "lineage/", il vendor
 * qualche centinaio di righe piu' sotto no). Leggere il campo che il
 * manifesto contiene gia' chiude quella trappola una volta per tutte, per
 * entrambi. */
#define VARIANTE_URL_MANIFESTO_BASE \
    "https://raw.githubusercontent.com/waydroid/OTA/master/system/lineage/" \
    "waydroid_arm64_only/"

/* La dimensione del /data vuoto, sempre la stessa per entrambe le varianti:
 * un ext4 da 4 GiB (dati.c e runtime/empty-data.zip). */
#define VARIANTE_DATI_BYTE ((uint64_t)4294967296ULL)

static const struct {
    const char *manifesto;     /* nome del file JSON, sotto VARIANTE_URL_MANIFESTO_BASE */
    uint64_t archivio_byte;    /* La taglia dello ZIP della build fissata del
                                * 20260403, dal manifesto vero letto
                                * (728517282 e 1171824911 byte --
                                * "728 MB" e "1,17 GB" nella spec).
                                *
                                * SERVE COME RIPIEGO, e serve proprio a chi non
                                * ha mai scaricato: l'etichetta del menu e la
                                * conferma nominano i byte, ma la taglia VERA
                                * sta nel manifesto (rete) o in variants.txt
                                * (solo se quella variante e' gia' stata
                                * scelta una volta). Su un'installazione nuova
                                * non esiste ne' l'una ne' l'altra, e senza
                                * questa costante il menu direbbe "da
                                * scaricare" e basta -- muto proprio verso chi
                                * deve ancora decidere se ha la banda per
                                * farlo. Il menu resta cosi' indipendente da
                                * Internet, che e' la ragione di disegno
                                * originale.
                                *
                                * Una build futura di taglia diversa rende
                                * questo numero una stima un po' imprecisa,
                                * mai pericolosa: sposta solo il messaggio
                                * "manca spazio" e la cifra nella conferma,
                                * mai la sicurezza dello scaricamento, che si
                                * riverifica da se' con lo sha256 del
                                * manifesto dentro scarica_file. */
    uint64_t estratto_byte; /* MISURATO su questa macchina :
                                 * la dimensione di system(-gapps).img DOPO
                                 * l'estrazione, per la build fissata del
                                 * 20260403 (system.img 1624354816 byte,
                                 * system-gapps.img 2630660096 byte -- "1,62"
                                 * e "2,63" GB in sv-vincoli.md e nella spec).
                                 * var_manifesto_piu_recente non puo' darla:
                                 * e' la dimensione DENTRO lo zip, non quella
                                 * del suo download, e serve a
                                 * var_spazio_necessario PRIMA di scaricare.
                                 * Se domani si scegliesse di nuovo la stessa
                                 * variante con una build diversa, questo
                                 * numero sarebbe solo una stima un po'
                                 * imprecisa -- mai pericolosa: sposta solo il
                                 * messaggio "manca spazio", mai la sicurezza
                                 * dello scaricamento, che si riverifica da
                                 * se' con lo sha256 dentro scarica_file. */
} VARIANTE_RETE[VAR_QUANTE] = {
    { "VANILLA.json", 728517282ULL,  1624354816ULL },
    { "GAPPS.json",   1171824911ULL, 2630660096ULL },
};

/* Quanto si aspetta, chiudendo, il thread dello scaricamento. Il caso comune
 * e' immediato: la richiamata di avanzamento vede l'annullamento a ogni blocco
 * da 64 KB, e tar.exe si uccide entro un passo piu' VARIANTE_TAR_MORTE_MS.
 *
 * IL CASO LUNGO E' UNA CHIAMATA DI RETE GIA' PARTITA, e adesso e' un CONTO
 * invece di una speranza. scarica.c fissa le proprie scadenze con
 * WinHttpSetTimeouts (5 s per risolvere, 10 per connettersi, 30 per inviare,
 * 30 per ogni ricezione), quindi una richiesta in volo torna al peggio dopo
 * 5+10+30 (WinHttpSendRequest) + 30 (WinHttpReceiveResponse) + 30 (la lettura
 * in volo) = 105 s. 120 s coprono quel tetto con margine.
 *
 * IL NUMERO DI PRIMA ERA 60 s, GIUSTIFICATI CON UNA SCADENZA CHE NESSUNO
 * IMPONEVA: il commento diceva "la scadenza di ricezione di WinHTTP, 30 s di
 * default", ma scarica.c non chiamava WinHttpSetTimeouts. Valevano tutti i
 * default, e fra quelli la risoluzione del nome e' ILLIMITATA -- con la rete
 * appena caduta il thread poteva non tornare mai, l'attesa scadeva, e il
 * seguito di main() distruggeva il registro sotto un thread ancora vivo. Le
 * scadenze in scarica.c chiudono la causa; il bool che questa funzione ora
 * ritorna (vedi variante_ferma) e' la rete di sicurezza per il caso in cui
 * quel conto fosse comunque sbagliato. */
#define VARIANTE_FERMA_MS 120000
/* Quanto si aspetta il thread che classifica le immagini all'avvio, e su cosa
 * e' calcolato: due sha256 su 4,25 GB complessivi (1,62 + 2,63), MISURATI in
 * circa 8 s su questa macchina, cioe' circa 530 MB/s -- un NVMe scarico. La
 * verifica si interrompe solo FRA un'immagine e l'altra (scarica_sha256_file
 * non si spezza a meta'), quindi il caso peggiore e' un'immagine intera
 * cominciata un istante prima della richiesta di fermarsi.
 *
 * I 15 s di prima erano meno di 2x il valore misurato, e un fattore 2 non e'
 * un margine su un'operazione limitata dal disco: gli stessi 4,25 GB su un
 * disco meccanico, o su un NVMe conteso dall'avvio di QEMU e da uno
 * scaricamento in corso, vanno a 70-100 MB/s, cioe' 45-60 s. 60 s coprono
 * quel caso. Non costano nulla nel caso normale: questo thread parte
 * all'avvio e ha finito da un pezzo quando si chiude, perche' la VM ci mette
 * due minuti a partire. */
#define VARIANTE_CLASSE_FERMA_MS 60000
/* Ogni quanto si guarda l'annullamento mentre tar.exe estrae. */
#define VARIANTE_TAR_PASSO_MS 200
/* Quanto si aspetta la morte di tar.exe DOPO TerminateProcess. Erano 200 ms,
 * riusando il passo qui sopra, e non bastano: TerminateProcess CHIEDE
 * l'uccisione e ritorna subito, ma il processo muore davvero solo quando il
 * kernel ha chiuso le sue scritture in volo -- e qui la scrittura in volo e'
 * un'immagine da 2,63 GB. Senza questa attesa piu' lunga il tar.exe ancora
 * vivo tiene aperto il file che stiamo per cancellare, DeleteFileA e
 * RemoveDirectoryA falliscono, e restano gigabyte sul disco. */
#define VARIANTE_TAR_MORTE_MS 10000

/* 0 = nessun cambio in corso. Stesso principio di ril_in_corso in
 * rilascio.c: il controllo e la presa devono essere UNA operazione sola. */
static volatile LONG variante_in_corso;
/* >0 = FERMATI. Due richieste diverse, una sola bandiera, perche' per il
 * thread di rete sono la stessa cosa: l'utente ha scelto "annulla" dal menu,
 * oppure il guscio sta chiudendo (WM_APP+1 e variante_ferma). La richiamata di
 * avanzamento (sotto) la legge a ogni blocco e ritorna false: e' scarica_file
 * stesso, non questo file, a fermarsi lasciando il ".parziale" per la volta
 * dopo (vedi scarica.h). La guardano anche i punti fra un passo e l'altro
 * (dopo il manifesto, dopo lo scaricamento) e il ciclo che aspetta tar.exe. */
static volatile LONG variante_annulla;
/* Quale variante si sta scaricando, per la sola etichetta "scaricamento di %s
 * in corso" nel menu.
 *
 * LA TOCCA SOLO IL THREAD DELLA FINESTRA: la scrive variante_menu_mostra dopo
 * aver preso variante_in_corso e prima di far partire il thread, e la rilegge
 * lo stesso menu. Il thread dello scaricamento NON la legge -- riceve la
 * variante come argomento di CreateThread. Il commento di prima dichiarava un
 * ordine di scrittura fra thread ("PRIMA di alzare variante_in_corso") che il
 * codice non rispettava e che nessuno usa: qui non c'e' nessun contratto fra
 * thread da mantenere, ed e' meglio dirlo che documentarne uno falso. */
static VarNome variante_v_in_corso;
/* Le maniglie dei due thread di questa sezione. Si CONSERVANO invece di
 * chiuderle subito: chi le possiede le aspetta in variante_ferma, esattamente
 * come appunti.c fa con ap_thread_h. Vedi il commento su variante_ferma per
 * cosa si rompe senza quell'attesa. */
static HANDLE variante_thread_h;
static HANDLE variante_classe_thread_h;

/* LA CLASSIFICAZIONE DELLE DUE VARIANTI, calcolata una volta all'avvio su un
 * thread di sottofondo (variante_classifica_avvia) e da li' in poi soltanto
 * LETTA dal menu.
 *
 * PERCHE' NON SI CALCOLA APRENDO IL MENU, dove stava: classificare vuol dire
 * lo sha256 di un'immagine da 1,6-2,6 GB, cioe' circa 5 s per la GAPPS sul
 * THREAD DELLA FINESTRA -- la soglia oltre la quale Windows dichiara "non
 * risponde". E il costo vero non e' il disegno fermo: il ciclo dei messaggi di
 * main() e' anche cio' che fa girare vm_passo() e il polling dell'evento di
 * chiusura, quindi per tutta la durata dell'hash la macchina a stati era ferma
 * e chi chiudeva la finestra di Android veniva ignorato.
 *
 * variante_classe_pronta si alza DOPO aver riempito le altre due (barriera
 * completa: InterlockedExchange), e chi legge la guarda PRIMA di toccarle. */
static volatile LONG variante_classe_pronta;
static VarMancante variante_classe[VAR_QUANTE];
static VarStato variante_classe_stato;

/* La bandiera dell'annullamento letta come si deve: volatile impedisce al
 * compilatore di tenersela in un registro, ma non e' una barriera, e questa la
 * scrive un altro thread. Stessa forma di ap_thread in appunti.c. */
static bool variante_annullato(void)
{
    return InterlockedCompareExchange(&variante_annulla, 0, 0) != 0;
}

/* Un cambio di variante e' in attesa che la VM finisca di fermarsi: vedi il
 * ramo VM_USCITO nel ciclo dei messaggi. Toccate SOLO dal thread della
 * finestra. */
static bool variante_riavvio_pendente;
static VarNome variante_riavvio_a;

/* La cartella che contiene percorso, con la barra finale. Stessa logica di
 * dati_cartella_temp (dati.c) e di sc_cartella_di (scarica.c): var_percorsi
 * scrive '/', il resto del guscio '\', quindi si riconoscono entrambi. NON
 * condivisa con le altre due copie per lo stesso motivo gia' scritto li':
 * dati.c e scarica.c restano ciascuno cio' che sono senza dipendere da
 * main.c, e main.c non diventa una terza cosa da cui dipendono. */
static void variante_cartella_di(const char *percorso, char *dest, size_t max)
{
    const char *sep_win = strrchr(percorso, '\\');
    const char *sep_unix = strrchr(percorso, '/');
    const char *sep = sep_win;
    size_t dir_len;

    if (sep_unix && (!sep || sep_unix > sep)) {
        sep = sep_unix;
    }
    dir_len = sep ? (size_t)(sep - percorso) + 1 : 0;
    if (dir_len == 0) {
        snprintf(dest, max, ".");
        return;
    }
    snprintf(dest, max, "%.*s", (int)dir_len, percorso);
}

/* Estrae "system.img" da archivio dentro una cartella temporanea ACCANTO a
 * immagine_finale (STESSO VOLUME: dati_cartella_temp e dati_comando_estrai
 * sono le stesse funzioni pure che usa dati.c per il /data, riusate qui cosi'
 * come sono -- non sanno ne' gli importa se la destinazione e' un /data o
 * un'immagine di sistema) e la sposta su immagine_finale con MoveFileExA: la
 * stessa rinomina atomica di dati_crea_se_manca, ma che SOSTITUISCE il file
 * gia' presente -- vedi il commento sulla chiamata, perche' quel dettaglio da
 * solo decide se si possa tornare all'altra variante oppure no.
 *
 * DUE DIFFERENZE da dati_crea_se_manca: qui non c'e' un controllo "esiste
 * gia'" (chi chiama ha gia' deciso, guardando var_cosa_manca, che questa
 * immagine va rifatta) e il nome dentro lo zip e' fisso a "system.img" --
 * quello che gli archivi ufficiali di Waydroid usano per l'immagine di
 * sistema, qualunque sia il nome del file .zip che la contiene (verificato
 * scaricando il manifesto vero: il filename cambia con la data
 * e il romtype, "system.img" dentro no). */
static bool variante_estrai_immagine(const char *archivio,
                                     const char *immagine_finale,
                                     char *errore, size_t errore_n)
{
    char cartella_temp[MAX_PATH];
    char comando[2 * MAX_PATH + 32];
    char estratto[MAX_PATH];
    DWORD errore_dir;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD esito_processo;

    errore[0] = '\0';

    if (SearchPathA(NULL, "tar.exe", NULL, 0, NULL, NULL) == 0) {
        snprintf(errore, errore_n,
                "impossibile estrarre %s: tar.exe non si trova. Windows 11 "
                "lo spedisce di serie in System32", archivio);
        return false;
    }
    if (!dati_cartella_temp(cartella_temp, sizeof(cartella_temp),
                            immagine_finale)) {
        snprintf(errore, errore_n,
                "il percorso della cartella temporanea per %s e' troppo "
                "lungo", immagine_finale);
        return false;
    }
    if (snprintf(estratto, sizeof(estratto), "%s\\system.img", cartella_temp)
        >= (int)sizeof(estratto)) {
        snprintf(errore, errore_n,
                "il percorso del file estratto in %s e' troppo lungo",
                cartella_temp);
        return false;
    }
    if (!CreateDirectoryA(cartella_temp, NULL)) {
        errore_dir = GetLastError();
        if (errore_dir != ERROR_ALREADY_EXISTS) {
            snprintf(errore, errore_n,
                    "non riesco a creare la cartella temporanea %s (%lu)",
                    cartella_temp, errore_dir);
            return false;
        }
    }
    if (!dati_comando_estrai(comando, sizeof(comando), archivio,
                             cartella_temp)) {
        snprintf(errore, errore_n,
                "il comando di estrazione di %s non sta nel buffer", archivio);
        DeleteFileA(estratto);
        RemoveDirectoryA(cartella_temp);
        return false;
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, comando, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        snprintf(errore, errore_n,
                "impossibile avviare tar.exe per estrarre %s (%lu)", archivio,
                GetLastError());
        DeleteFileA(estratto);
        RemoveDirectoryA(cartella_temp);
        return false;
    }
    /* NON WaitForSingleObject(..., INFINITE), ed e' la differenza fra chiudere
     * il guscio in un istante e aspettare minuti a finestra gia' sparita:
     * estrarre 2,63 GB dura minuti, e chi chiude durante l'estrazione
     * troverebbe il processo ancora vivo senza piu' niente da guardare. Si
     * guarda quindi l'annullamento a ogni passo e si UCCIDE tar.exe: cio' che
     * stava scrivendo sta tutto nella cartella temporanea, che si PROVA a
     * cancellare subito sotto -- e riuscendo o no, nessun file del prodotto
     * resta a meta', perche' l'immagine vera nasce solo dalla rinomina in
     * fondo a questa funzione. */
    for (;;) {
        DWORD atteso = WaitForSingleObject(pi.hProcess, VARIANTE_TAR_PASSO_MS);

        if (atteso != WAIT_TIMEOUT) {
            break;   /* finito, o attesa fallita: l'esito lo dice il codice di uscita */
        }
        if (variante_annullato()) {
            /* I DUE ESITI SI GUARDANO, e la riga di registro dice cio' che e'
             * successo DAVVERO. Prima nessuno dei due si guardava e il
             * messaggio affermava comunque "la cartella temporanea e' stata
             * ripulita": con una scrittura da gigabyte in volo la morte del
             * figlio non e' istantanea, l'attesa da 200 ms scadeva, DeleteFileA
             * e RemoveDirectoryA fallivano sul file ancora aperto, e restavano
             * fino a 2,63 GB sul disco mentre il registro giurava il
             * contrario. Una bugia nel registro e' un difetto come gli altri:
             * chi lo legge decide in base a quello. */
            BOOL ucciso = TerminateProcess(pi.hProcess, 1);
            DWORD morte = WaitForSingleObject(pi.hProcess,
                                              VARIANTE_TAR_MORTE_MS);
            bool pulita;
            DWORD errore_pulizia;

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            /* Si prova a pulire in ogni caso, anche col figlio dato per vivo:
             * puo' essere morto nell'istante fra l'attesa scaduta e queste due
             * righe, e in quel caso la pulizia riesce davvero. E' l'esito di
             * RemoveDirectoryA -- non quello di TerminateProcess ne' quello
             * dell'attesa -- a dire la verita' su cosa e' rimasto: una cartella
             * con dentro ancora qualcosa non si cancella. */
            DeleteFileA(estratto);
            pulita = RemoveDirectoryA(cartella_temp) != 0;
            errore_pulizia = pulita ? 0 : GetLastError();
            if (!pulita) {
                registro_riga(REG_GUSCIO, "variant: %s is NOT deleted (%lu) "
                              "-- TerminateProcess %s, tar.exe %s: the folder "
                              "stays on disk and must be removed by hand",
                              cartella_temp, (unsigned long)errore_pulizia,
                              ucciso ? "riuscita" : "FALLITA",
                              morte == WAIT_OBJECT_0 ? "morto"
                                                     : "ancora vivo");
            }
            /* Corto apposta: chi chiama lo incastona in una riga di registro
             * che nomina gia' l'archivio, e REG_LUNGHEZZA e' 256. La cartella
             * la nomina la riga qui sopra, che c'e' solo quando serve. */
            snprintf(errore, errore_n, "annullata, la cartella temporanea %s",
                    pulita ? "e' stata ripulita"
                           : "NON si e' potuta ripulire");
            return false;
        }
    }
    esito_processo = 0;
    GetExitCodeProcess(pi.hProcess, &esito_processo);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (esito_processo != 0) {
        snprintf(errore, errore_n,
                "tar.exe e' uscito con codice %lu estraendo %s",
                esito_processo, archivio);
        DeleteFileA(estratto);
        RemoveDirectoryA(cartella_temp);
        return false;
    }
    /* MoveFileExA con MOVEFILE_REPLACE_EXISTING, non MoveFileA, ed e' un
     * BLOCCANTE non una rifinitura: MoveFileA con la destinazione GIA'
     * ESISTENTE fallisce con 183 (ERROR_ALREADY_EXISTS), e meta' degli
     * ingressi qui hanno il file sul disco PER DEFINIZIONE -- questo ramo gira
     * per VAR_MANCA_IMMAGINE oppure per VAR_IMPRONTA_SBAGLIATA, e il secondo
     * richiede immagine_c_e == true.
     *
     * Lo scenario e' il primo che fa chiunque provi un selettore: scarico la
     * GAPPS, ci passo, poi torno alla vanilla. system.img c'e' gia' -- viaggia
     * col prodotto -- ma variants.txt non ha la sua riga, quindi la
     * classificazione e' IMPRONTA_SBAGLIATA e il menu dice "da scaricare". Con
     * MoveFileA si scaricavano 728 MB, si verificavano, si estraevano e lo
     * spostamento falliva con 183, lasciando l'archivio sul disco. Riprovare
     * dava lo stesso errore per sempre: il ritorno all'altra variante era
     * impossibile, sempre.
     *
     * MOVEFILE_COPY_ALLOWED NON si aggiunge: la cartella temporanea nasce
     * accanto alla destinazione (dati_cartella_temp), quindi questo e' sempre
     * una rinomina atomica sullo stesso volume -- come in dati_crea_se_manca.
     * Permettere la copia nasconderebbe il giorno in cui non lo fosse piu'
     * dietro un'operazione da minuti che puo' lasciare un'immagine troncata
     * gia' al percorso definitivo.
     *
     * PASSATA A dati_sposta_con_riprova, non chiamata nuda: e' IL PUNTO
     * ESATTO del guasto misurato sul prodotto vivo il cui rapporto ha
     * disegnato quella funzione (vedi il commento su dati_sposta_con_riprova
     * in guscio.h) -- dopo aver scaricato e verificato 1,17 GB della GAPPS,
     * questa stessa chiamata e' fallita con ERROR_ACCESS_DENIED (5) su una
     * destinazione che non era di sola lettura, ed e' riuscita riprovando A
     * MANO pochi minuti dopo, senza riscaricare niente. Buttare via
     * quell'operazione al primo no invece di riprovare un conflitto di
     * condivisione transitorio era il difetto vero, non il blocco in se'. */
    {
        DWORD ultimo_errore = 0;
        int tentativi = 0;

        if (!dati_sposta_con_riprova(estratto, immagine_finale,
                                     MOVEFILE_REPLACE_EXISTING, "immagine",
                                     &ultimo_errore, &tentativi)) {
            /* Se le riprove qui sopra si esauriscono, chi legge deve sapere
             * la stessa cosa che ha reso possibile il successo a mano: questa
             * funzione non ha mai toccato archivio (lo zip scaricato), quindi
             * un nuovo tentativo del cambio riparte dall'estrazione, non dal
             * download -- e' la cosa piu' preziosa che l'utente ha in mano in
             * questo momento, e senza dirlo non ha modo di saperlo. */
            if (dati_sposta_errore_e_transitorio(ultimo_errore)) {
                snprintf(errore, errore_n,
                        "estratto %s ma non riesco a spostarlo su %s dopo %d "
                        "tentativi (%lu) -- l'archivio scaricato resta sul "
                        "disco, riprovare non lo riscarica",
                        estratto, immagine_finale, tentativi,
                        (unsigned long)ultimo_errore);
            } else {
                snprintf(errore, errore_n,
                        "estratto %s ma non riesco a spostarlo su %s (%lu)",
                        estratto, immagine_finale,
                        (unsigned long)ultimo_errore);
            }
            DeleteFileA(estratto);
            RemoveDirectoryA(cartella_temp);
            return false;
        }
    }
    DeleteFileA(estratto);
    RemoveDirectoryA(cartella_temp);
    return true;
}

/* --- la classificazione, calcolata una volta e poi solo letta ----------- */

/* Il nome di uno stato per il registro. Non e' l'etichetta del menu (quella la
 * costruisce variante_menu_mostra con le taglie dentro): serve a rendere
 * leggibile la riga che dice cosa si e' trovato sul disco all'avvio. */
static const char *variante_manca_testo(VarMancante m)
{
    switch (m) {
    case VAR_PRONTA:         return "ready";
    case VAR_MANCA_DATI:     return "/data missing";
    case VAR_MANCA_IMMAGINE: return "image missing";
    default:                 return "wrong hash";
    }
}

/* Guarda il disco per una variante: c'e' l'immagine, ci sono i dati, e che
 * impronta ha l'immagine. E' QUI che si spendono i secondi -- scarica_sha256_file
 * su 1,6-2,6 GB -- ed e' per questo che questa funzione non deve mai finire sul
 * thread della finestra. */
static VarMancante variante_classifica_una(const VarStato *s, VarNome v)
{
    const char *immagine;
    const char *dati;
    bool immagine_c_e;
    bool dati_ci_sono;
    bool sha_ok;
    char sha_file[VAR_SHA256_CIFRE + 1];
    char errore[256];

    var_percorsi(v, &immagine, &dati);
    immagine_c_e = GetFileAttributesA(immagine) != INVALID_FILE_ATTRIBUTES;
    dati_ci_sono = GetFileAttributesA(dati) != INVALID_FILE_ATTRIBUTES;
    sha_ok = immagine_c_e &&
             scarica_sha256_file(immagine, sha_file, errore, sizeof(errore));
    return var_cosa_manca(s, v, immagine_c_e, dati_ci_sono,
                          sha_ok ? sha_file : NULL);
}

/* Classifica ENTRAMBE le varianti e pubblica il risultato. Entrambe e non solo
 * quella non attiva, perche' la variante attiva cambia durante la sessione: dopo
 * un cambio, quella di prima diventa "l'altra" e il menu ne vuole lo stato senza
 * ricalcolarlo. */
static void variante_classifica_tutte(void)
{
    VarStato stato;
    char errore[256];
    int riga_err = 0;
    int i;

    if (!var_stato_leggi(&stato, "runtime/variants.txt", errore,
                         sizeof(errore), &riga_err)) {
        registro_riga(REG_GUSCIO, "variant: runtime/variants.txt line %d "
                      "is malformed (%s), treating it as absent", riga_err,
                      errore);
        memset(&stato, 0, sizeof(stato));
    }
    for (i = 0; i < VAR_QUANTE; i++) {
        /* Fra un'immagine e l'altra, non dentro: scarica_sha256_file non si
         * interrompe a meta'. Chi chiude nei primi secondi aspetta al massimo
         * un hash, non due. */
        if (variante_annullato()) {
            /* NON "il guscio sta chiudendo": variante_annulla e' UNA bandiera
             * per DUE richieste (vedi il commento sulla variabile), e da qui
             * non si distingue una chiusura da un "annulla" del menu. Oggi
             * solo la chiusura puo' arrivare fin qui -- i bottoni restano
             * spenti fino a VM_PRONTO e questo thread ha gia' finito -- ma la
             * riga afferma soltanto cio' che questo punto sa davvero: il
             * giorno in cui l'altra strada si aprisse, direbbe il falso senza
             * che nessuno tocchi questa funzione. */
            registro_riga(REG_GUSCIO, "variant: image check "
                          "interrupted, a stop request arrived");
            return;
        }
        variante_classe[i] = variante_classifica_una(&stato, (VarNome)i);
    }
    variante_classe_stato = stato;
    /* DOPO aver riempito le due sopra: vedi il commento sulle variabili. */
    InterlockedExchange(&variante_classe_pronta, 1);
    registro_riga(REG_GUSCIO, "variant: on-disk check done -- %s: %s, "
                  "%s: %s", var_testo(VAR_VANILLA),
                  variante_manca_testo(variante_classe[VAR_VANILLA]),
                  var_testo(VAR_GAPPS),
                  variante_manca_testo(variante_classe[VAR_GAPPS]));
}

static DWORD WINAPI variante_thread_classifica(LPVOID arg)
{
    (void)arg;
    variante_classifica_tutte();
    return 0;
}

/* Da chiamare UNA volta all'avvio, dal thread della finestra, dopo la
 * creazione del /data della variante attiva (quel blocco cambia proprio cio'
 * che si sta per classificare) e prima di vm_apri.
 *
 * I circa 8 s che questo thread spende sono invisibili: l'avvio ne passa gia'
 * un centinaio ad aspettare la VM, e i bottoni della finestra restano spenti
 * fino a VM_PRONTO.
 *
 * Se il thread non parte si classifica QUI, sincroni: l'avvio ci mette
 * qualche secondo in piu' una volta sola, ma il menu resta vivo invece di
 * mostrare per sempre "verifica in corso". */
static void variante_classifica_avvia(void)
{
    variante_classe_thread_h = CreateThread(NULL, 0, variante_thread_classifica,
                                            NULL, 0, NULL);
    if (!variante_classe_thread_h) {
        registro_riga(REG_GUSCIO, "variant: the check thread did not "
                      "start (%lu), checking right here instead", GetLastError());
        variante_classifica_tutte();
    }
}

/* Riscrive SOLO la riga "variante=..." di config.txt (chiave
 * eventualmente indentata, con o senza spazi attorno all'uguale, come
 * config_carica gia' tollera), lasciando ogni altra riga -- commenti compresi
 * -- intatta: e' la configurazione dell'UTENTE, non uno stato del programma
 * (quello e' runtime/variants.txt), e riscriverla da zero perderebbe ogni
 * commento che l'utente avesse aggiunto.
 *
 * Se la chiave compare piu' di una volta (caso anomalo: config_carica la
 * scriverebbe una volta sola, ma un file modificato a mano potrebbe averne
 * due) tutte le occorrenze si tolgono e ne resta esattamente una, nella
 * posizione della prima -- lasciarne una seconda invariata vorrebbe dire che
 * l'ultima riga del file deciderebbe di nuovo, e il cambio scomparirebbe al
 * prossimo avvio senza che nulla lo dica.
 *
 * "wb" e non "w" in scrittura, per lo stesso motivo di var_stato_scrivi in
 * varianti.c: su Windows "w" tradurrebbe ogni \n in \r\n. Su ogni fallimento
 * (file non leggibile, piu' grande del buffer, non riscrivibile) si registra
 * e non si cambia nulla su disco: g_c.variante in memoria e' gia' quella
 * nuova per QUESTA sessione (variante_applica assegna g_c.variante PRIMA di
 * chiamare questa funzione), ma la prossima sessione leggerebbe ancora la
 * vecchia -- si dichiara, non si finge un successo. */
static void variante_scrivi_configurazione(VarNome v)
{
    char percorso[MAX_PATH];
    static char contenuto[65536];
    static char nuovo[65536];
    FILE *f;
    size_t letti;
    size_t scritti = 0;
    const char *p;
    const char *fine;
    bool trovata = false;

    guscio_percorso_accanto("config.txt", percorso, sizeof(percorso));

    f = fopen(percorso, "rb");
    if (!f) {
        registro_riga(REG_GUSCIO, "variant: cannot read %s to "
                      "write variant=%s: the change applies only to "
                      "this run", percorso, var_testo(v));
        return;
    }
    letti = fread(contenuto, 1, sizeof(contenuto) - 1, f);
    if (!feof(f)) {
        /* Piu' grande del buffer: si rifiuta invece di troncare "un po'",
         * stesso principio di dati_comando_estrai su un comando che non ci
         * sta. */
        fclose(f);
        registro_riga(REG_GUSCIO, "variant: %s is larger than %zu bytes, "
                      "not rewriting it: the change applies only to this "
                      "run", percorso, sizeof(contenuto) - 1);
        return;
    }
    fclose(f);
    contenuto[letti] = '\0';

    p = contenuto;
    fine = contenuto + letti;
    while (p < fine) {
        const char *fine_riga = memchr(p, '\n', (size_t)(fine - p));
        size_t lung_riga = fine_riga ? (size_t)(fine_riga - p) + 1
                                     : (size_t)(fine - p);
        const char *q = p;
        bool chiave_variante;

        while (q < p + lung_riga && (*q == ' ' || *q == '\t')) {
            q++;
        }
        chiave_variante = (size_t)(p + lung_riga - q) >= strlen("variante") &&
                          !strncmp(q, "variante", strlen("variante"));
        if (chiave_variante) {
            const char *r = q + strlen("variante");

            while (r < p + lung_riga && (*r == ' ' || *r == '\t')) {
                r++;
            }
            chiave_variante = r < p + lung_riga && *r == '=';
        }

        if (chiave_variante) {
            if (!trovata) {
                int n = snprintf(nuovo + scritti, sizeof(nuovo) - scritti,
                                "variante=%s\n", var_testo(v));

                if (n < 0 || (size_t)n >= sizeof(nuovo) - scritti) {
                    registro_riga(REG_GUSCIO, "variant: %s not rewritten, "
                                  "the new line does not fit the buffer",
                                  percorso);
                    return;
                }
                scritti += (size_t)n;
                trovata = true;
            }
            /* Righe "variante=" successive alla prima si tolgono e basta:
             * vedi il commento sopra la funzione. */
        } else {
            if (lung_riga >= sizeof(nuovo) - scritti) {
                registro_riga(REG_GUSCIO, "variant: %s not rewritten, it does "
                              "not fit the buffer", percorso);
                return;
            }
            memcpy(nuovo + scritti, p, lung_riga);
            scritti += lung_riga;
        }
        p += lung_riga;
    }
    if (!trovata) {
        int n = snprintf(nuovo + scritti, sizeof(nuovo) - scritti,
                        "variante=%s\n", var_testo(v));

        if (n < 0 || (size_t)n >= sizeof(nuovo) - scritti) {
            registro_riga(REG_GUSCIO, "variant: %s not rewritten, the new "
                          "line does not fit the buffer", percorso);
            return;
        }
        scritti += (size_t)n;
    }

    f = fopen(percorso, "wb");
    if (!f) {
        registro_riga(REG_GUSCIO, "variant: cannot rewrite %s: the "
                      "change applies only to this run", percorso);
        return;
    }
    if (fwrite(nuovo, 1, scritti, f) != scritti) {
        registro_riga(REG_GUSCIO, "variant: write of %s incomplete: the "
                      "change may not have been saved", percorso);
    }
    fclose(f);
    registro_riga(REG_GUSCIO, "variant: %s updated with variant=%s",
                  percorso, var_testo(v));
}

/* Il cambio VERO, da chiamare SOLO col thread della finestra e SOLO a VM
 * ferma: crea il /data se manca (come fa main() al primo avvio, per lo
 * stesso motivo -- l'initramfs monta a secco e si ferma se il file non c'e'),
 * scrive config.txt, e riavvia la macchina a stati da capo. vm_apri()
 * riporta lo stato a VM_PREPARA: il tick successivo del timer la fa
 * ripartire esattamente come al primo avvio del programma. */
static void variante_applica(VarNome v)
{
    const char *dati;
    char errore_dati[256];
    bool creato = false;

    g_c.variante = v;
    var_percorsi(v, NULL, &dati);
    if (!dati_crea_se_manca("runtime/empty-data.zip", dati, &creato,
                            errore_dati, sizeof(errore_dati))) {
        registro_riga(REG_GUSCIO, "%s", errore_dati);
    } else {
        if (creato) {
            registro_riga(REG_GUSCIO, "/data created from runtime/empty-data.zip: "
                          "%s", dati);
        }
        /* Il /data ora c'e' di sicuro: la classificazione tenuta in memoria
         * direbbe ancora VAR_MANCA_DATI, e mentirebbe alla prossima apertura
         * del menu. E' uno dei due soli momenti in cui la cambiamo NOI --
         * l'altro e' la fine di uno scaricamento -- quindi e' uno dei due soli
         * punti in cui va rinfrescata. Siamo sul thread della finestra, lo
         * stesso che la legge. */
        if (variante_classe[v] == VAR_MANCA_DATI) {
            variante_classe[v] = VAR_PRONTA;
        }
    }
    variante_scrivi_configurazione(v);
    registro_riga(REG_GUSCIO, "variant: restarting with %s", var_testo(v));
    vm_apri(&g_c);
}

/* Chiede il cambio verso v. SEMPRE sul thread della finestra: tocca lo stato
 * di vm.c, che non ha lucchetto. Due chiamanti: il click su una voce
 * "pronta" del menu (subito) e GUSCIO_MSG_VARIANTE_PRONTA, dopo che il
 * thread dello scaricamento ha finito.
 *
 * Il cambio vero (variante_applica) avviene SOLO quando la VM ha gia' finito
 * di fermarsi: scrivere sopra system.img/data.img sotto una VM ancora accesa
 * e' esattamente il guasto MISURATO che ha disegnato tutto
 * questo lavoro (sezione 2 della spec) -- li' e' stato un cp a mano mentre
 * QEMU teneva aperta l'immagine, qui la stessa corsa si eviterebbe per un
 * pelo invece che per costruzione. */
static void variante_richiedi(VarNome v)
{
    if (g_chiudendo) {
        registro_riga(REG_GUSCIO, "variant: the shell is closing, switch "
                      "to %s ignored", var_testo(v));
        return;
    }
    if (v == g_c.variante) {
        return;   /* gia' quella attiva: niente da fare */
    }
    variante_riavvio_pendente = true;
    variante_riavvio_a = v;
    switch (vm_azione_chiusura(vm_stato())) {
    case CHIUSURA_SPEGNI:
        registro_riga(REG_GUSCIO, "variant: stopping the VM to switch to %s",
                      var_testo(v));
        finestra_fase("variant", "stopping");
        finestra_bottoni(false);
        vm_avvia_spegnimento();
        break;
    case CHIUSURA_ATTENDI:
        registro_riga(REG_GUSCIO, "variant: a shutdown is already running, the "
                      "switch to %s continues by itself when it ends",
                      var_testo(v));
        /* I bottoni si spengono anche qui, come nel ramo CHIUSURA_SPEGNI: il
         * cambio e' ormai impegnato (variante_riavvio_pendente e' gia' alzato
         * qui sopra) e lasciarli attivi durante un arresto in corso invita a
         * un secondo clic che non puo' fare niente di buono -- il ramo
         * gemello lo spegneva, questo no, e la differenza non aveva ragione. */
        finestra_bottoni(false);
        break;
    case CHIUSURA_DURA:
        vm_chiudi();
        variante_riavvio_pendente = false;
        variante_applica(v);
        break;
    }
}

/* Richiamata di avanzamento per scarica_file, sul thread dello scaricamento
 * (MAI su quello della finestra: vedi il commento in testa alla sezione). Si
 * registra ogni 5 punti percentuali, non a ogni blocco: a blocchi da 64 KB
 * (scarica.c) un download da 1,17 GB chiama questa funzione circa 18000
 * volte, e 18000 righe di registro per un solo scaricamento sarebbero rumore
 * che copre tutto il resto. variante_percento_ultimo si azzera in
 * variante_thread_scarica prima di ogni scaricamento. */
static int variante_percento_ultimo = -5;

static bool variante_avanzamento(uint64_t fatti, uint64_t totali, void *dato)
{
    VarNome v = *(VarNome *)dato;
    int percento = totali ? (int)((fatti * 100) / totali) : 0;

    if (percento >= variante_percento_ultimo + 5) {
        char taglia_fatti[32];
        char taglia_totali[32];

        rilascio_taglia(taglia_fatti, sizeof(taglia_fatti), (long long)fatti);
        rilascio_taglia(taglia_totali, sizeof(taglia_totali),
                        (long long)totali);
        registro_riga(REG_GUSCIO, "variant: downloading %s -- %s of %s (%d%%)",
                      var_testo(v), taglia_fatti, taglia_totali, percento);
        variante_percento_ultimo = percento;
    }
    return !variante_annullato();
}

/* Esito di variante_thread_scarica sul fronte della rete e del successo,
 * scritti INCONDIZIONATAMENTE a ogni chiamata (non solo quando serve) e letti
 * SOLO da chi ha gia' aspettato il thread fino in fondo -- stessa barriera di
 * vendor_esito_ok/vendor_esito_serve_rete (vedi il commento su quei due campi,
 * piu' sotto nella sezione del vendor): il join e' gia' una sincronizzazione,
 * niente volatile ne' Interlocked.
 *
 * variante_menu_mostra -- il chiamante di sempre di questo thread -- non li
 * legge mai: gli basta il registro, che questo thread scrive gia' da se'.
 * Servono al nuovo chiamante, variante_prepara_attiva (sezione del vendor,
 * chiamata da main() per la variante attiva mancante al primo avvio): quello
 * non ha un menu a cui tornare a mostrare un errore, e deve decidere da solo
 * se aprire una finestra di messaggio ("serve la rete") o limitarsi al
 * registro (ogni altro motivo, gia' scritto li' da questo stesso thread). */
static bool variante_esito_ok;
static bool variante_esito_serve_rete;

/* Lo scaricamento vero e proprio: manifesto, archivio, estrazione,
 * cancellazione dell'archivio, scrittura di variants.txt. Gira su un thread
 * suo (vedi il commento in testa alla sezione) e finisce postando
 * GUSCIO_MSG_VARIANTE_PRONTA solo se OGNI passo e' andato -- se qualcosa
 * fallisce a meta', si registra il perche' e si esce senza cambiare variante
 * ne' toccare variants.txt: la variante attiva resta quella di prima.
 *
 * COSA NON FA PIU', e sono tre cose che stavano qui e sono risalite al thread
 * della finestra (variante_menu_mostra):
 *
 *  - LA CONFERMA E IL MESSAGGIO DELLO SPAZIO. Erano due MessageBoxA chiamate
 *    da QUI, con proprietaria una finestra di un ALTRO thread: una modale cosi'
 *    disabilita la finestra proprietaria da fuori, e quella finestra continua
 *    intanto a ricevere il WM_TIMER che sorveglia la chiusura -- si poteva
 *    arrivare a DestroyWindow sulla proprietaria di una modale ancora viva su
 *    un altro thread. In piu' la conferma compariva SECONDI dopo il clic, dopo
 *    il giro di rete, quando l'utente aveva gia' smesso di guardare. Ora si
 *    chiedono prima di far partire questo thread. (E' anche cio' che rende
 *    possibile aspettarlo in variante_ferma: aspettare un thread fermo dentro
 *    una MessageBox sarebbe uno stallo.)
 *
 *  - LA CLASSIFICAZIONE. Calcolava un secondo sha256 dello stesso file gia'
 *    verificato dal menu. Ora si legge quella pubblicata da
 *    variante_classifica_tutte, che il chiamante ha gia' guardato per decidere
 *    di arrivare fin qui: questo thread parte SOLO quando serve scaricare.
 *
 *  - L'AZZERAMENTO di variante_annulla, che stava appena prima di
 *    scarica_file e cancellava un annullamento chiesto durante il recupero del
 *    manifesto -- momento in cui la finestra e' reattiva e il menu offre gia'
 *    "annulla": l'utente leggeva "annullamento chiesto" nel registro e 1,17 GB
 *    partivano lo stesso. Ora si azzera quando lo scaricamento viene ACCETTATO,
 *    sul thread della finestra, e qui la bandiera si GUARDA e basta. */
static DWORD WINAPI variante_thread_scarica(LPVOID arg)
{
    VarNome v = (VarNome)(INT_PTR)arg;
    bool pronto = false;
    static char manifesto[65536];   /* oggi ~8 KB: e' testo di rete, largo margine */
    char errore[512];
    char url_manifesto[256];
    char archivio_percorso[MAX_PATH];
    char cartella[MAX_PATH];
    char taglia[32];
    VarVoce voce;
    const char *immagine;
    const char *dati;
    /* La copia dello stato pubblicato dalla verifica d'avvio: e' la base su cui
     * si scrive la riga nuova, e contiene gia' quella dell'ALTRA variante, che
     * non va persa. */
    VarStato stato = variante_classe_stato;

    variante_esito_ok = false;
    variante_esito_serve_rete = false;
    var_percorsi(v, &immagine, &dati);
    snprintf(url_manifesto, sizeof(url_manifesto), "%s%s",
             VARIANTE_URL_MANIFESTO_BASE, VARIANTE_RETE[v].manifesto);

    registro_riga(REG_GUSCIO, "variant: checking the latest %s build on the "
                  "manifest", var_testo(v));
    if (!scarica_testo(url_manifesto, manifesto, sizeof(manifesto), errore,
                       sizeof(errore))) {
        registro_riga(REG_GUSCIO, "variant: manifest unreachable, "
                      "switch to %s cancelled: %s", var_testo(v), errore);
        variante_esito_serve_rete = true;
        goto fine;
    }
    /* IL PRIMO DEI DUE CANCELLI DELL'ANNULLAMENTO. Il recupero del manifesto
     * dura secondi, e in quei secondi la finestra e' viva e il menu offre gia'
     * "annulla": senza questo controllo la richiesta veniva vista solo dalla
     * richiamata di avanzamento, cioe' a scaricamento gia' partito. */
    if (variante_annullato()) {
        registro_riga(REG_GUSCIO, "variant: download of %s cancelled "
                      "before it started", var_testo(v));
        goto fine;
    }
    if (!var_manifesto_piu_recente(manifesto, strlen(manifesto), &voce,
                                   errore, sizeof(errore))) {
        registro_riga(REG_GUSCIO, "variant: %s manifest unreadable, "
                      "switch cancelled: %s", var_testo(v), errore);
        goto fine;
    }

    variante_cartella_di(immagine, cartella, sizeof(cartella));
    snprintf(archivio_percorso, sizeof(archivio_percorso), "%s%s", cartella,
             voce.file);
    /* voce.url e' l'indirizzo VERO letto dal manifesto, non piu' ricostruito
     * da una base fissa: vedi il commento su VARIANTE_URL_MANIFESTO_BASE qui
     * sopra e VarVoce.url in varianti.h per il perche'. */
    rilascio_taglia(taglia, sizeof(taglia), (long long)voce.byte);

    registro_riga(REG_GUSCIO, "variant: downloading %s (%s)", voce.file, taglia);
    variante_percento_ultimo = -5;
    if (!scarica_file(voce.url, archivio_percorso, voce.sha256, voce.byte,
                      variante_avanzamento, &v, errore, sizeof(errore))) {
        registro_riga(REG_GUSCIO, "variant: download of %s failed: %s",
                      voce.file, errore);
        variante_esito_serve_rete = true;
        goto fine;
    }
    /* IL SECONDO CANCELLO: scarica_file ritorna false quando la richiamata
     * annulla, quindi qui ci si arriva solo con l'archivio intero. Ma
     * l'annullamento puo' essere arrivato nell'ultimo istante, e l'estrazione
     * di 2,63 GB che comincia sotto dura minuti. */
    if (variante_annullato()) {
        registro_riga(REG_GUSCIO, "variant: %s downloaded, but the cancellation "
                      "arrived: not extracting. The archive stays on disk and "
                      "will not be downloaded again next time", voce.file);
        goto fine;
    }
    registro_riga(REG_GUSCIO, "variant: %s downloaded and verified, extracting",
                  voce.file);

    if (!variante_estrai_immagine(archivio_percorso, immagine, errore,
                                  sizeof(errore))) {
        registro_riga(REG_GUSCIO, "variant: extraction of %s failed: %s",
                      voce.file, errore);
        goto fine;
    }
    if (!DeleteFileA(archivio_percorso)) {
        registro_riga(REG_GUSCIO, "variant: %s extracted, but I could not "
                      "delete the archive (%lu): it stays on disk",
                      voce.file, GetLastError());
    }

    stato.voci[v] = voce;
    if (!var_stato_scrivi(&stato, "runtime/variants.txt", errore,
                          sizeof(errore))) {
        /* IL CONSIGLIO DEVE ESSERE VERO. Prima diceva "riprovare" e basta,
         * mentre l'immagine era GIA' stata spostata al suo posto: il tentativo
         * dopo trovava un file senza riga in variants.txt, cioe' ancora
         * IMPRONTA_SBAGLIATA, e con MoveFileA non poteva riuscire mai. Adesso
         * riesce (vedi MOVEFILE_REPLACE_EXISTING in variante_estrai_immagine),
         * ma costa un altro scaricamento intero: si dice, invece di lasciarlo
         * scoprire. */
        registro_riga(REG_GUSCIO, "variant: %s is extracted and already in "
                      "place, but runtime/variants.txt cannot be written (%s): the "
                      "switch does NOT proceed", var_testo(v), errore);
        registro_riga(REG_GUSCIO, "variant: without that line the image stays "
                      "classified as a wrong hash, so another "
                      "attempt will DOWNLOAD IT AGAIN: free space or clear the "
                      "read-only flag on runtime/ before retrying");
        goto fine;
    }
    registro_riga(REG_GUSCIO, "variant: %s ready", var_testo(v));

    /* LA CLASSIFICAZIONE SI RINFRESCA QUI, ed e' l'unico momento in cui puo'
     * essere cambiata da uno scaricamento. L'immagine e' quella del manifesto e
     * lo sha256 e' gia' stato verificato dentro scarica_file: ricalcolarlo
     * sarebbe un terzo hash dello stesso file. Resta da guardare solo il /data,
     * che costa un GetFileAttributesA.
     *
     * Si scrive PRIMA di azzerare variante_in_corso la' sotto: il menu legge
     * queste due variabili solo dopo aver visto variante_in_corso a zero, e
     * InterlockedExchange e' una barriera completa. */
    variante_classe_stato = stato;
    variante_classe[v] = (GetFileAttributesA(dati) != INVALID_FILE_ATTRIBUTES)
                             ? VAR_PRONTA : VAR_MANCA_DATI;
    pronto = true;

fine:
    variante_esito_ok = pronto;
    if (pronto) {
        PostMessageA(finestra_handle(), GUSCIO_MSG_VARIANTE_PRONTA,
                    (WPARAM)v, 0);
    }
    InterlockedExchange(&variante_in_corso, 0);
    return 0;
}

/* Chiede all'utente se procedere, e controlla lo spazio. TUTTO SUL THREAD
 * DELLA FINESTRA e PRIMA che il thread di rete esista, perche' e' l'unico
 * thread che possiede questa finestra: una MessageBoxA con proprietaria una
 * finestra di un altro thread la disabilita da fuori, e quella finestra
 * continua intanto a ricevere il WM_TIMER che sorveglia la chiusura.
 *
 * LA TAGLIA E' UNA STIMA, e non poteva essere altrimenti senza andare in rete
 * prima di aver chiesto il permesso di andarci: e' quella vera dell'ultima
 * volta se questa variante e' gia' stata scaricata (variants.txt), altrimenti
 * quella della build fissata (VARIANTE_RETE). La cifra esatta del manifesto
 * finisce comunque nel registro appena il thread la conosce.
 *
 * Ritorna false se non si deve procedere: l'utente ha detto no, oppure lo
 * spazio non basta (e in quel caso lo si e' gia' detto, in una finestra e nel
 * registro). */
static bool variante_conferma(VarNome altra, VarMancante m_altra,
                              uint64_t archivio_byte)
{
    const char *immagine;
    char cartella[MAX_PATH];
    char t_serve[32], t_liberi[32], t_manca[32], taglia[32];
    char msg[320];
    uint64_t serve;
    uint64_t liberi = 0;

    rilascio_taglia(taglia, sizeof(taglia), (long long)archivio_byte);
    snprintf(msg, sizeof(msg),
            "Per usare %s serve scaricare circa %s.\n\nContinuare?",
            VARIANTE_NOME_MENU[altra], taglia);
    if (MessageBoxA(finestra_handle(), msg, "Habumi",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        registro_riga(REG_GUSCIO, "variant: download of %s cancelled "
                      "by the user before it started", var_testo(altra));
        return false;
    }

    /* AL PICCO: l'archivio convive con l'immagine estratta finche' non viene
     * cancellato, e il /data va contato. E' un conto che scarica_file NON puo'
     * fare da se': lui conosce solo i byte dell'archivio. */
    var_percorsi(altra, &immagine, NULL);
    variante_cartella_di(immagine, cartella, sizeof(cartella));
    serve = var_spazio_necessario(m_altra, archivio_byte,
                                  VARIANTE_RETE[altra].estratto_byte,
                                  VARIANTE_DATI_BYTE);
    if (scarica_spazio_libero(cartella, &liberi) && liberi >= serve) {
        return true;
    }
    rilascio_taglia(t_serve, sizeof(t_serve), (long long)serve);
    rilascio_taglia(t_liberi, sizeof(t_liberi), (long long)liberi);
    rilascio_taglia(t_manca, sizeof(t_manca),
                    (long long)(serve > liberi ? serve - liberi : 0));
    registro_riga(REG_GUSCIO, "variant: not enough space for %s: %s needed, "
                  "%s free on %s, %s short", var_testo(altra), t_serve,
                  t_liberi, cartella, t_manca);
    snprintf(msg, sizeof(msg),
            "Spazio insufficiente per %s.\n\nServono %s, liberi %s.\n"
            "Mancano %s.", VARIANTE_NOME_MENU[altra], t_serve, t_liberi,
            t_manca);
    MessageBoxA(finestra_handle(), msg, "Habumi",
                MB_OK | MB_ICONERROR);
    return false;
}

/* Costruisce il menu e agisce sulla scelta.
 *
 * IL MENU E' UNA LETTURA PURA: nessun hash, nessun accesso al disco, nessuna
 * rete. La classificazione arriva da variante_classifica_tutte, calcolata una
 * volta all'avvio su un thread di sottofondo -- vedi il commento su
 * variante_classe_pronta per cosa costava calcolarla qui. */
static void variante_menu_mostra(HWND bottone)
{
    HMENU menu;
    RECT r;
    UINT scelta;
    char etichetta[160];
    VarNome attiva = g_c.variante;
    VarNome altra = (attiva == VAR_GAPPS) ? VAR_VANILLA : VAR_GAPPS;
    /* I due valori iniziali non si osservano mai -- si leggono solo dietro la
     * voce di menu che il ramo qui sotto aggiunge, e quella voce esiste solo
     * quando la lettura e' avvenuta -- ma sono quelli che NON fanno danno se un
     * domani qualcuno aggiungesse una strada: "manca l'immagine" al massimo
     * chiede una conferma con una taglia a zero, mentre un VAR_PRONTA di
     * comodo cambierebbe variante verso un'immagine che potrebbe non esserci. */
    VarMancante m_altra = VAR_MANCA_IMMAGINE;
    uint64_t archivio_byte = 0;
    bool classe_pronta =
        InterlockedCompareExchange(&variante_classe_pronta, 0, 0) != 0;
    bool scaricamento_in_corso =
        InterlockedCompareExchange(&variante_in_corso, 0, 0) != 0;

    /* LE DUE LETTURE STANNO DENTRO IL RAMO CHE LE USA, e non sopra dove
     * stavano: i commenti dicevano gia' "dopo aver visto la bandiera, mai
     * prima" -- l'altra meta' della barriera che gli altri due thread alzano
     * scrivendole -- ma il codice le faceva INCONDIZIONATAMENTE. Non si vedeva
     * niente, perche' il valore letto troppo presto finiva in rami che non lo
     * usano, e questo e' esattamente il modo in cui un commento vero diventa
     * falso senza che nessuno se ne accorga. Adesso la promessa e' il codice:
     * variante_classe e variante_classe_stato si leggono solo dopo aver visto
     * variante_in_corso a zero E variante_classe_pronta a uno.
     *
     * LA TAGLIA C'E' ANCHE SU UN'INSTALLAZIONE NUOVA. Prima l'etichetta la
     * prendeva solo da variants.txt, che ha una riga per quella variante SOLO
     * se e' gia' stata scaricata una volta: cosi' "da scaricare, 1,17 GB"
     * mancava proprio a chi non aveva ancora scaricato niente, cioe' all'unico
     * a cui serviva. Il ripiego e' la taglia della build fissata. */
    if (!scaricamento_in_corso && classe_pronta) {
        m_altra = variante_classe[altra];
        archivio_byte = variante_classe_stato.voci[altra].byte
                            ? variante_classe_stato.voci[altra].byte
                            : VARIANTE_RETE[altra].archivio_byte;
    }

    menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    if (scaricamento_in_corso) {
        snprintf(etichetta, sizeof(etichetta), "downloading %s",
                 var_testo(variante_v_in_corso));
        AppendMenuA(menu, MF_STRING | MF_GRAYED, 0, etichetta);
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, ID_MENU_VARIANTE_ANNULLA,
                    "cancel download");
    } else {
        snprintf(etichetta, sizeof(etichetta), "%s (attiva)",
                VARIANTE_NOME_MENU[attiva]);
        AppendMenuA(menu, MF_STRING | MF_GRAYED | MF_CHECKED, 0, etichetta);

        if (!classe_pronta) {
            /* Nei primissimi secondi dell'avvio. Praticamente irraggiungibile
             * -- i bottoni restano spenti fino a VM_PRONTO, che arriva dopo
             * circa due minuti -- ma dirlo costa una riga e l'alternativa
             * sarebbe una voce che mente. */
            AppendMenuA(menu, MF_STRING | MF_GRAYED, 0,
                        "still verifying the images");
        } else {
            if (m_altra == VAR_PRONTA || m_altra == VAR_MANCA_DATI) {
                snprintf(etichetta, sizeof(etichetta), "%s -- pronta",
                        VARIANTE_NOME_MENU[altra]);
            } else {
                char taglia[32];

                rilascio_taglia(taglia, sizeof(taglia),
                                (long long)archivio_byte);
                snprintf(etichetta, sizeof(etichetta),
                        "%s -- da scaricare, %s", VARIANTE_NOME_MENU[altra],
                        taglia);
            }
            AppendMenuA(menu, MF_STRING, ID_MENU_VARIANTE_ALTRA, etichetta);
        }
    }

    GetWindowRect(bottone, &r);
    SetForegroundWindow(finestra_handle());
    scelta = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN |
                                  TPM_TOPALIGN, r.left, r.bottom, 0,
                                  finestra_handle(), NULL);
    PostMessageA(finestra_handle(), WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (scaricamento_in_corso) {
        if (scelta == ID_MENU_VARIANTE_ANNULLA) {
            registro_riga(REG_GUSCIO, "variant: cancellation requested "
                          "by the user");
            InterlockedExchange(&variante_annulla, 1);
        }
        return;
    }
    if (scelta != ID_MENU_VARIANTE_ALTRA) {
        return;   /* menu chiuso senza scegliere, o una delle voci grigie */
    }

    if (m_altra == VAR_PRONTA || m_altra == VAR_MANCA_DATI) {
        variante_richiedi(altra);
        return;
    }
    if (g_chiudendo) {
        registro_riga(REG_GUSCIO, "variant: the shell is closing, "
                      "download not started");
        return;
    }
    if (!variante_conferma(altra, m_altra, archivio_byte)) {
        return;
    }
    /* SI RIGUARDA g_chiudendo, e non e' la stessa domanda di prima: la
     * conferma qui sopra e' una MessageBoxA, cioe' un ciclo di messaggi
     * annidato che puo' durare quanto pare all'utente, e da quando WM_APP+1 si
     * esegue anche dentro le modali (vedi guscio_messaggio) la chiusura puo'
     * essere COMINCIATA mentre quella finestrella era aperta. Senza questa
     * seconda occhiata un "si'" dato dopo un WM_QUERYENDSESSION farebbe partire
     * 1,17 GB di scaricamento mentre Windows si spegne. */
    if (g_chiudendo) {
        registro_riga(REG_GUSCIO, "variant: the shell started "
                      "closing while I was asking for confirmation, download of %s "
                      "not started", var_testo(altra));
        return;
    }
    if (InterlockedCompareExchange(&variante_in_corso, 1, 0) != 0) {
        registro_riga(REG_GUSCIO, "variant: a switch is already running");
        return;
    }
    {
        /* L'AZZERAMENTO DELL'ANNULLAMENTO STA QUI, nel punto in cui lo
         * scaricamento viene ACCETTATO e sul thread che poi lo annullera':
         * cosi' non puo' cancellare una richiesta di annullamento arrivata
         * dopo. Stava dentro il thread, appena prima di scarica_file, e li'
         * buttava via l'annullamento chiesto mentre si recuperava il
         * manifesto. */
        InterlockedExchange(&variante_annulla, 0);
        variante_v_in_corso = altra;
        /* La maniglia del giro precedente: si chiude solo QUI, perche' il
         * thread che rappresenta e' finito da un pezzo (variante_in_corso era
         * a zero) e variante_ferma deve poter contare su una maniglia valida
         * finche' non se ne apre un'altra. */
        if (variante_thread_h) {
            CloseHandle(variante_thread_h);
            variante_thread_h = NULL;
        }
        variante_thread_h = CreateThread(NULL, 0, variante_thread_scarica,
                                         (LPVOID)(INT_PTR)altra, 0, NULL);
        if (!variante_thread_h) {
            registro_riga(REG_GUSCIO, "variant: I could not "
                          "start the download thread");
            InterlockedExchange(&variante_in_corso, 0);
        }
    }
}

/* Chiede ai due thread di questa sezione di fermarsi e LI ASPETTA. Da chiamare
 * dal thread della finestra, prima di registro_chiudi.
 *
 * PERCHE' SI ASPETTA, ed e' esattamente il motivo per cui appunti_ferma
 * aspetta il suo: questi thread scrivono nel registro. Il controllo
 * "if (!reg_aperto) return;" in testa a registro_riga sta FUORI dal lucchetto,
 * quindi un thread che l'ha appena superato chiama EnterCriticalSection su una
 * sezione che registro_chiudi ha nel frattempo distrutta -- comportamento non
 * definito. Prima non si aspettava nessuno: la maniglia si chiudeva subito
 * dopo CreateThread, e chiudendo la finestra a scaricamento al 40% il processo
 * usciva UCCIDENDO il thread dov'era:
 *   - dentro var_stato_scrivi, dove variants.txt e' gia' stato troncato da
 *     fopen("wb") e non ancora riscritto: si perdeva la riga della variante
 *     che FUNZIONAVA, e al riavvio anche quella andava riscaricata;
 *   - dentro tar.exe, dove il figlio restava vivo e la cartella temporanea con
 *     dentro fino a 2,63 GB non la puliva nessuno, per sempre.
 * g_chiudendo non bastava e non poteva bastare: lo guarda solo il thread della
 * finestra, e copre i due secondi finali lasciando aperti i dieci minuti prima.
 *
 * SULLE SCADENZE: se un'attesa scade NON si distrugge e non si chiude niente,
 * come in appunti_ferma -- il thread e' ancora vivo e possiede quello che
 * possiede, e il processo sta comunque uscendo, che libera tutto da se'.
 *
 * IL VALORE DI RITORNO E' LA RETE DI SICUREZZA, ed e' il motivo per cui questa
 * funzione non e' piu' void. false = almeno un'attesa e' SCADUTA, cioe' esiste
 * ancora un thread che puo' essere dentro registro_riga. Chi chiama deve
 * saltare registro_chiudi: distruggere quella sezione critica sotto di lui e'
 * la stessa UB che questa attesa esiste per evitare, ridotta da "sempre" a
 * "raro" ma non chiusa. Non si perde niente saltandolo -- il registro svuota a
 * ogni riga (vedi registro.c) -- e il processo che esce chiude il FILE da se'.
 *
 * LE DUE ATTESE SONO INDIPENDENTI, non in catena: prima si ritornava alla
 * PRIMA scadenza, e quel ritorno lasciava il thread di verifica non aspettato
 * NEMMENO UNA VOLTA. Sono due thread diversi con due bilanci diversi, e la
 * scadenza dell'uno non dice niente sull'altro. */
static bool variante_ferma(void)
{
    bool tutti_fermi = true;

    /* La stessa bandiera dell'"annulla" del menu: per chi scarica, "l'utente
     * ha annullato" e "il guscio sta chiudendo" sono la stessa richiesta. */
    InterlockedExchange(&variante_annulla, 1);

    if (variante_thread_h) {
        if (WaitForSingleObject(variante_thread_h, VARIANTE_FERMA_MS) !=
            WAIT_OBJECT_0) {
            registro_riga(REG_GUSCIO, "variant: the download thread "
                          "did not stop in time, exiting without "
                          "waiting for it and without closing the log");
            tutti_fermi = false;
        } else {
            CloseHandle(variante_thread_h);
            variante_thread_h = NULL;
        }
    }
    if (variante_classe_thread_h) {
        if (WaitForSingleObject(variante_classe_thread_h,
                                VARIANTE_CLASSE_FERMA_MS) != WAIT_OBJECT_0) {
            registro_riga(REG_GUSCIO, "variant: the check thread did not "
                          "stop in time, exiting without waiting for it and "
                          "without closing the log");
            tutti_fermi = false;
        } else {
            CloseHandle(variante_classe_thread_h);
            variante_classe_thread_h = NULL;
        }
    }
    return tutti_fermi;
}

/* Ferma la scorciatoia di sistema e chiude la finestra, in un posto solo: i
 * due punti che distruggono la finestra (spegnimento pulito concluso,
 * uscita dura) altrimenti dovrebbero ricordarsi entrambi di questa riga.
 * PRIMA di DestroyWindow, non dopo: l'handle deve essere ancora valido
 * quando si chiede di sospendere la registrazione. */
static void guscio_chiudi_finestra(void)
{
    UnregisterHotKey(finestra_handle(), ID_HOTKEY_TASTI);
    DestroyWindow(finestra_handle());
}

/* TUTTI I MESSAGGI DEL GUSCIO, IN UN POSTO SOLO, e li consegna fin_wndproc
 * (finestra_messaggi_guscio) invece del ciclo dei messaggi di main().
 *
 * PERCHE' NON PIU' NEL CICLO, dove sono stati fin qui. Erano una catena di
 * "if (msg.message == ...)" davanti a DispatchMessage, e funzionava finche'
 * quel ciclo era l'unico a pompare. Non lo e': ogni MessageBoxA e ogni
 * TrackPopupMenu fanno girare un ciclo PROPRIO dentro USER32, che di
 * quella catena non sa niente e si limita a consegnare i messaggi al
 * wndproc -- dove cadevano in DefWindowProcA e SPARIVANO.
 *
 * IL CASO CHE CONTA e' l'arresto di Windows. WM_QUERYENDSESSION arriva anche
 * a una finestra disabilitata da una modale, fin_wndproc posta WM_APP+1, e
 * quel WM_APP+1 finiva nel nulla: lo spegnimento pulito della VM non partiva
 * MAI, e Android si prendeva la corrente staccata con data.img montata
 * cache=writeback -- esattamente il Bloccante Critico che vm_azione_chiusura
 * esiste per evitare. Stessa fine per un WM_CLOSE dal Gestione attivita' e
 * per un GUSCIO_MSG_VARIANTE_PRONTA arrivato mentre l'utente guardava una
 * conferma. La finestra di perdita l'ha aperta lo spostamento della conferma
 * del cambio variante sul thread della finestra: prima quella modale girava
 * su un ALTRO thread e il ciclo di main() continuava a pompare.
 *
 * Da qui invece non si perde niente: il wndproc lo chiamano tutti i cicli,
 * annidati compresi. Le funzioni chiamate sotto restano quelle di prima e
 * girano sullo stesso thread della finestra di prima -- cambia soltanto CHI
 * le raggiunge. */
static void guscio_messaggio(UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_APP + 1) {
        /* chiusura chiesta dall'utente. La decisione (quale azione, per
         * quale stato) vive in vm_azione_chiusura: qui si esegue soltanto,
         * vedi vm.c per la ragione di ogni stato -- in particolare per il
         * Bloccante Critico che questa estrazione corregge (chiudere
         * durante VM_SPEGNIMENTO non deve piu' uccidere subito).
         *
         * g_chiudendo si alza SUBITO, prima di decidere quale azione:
         * un GUSCIO_MSG_VARIANTE_PRONTA che arrivasse dopo questo punto
         * (il thread dello scaricamento non sa che si sta chiudendo) non
         * deve riaprire la VM ne' riscrivere config.txt -- vedi
         * variante_richiedi. */
        g_chiudendo = true;
        /* L'annullamento deve raggiungere ANCHE il thread di rete, e
         * raggiungerlo subito: g_chiudendo lo guarda solo il thread della
         * finestra, e fra qui e l'uscita dal ciclo dei messaggi ci sono i
         * secondi dello spegnimento pulito della VM -- durante i quali uno
         * scaricamento da 1,17 GB continuerebbe a scendere per poi essere
         * buttato via. variante_ferma la rialza comunque alla fine: qui si
         * guadagna solo l'anticipo, che pero' e' tutto cio' che separa una
         * chiusura immediata da una che aspetta. */
        InterlockedExchange(&variante_annulla, 1);
        switch (vm_azione_chiusura(vm_stato())) {
        case CHIUSURA_SPEGNI:
            finestra_fase("shutdown", "in progress");
            finestra_bottoni(false);
            vm_avvia_spegnimento();
            break;

        case CHIUSURA_ATTENDI:
            /* Uno spegnimento e' gia' in corso: NON fare nulla qui.
             * Uccidere o riavviarlo taglierebbe la corrente al guest a
             * meta' di un reboot -p su data.img cache=writeback -- e' il
             * Bloccante Critico. Ignorare e' sicuro: VM_SPEGNIMENTO_MS in
             * vm_passo forza comunque l'uscita entro la sua scadenza. */
            registro_riga(REG_GUSCIO, "shutdown already running, "
                          "waiting (further close clicks ignored)");
            break;

        case CHIUSURA_DURA:
            /* Si dichiara PERCHE' solo nei due stati in cui l'uscita dura
             * e' inevitabile e non solo comoda: adbd non esiste ancora
             * cosi' presto nell'avvio, quindi non c'e' nessuno a cui
             * chiedere lo spegnimento pulito. Negli altri stati coperti
             * da CHIUSURA_DURA (vedi vm_azione_chiusura) non c'e' alcun
             * processo da uccidere: una riga qui sarebbe rumore, non
             * informazione. */
            if (vm_stato() == VM_AVVIA || vm_stato() == VM_ATTESA_KERNEL) {
                registro_riga(REG_GUSCIO, "HARD close: adbd does not exist "
                              "yet in the first seconds of boot, "
                              "there is nobody to ask for a clean "
                              "shutdown");
            }
            vm_chiudi();
            guscio_chiudi_finestra();
            break;
        }
        return;
    }
    /* I DUE VERSI DEGLI APPUNTI CONDIVISI, e stanno qui per due ragioni
     * diverse. Il primo lo rimanda fin_wndproc perche' il ramo
     * WM_CLIPBOARDUPDATE non deve fare lavoro lungo, come nessun altro ramo
     * di quel file; il secondo lo posta il THREAD del server, perche'
     * SetClipboardData vuole il thread che possiede la finestra e da un
     * altro thread non fallisce -- fa una cosa diversa. */
    if (m == GUSCIO_MSG_APPUNTI_HOST) {
        appunti_host_cambiati();
        return;
    }
    if (m == GUSCIO_MSG_APPUNTI_GUEST) {
        appunti_guest_arrivato();
        return;
    }
    /* Il thread dello scaricamento (variante_thread_scarica) ha finito e
     * tutto e' andato: applica il cambio SEMPRE sul thread della
     * finestra, mai su quello dello scaricamento -- vedi il commento in
     * testa alla sezione "la variante" per il perche' (vm.c non ha
     * lucchetto sul proprio stato). */
    if (m == GUSCIO_MSG_VARIANTE_PRONTA) {
        variante_richiedi((VarNome)wp);
        return;
    }
    /* WM_APP+2 e non WM_COMMAND: WM_COMMAND arriva al wndproc, non alla coda
     * che GetMessage legge, quindi qui non si vedrebbe mai. finestra.c lo
     * rimanda come WM_APP+2 apposta -- e lo rimanda invece di premere i tasti
     * da se' perche' non deve conoscere adb, o diventerebbe il file che sa
     * tutto. */
    if (m == WM_APP + 2) {
        switch (LOWORD(wp)) {
        case ID_POWER:   adb_keyevent(&g_c, "KEYCODE_POWER"); break;
        case ID_VOL_SU:  adb_keyevent(&g_c, "KEYCODE_VOLUME_UP"); break;
        case ID_VOL_GIU: adb_keyevent(&g_c, "KEYCODE_VOLUME_DOWN"); break;
        case ID_HOME:    adb_keyevent(&g_c, "KEYCODE_HOME"); break;
        case ID_BACK:    adb_keyevent(&g_c, "KEYCODE_BACK"); break;
        case ID_RECENTI: adb_keyevent(&g_c, "KEYCODE_APP_SWITCH"); break;
        case ID_RUOTA: {
            int w = 0, h = 0;
            char stato[32] = { 0 };
            int attuale = 0;
            bool verso_orizzontale;
            int bw = 0, bh = 0;

            /* Si chiede al guest quanto crede di essere, invece di tenere
             * un contatore nostro: se qualcosa avesse cambiato la
             * risoluzione alle nostre spalle, un contatore mentirebbe. */
            if (!adb_risoluzione_guest(&g_c, &w, &h)) {
                registro_riga(REG_GUSCIO, "rotation: cannot read "
                              "the guest resolution");
                break;
            }
            /* LO STATO SI LEGGE PRIMA DI REGISTRARE, e la ragione e' che la
             * riga di registro precedente MENTIVA. Diceva
             *     rotazione: 1280x800 -> 800x1280
             * costruita dalla risoluzione appena letta con "wm size" -- che
             * in questo QEMU non cambia mai. MISURATO: tre pressioni di
             * seguito hanno prodotto tre righe IDENTICHE, mentre la
             * rotazione vera alternava (user_rotation 0->1->0->1). Chi
             * leggeva il registro non poteva sapere in quale verso fosse
             * andata, ne' se fosse andata affatto.
             *
             * "settings get" su una chiave mai scritta risponde con la
             * stringa "null", non con un numero: prima della prima rotazione
             * della sessione e' proprio questo il caso. Va trattata come "0"
             * ESPLICITAMENTE: senza il confronto il codice funzionerebbe
             * comunque, ma per una proprieta' implicita di atoi (atoi("null")
             * vale 0 perche' si ferma al primo carattere non numerico), e chi
             * legge non deve essere costretto a dedurla. */
            if (adb_esegui(&g_c, "settings get system user_rotation",
                           stato, sizeof(stato))) {
                if (!strncmp(stato, "null", 4)) {
                    attuale = 0;
                } else {
                    attuale = atoi(stato);
                }
            }
            /* IL BERSAGLIO SI RICAVA DAL VERSO, non scambiando w e h.
             *
             * Prima era tubo_risoluzione(h, w), cioe' lo scambio di cio' che
             * dice "wm size" -- che in questo QEMU non cambia mai. MISURATO
             * nel registro del prodotto: tre pressioni, tre
             * volte lo stesso bersaglio (1600x2560) mentre il verso alternava
             * correttamente. Effetto: dopo la prima pressione il framebuffer
             * restava verticale per sempre, e quando Android tornava
             * orizzontale il contenuto finiva scalato dentro le bande. Il
             * difetto era invisibile a chi guardava il video, perche' lo
             * schermo SI VEDE ruotare comunque: quello lo fa user_rotation.
             *
             * Il verso: user_rotation a 0 vuol dire che adesso siamo
             * orizzontali, quindi si va al VERTICALE, e viceversa. */
            verso_orizzontale = attuale != 0;
            dpi_ruota(w, h, verso_orizzontale, &bw, &bh);
            if (bw <= 0 || bh <= 0) {
                registro_riga(REG_GUSCIO, "rotation: the resolution read "
                              "from the guest (%dx%d) is not usable", w, h);
                break;
            }
            /* Si registra SOLO il bersaglio chiesto, non "da X a Y": QEMU non
             * dice indietro quale risoluzione abbia applicato, e il tubo e' a
             * senso unico. La riga di prima dichiarava un "da" che era la
             * lettura invariante di wm size, cioe' un numero che non
             * descriveva il framebuffer vero. */
            registro_riga(REG_GUSCIO, "rotation: to %s, framebuffer "
                          "requested %dx%d",
                          verso_orizzontale ? "landscape"
                                            : "portrait", bw, bh);

            if (!tubo_risoluzione(bw, bh)) {
                /* Il PERCHE' del fallimento (tubo assente, tubo occupato,
                 * altro errore Windows) e' gia' registrato dentro
                 * tubo_risoluzione: quel logging vive solo li' dentro, e
                 * chi legge main.c non lo sa. Questa riga registra solo
                 * che, dal punto di chiamata, il comando non e' arrivato
                 * a QEMU. */
                registro_riga(REG_GUSCIO, "rotation: the command pipe "
                              "did not deliver the resolution to QEMU");
            }

            /* RIPIEGO, e la ragione per cui esiste sta nella misura del
             * Passo 6 del piano: lo scambio delle dimensioni da solo non ha
             * raddrizzato l'immagine, quindi ad Android va detto anche di
             * ruotare. accelerometer_rotation va spenta o Android
             * ripristina la propria scelta. */
            adb_esegui(&g_c, "settings put system accelerometer_rotation 0",
                       NULL, 0);

            /* SCOSTAMENTO DAL BRIEF, misurato: il brief decide il verso
             * da "h > w" sulla risoluzione appena letta con adb_risoluzione_guest
             * (cioe' "wm size"). Ma il Passo 6 ha gia' mostrato che "wm size"
             * (Physical size) NON cambia mai, in questo QEMU, qualunque cosa
             * gli si mandi sul tubo: resta 1280x800 a ogni pressione. Decidere
             * il verso su un valore che non cambia mai sceglie sempre lo stesso
             * ramo, e "ruota" non tornerebbe mai indietro (misurato: due
             * pressioni consecutive hanno prodotto user_rotation=1 entrambe le
             * volte). Si legge invece la stessa impostazione che si sta per
             * scrivere e la si inverte: e' l'unico stato che davvero alterna a
             * ogni pressione, e resta "chiesto al guest" nello spirito del
             * commento sopra -- non un contatore nostro, ma la lettura di cio'
             * che il guest ha gia' applicato. */
            adb_esegui(&g_c, attuale ? "settings put system user_rotation 0"
                                     : "settings put system user_rotation 1",
                       NULL, 0);
            break;
        }
        case ID_TASTI:
            mappa_menu_mostra((HWND)lp);
            break;
        case ID_VARIANTE:
            variante_menu_mostra((HWND)lp);
            break;
        default: break;
        }
        return;
    }
}

/* --- il vendor: unico, non e' una variante, non ha un /data -------------
 *
 * Vedi il commento su VarStato.vendor in varianti.h per il perche' non sta
 * dentro VarNome. Qui c'e' il COME: si scarica una volta sola, se manca,
 * PRIMA di aprire la VM e nello stesso punto in cui main() crea il /data
 * qualche riga piu' sotto -- e non passa MAI dal menu delle varianti
 * (variante_menu_mostra, qui sopra): non e' una scelta dell'utente, e' un
 * prerequisito che manca.
 *
 * HA UN THREAD SUO, come lo scaricamento di una variante -- e questa
 * correzione esiste per rimpiazzare un commento SBAGLIATO che stava proprio
 * qui. Diceva: "questo blocco gira PRIMA che il ciclo dei messaggi cominci a
 * pompare, quindi non esiste ancora nessuno a cui l'utente possa chiedere
 * annulla", per giustificare uno scaricamento SINCRONO sul thread della
 * finestra. E' FALSO, ed era verificabile leggendo il resto del guscio:
 * finestra_apri crea la finestra, arma un SetTimer da 500 ms e chiama
 * ShowWindow (finestra.c) MOLTO PRIMA di questo punto (vedi dove viene
 * chiamata in main(), e dove viene chiamato vendor_prepara -- fra le due
 * righe non c'e' nessuna DestroyWindow). La finestra a questo punto ESISTE
 * GIA' ED E' GIA' VISIBILE, con una coda messaggi vera: quello che mancava
 * non era la finestra, era qualcuno che la pompasse. Windows non guarda se
 * esiste un bottone "annulla": marca "non risponde" una finestra la cui coda
 * non viene svuotata per qualche secondo, e con circa 74 MB su una linea
 * lenta questo voleva dire minuti di "Habumi.exe non risponde" al
 * PRIMO avvio -- la prima impressione del prodotto.
 *
 * L'estrazione sincrona di runtime/empty-data.zip, qualche riga piu' sotto,
 * NON e' la stessa cosa e non la si prenda a modello per casi come questo:
 * e' I/O locale su 4 MB, sotto il secondo. Questo e' rete, senza limite
 * superiore.
 *
 * IL RIMEDIO: vendor_thread_scarica fa manifesto, scaricamento, estrazione e
 * scrittura dello stato su un thread suo -- stessa forma di
 * variante_thread_scarica qui sopra -- e vendor_attendi, sul thread della
 * finestra, lo aspetta ALTERNANDO MsgWaitForMultipleObjects allo svuotamento
 * della coda dei messaggi: la finestra resta viva e l'avanzamento (le righe
 * che vendor_avanzamento scrive nel registro) continua a comparire nel
 * pannello, esattamente come durante lo scaricamento di una variante. Se
 * l'utente chiude la finestra in questo momento vedi vendor_attendi e
 * vendor_prepara per come si gestisce: vm_apri() non e' stato ancora
 * chiamato, quindi vm_stato() vale VM_PREPARA, WM_APP+1 arriva a
 * CHIUSURA_DURA (vm_azione_chiusura in vm.c) e la finestra si distrugge come
 * sempre -- qui si deve solo smettere di pomparla e non aprire comunque la
 * VM dopo.
 *
 * SE MANCA LA RETE E IL VENDOR NON C'E', si dice che serve una connessione
 * al primo avvio e non si prova ad aprire la VM: un -drive su un file
 * inesistente e' il noto "il kernel si e' inchiodato", una diagnosi falsa
 * che ha gia' fatto perdere tempo una volta (vedi vendor_prepara). */
#define VENDOR_IMMAGINE "guest/images/android/vendor.img"

/* Manifesto a URL fisso, come i due delle varianti (VARIANTE_URL_MANIFESTO_BASE
 * qui sopra) ma sotto un sottopercorso diverso: "vendor/waydroid_arm64_only/",
 * SENZA il segmento "lineage/" che hanno system e system-gapps -- verificato
 * (.superpowers/sdd/rl-vincoli.md) scaricando questo manifesto
 * per davvero e leggendone la voce MAINLINE 20260403. L'URL dell'ARCHIVIO
 * invece non ha una base fissa qui: come per le varianti (vedi il commento
 * su VARIANTE_URL_MANIFESTO_BASE), si legge dal campo "url" della voce del
 * manifesto -- proprio il sottopercorso senza "lineage/" appena descritto
 * era la prova che ricostruirlo da una base condivisa con le varianti non
 * avrebbe funzionato. */
#define VENDOR_URL_MANIFESTO \
    "https://raw.githubusercontent.com/waydroid/OTA/master/vendor/" \
    "waydroid_arm64_only/MAINLINE.json"

/* Alzato quando l'utente chiude la finestra durante lo scaricamento del
 * vendor (vedi vendor_attendi, piu' sotto): stesso principio di
 * variante_annulla piu' sopra, per lo stesso motivo -- lo scrive il thread
 * della finestra e lo legge vendor_thread_scarica, e volatile impedisce al
 * compilatore di tenerselo in un registro ma non e' una barriera, da qui le
 * stesse InterlockedExchange/InterlockedCompareExchange di variante_annulla.
 * Un flag SUO, non variante_annulla riusata: sono due thread diversi, avviati
 * in punti diversi dell'avvio (questo PRIMA di variante_classifica_avvia),
 * e confonderli sotto lo stesso nome renderebbe questa sezione dipendente da
 * uno stato che non le appartiene. */
static volatile LONG vendor_annulla;

static bool vendor_annullato(void)
{
    return InterlockedCompareExchange(&vendor_annulla, 0, 0) != 0;
}

/* Richiamata di avanzamento per lo scaricamento del vendor: stessa forma di
 * variante_avanzamento qui sopra (una riga di registro ogni 5 punti
 * percentuali, non a ogni blocco). ORA controlla l'annullamento: il
 * commento diceva "nessun thread puo' ancora chiederlo, qui", ma questa
 * stessa correzione sposta lo scaricamento su un thread suo (vedi il
 * commento in testa alla sezione) -- da qui in poi chiudere la finestra
 * durante lo scaricamento del vendor deve fermarlo, esattamente come per
 * una variante. */
static int vendor_percento_ultimo = -5;

static bool vendor_avanzamento(uint64_t fatti, uint64_t totali, void *dato)
{
    int percento = totali ? (int)((fatti * 100) / totali) : 0;

    (void)dato;
    if (percento >= vendor_percento_ultimo + 5) {
        char taglia_fatti[32];
        char taglia_totali[32];

        rilascio_taglia(taglia_fatti, sizeof(taglia_fatti), (long long)fatti);
        rilascio_taglia(taglia_totali, sizeof(taglia_totali),
                        (long long)totali);
        registro_riga(REG_GUSCIO, "vendor: downloading -- %s of %s (%d%%)",
                      taglia_fatti, taglia_totali, percento);
        vendor_percento_ultimo = percento;
    }
    return !vendor_annullato();
}

/* Estrae "vendor.img" da archivio in una cartella temporanea ACCANTO a
 * immagine_finale e la sposta li' -- stesso schema di variante_estrai_immagine
 * qui sopra (dati_cartella_temp/dati_comando_estrai/dati_sposta_con_riprova,
 * le funzioni pure e non di dati.c), ma senza il ciclo di annullamento che
 * quella ha: girando su vendor_thread_scarica, non piu' sul thread della
 * finestra (vedi il commento in testa alla sezione), questa attesa non
 * blocca piu' la finestra -- e l'archivio del vendor e' circa 74 MB, non i
 * 1,6-2,6 GB di un'immagine di sistema: estrarlo e' un'operazione LOCALE di
 * pochi secondi, non la rete senza limite superiore che questa correzione
 * esiste per non bloccare. Aspetta tar.exe con WaitForSingleObject(INFINITE),
 * come dati_crea_se_manca fa per data.img,
 * per la stessa ragione.
 *
 * flag_move a 0, non MOVEFILE_REPLACE_EXISTING: vendor_thread_scarica chiama
 * questa funzione solo quando immagine_finale non esiste ancora (stessa
 * premessa di dati_crea_se_manca). */
static bool vendor_estrai(const char *archivio, const char *immagine_finale,
                          char *errore, size_t errore_n)
{
    char cartella_temp[MAX_PATH];
    char comando[2 * MAX_PATH + 32];
    char estratto[MAX_PATH];
    DWORD errore_dir;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD esito_processo;

    errore[0] = '\0';

    if (SearchPathA(NULL, "tar.exe", NULL, 0, NULL, NULL) == 0) {
        snprintf(errore, errore_n,
                "impossibile estrarre %s: tar.exe non si trova. Windows 11 "
                "lo spedisce di serie in System32", archivio);
        return false;
    }
    if (!dati_cartella_temp(cartella_temp, sizeof(cartella_temp),
                            immagine_finale)) {
        snprintf(errore, errore_n,
                "il percorso della cartella temporanea per %s e' troppo "
                "lungo", immagine_finale);
        return false;
    }
    if (snprintf(estratto, sizeof(estratto), "%s\\vendor.img", cartella_temp)
        >= (int)sizeof(estratto)) {
        snprintf(errore, errore_n,
                "il percorso del file estratto in %s e' troppo lungo",
                cartella_temp);
        return false;
    }
    if (!CreateDirectoryA(cartella_temp, NULL)) {
        errore_dir = GetLastError();
        if (errore_dir != ERROR_ALREADY_EXISTS) {
            snprintf(errore, errore_n,
                    "non riesco a creare la cartella temporanea %s (%lu)",
                    cartella_temp, errore_dir);
            return false;
        }
    }
    if (!dati_comando_estrai(comando, sizeof(comando), archivio,
                             cartella_temp)) {
        snprintf(errore, errore_n,
                "il comando di estrazione di %s non sta nel buffer", archivio);
        DeleteFileA(estratto);
        RemoveDirectoryA(cartella_temp);
        return false;
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, comando, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        snprintf(errore, errore_n,
                "impossibile avviare tar.exe per estrarre %s (%lu)", archivio,
                GetLastError());
        DeleteFileA(estratto);
        RemoveDirectoryA(cartella_temp);
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    esito_processo = 0;
    GetExitCodeProcess(pi.hProcess, &esito_processo);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (esito_processo != 0) {
        snprintf(errore, errore_n,
                "tar.exe e' uscito con codice %lu estraendo %s",
                esito_processo, archivio);
        DeleteFileA(estratto);
        RemoveDirectoryA(cartella_temp);
        return false;
    }

    {
        DWORD ultimo_errore = 0;
        int tentativi = 0;

        if (!dati_sposta_con_riprova(estratto, immagine_finale, 0, "vendor",
                                     &ultimo_errore, &tentativi)) {
            snprintf(errore, errore_n,
                    "estratto %s ma non riesco a spostarlo su %s dopo %d "
                    "tentativi (%lu)", estratto, immagine_finale, tentativi,
                    (unsigned long)ultimo_errore);
            DeleteFileA(estratto);
            RemoveDirectoryA(cartella_temp);
            return false;
        }
    }
    DeleteFileA(estratto);
    RemoveDirectoryA(cartella_temp);
    return true;
}

/* Esito di vendor_thread_scarica, letto da vendor_prepara SOLO dopo che
 * vendor_attendi ha aspettato il thread fino in fondo (WaitForSingleObject
 * ha ritornato WAIT_OBJECT_0, mai su una scadenza): quell'attesa e' gia' una
 * barriera -- aspettare la fine di un thread sincronizza con tutto cio' che
 * ha scritto prima di uscire, sulla stessa garanzia per cui appunti_ferma e
 * variante_ferma possono chiudere le maniglie subito dopo -- quindi questi
 * due campi non hanno bisogno di volatile ne' di Interlocked: un solo
 * thread li scrive, e chi li legge lo fa solo a scrittura gia' conclusa e
 * pubblicata. */
static bool vendor_esito_ok;
static bool vendor_esito_serve_rete;

/* Manifesto, scaricamento (verificato dentro scarica_file), estrazione,
 * spostamento, riga di stato -- su un thread suo, come variante_thread_scarica
 * qui sopra e per lo stesso motivo (vedi il commento in testa alla sezione).
 * Riusa le stesse funzioni del selettore delle varianti
 * (var_manifesto_piu_recente, scarica_testo, scarica_file,
 * var_stato_leggi/scrivi).
 *
 * Chiamata SOLO da vendor_prepara, e solo quando VENDOR_IMMAGINE non esiste
 * gia' -- quel controllo, che non ha bisogno di rete ne' di un thread, resta
 * in vendor_prepara. Scrive l'esito in vendor_esito_ok/vendor_esito_serve_rete
 * (vedi il commento sopra i due campi) invece di ritornarlo: e' un thread,
 * CreateThread vuole un DWORD WINAPI (LPVOID), non un bool. */
static DWORD WINAPI vendor_thread_scarica(LPVOID arg)
{
    char errore[512];
    char archivio_percorso[MAX_PATH];
    char cartella[MAX_PATH];
    char taglia[32];
    char avviso[256];
    static char manifesto[65536];
    VarStato stato;
    VarVoce voce;
    int riga_err = 0;

    (void)arg;
    vendor_esito_ok = false;
    vendor_esito_serve_rete = false;

    registro_riga(REG_GUSCIO, "vendor: %s is missing, checking the manifest",
                  VENDOR_IMMAGINE);
    if (!scarica_testo(VENDOR_URL_MANIFESTO, manifesto, sizeof(manifesto),
                       errore, sizeof(errore))) {
        registro_riga(REG_GUSCIO, "vendor: manifest unreachable: %s",
                      errore);
        vendor_esito_serve_rete = true;
        return 0;
    }
    /* IL PRIMO CANCELLO DELL'ANNULLAMENTO, come in variante_thread_scarica:
     * il recupero del manifesto dura secondi, e in quei secondi vendor_attendi
     * sta gia' pompando la finestra -- un WM_CLOSE puo' arrivare prima che lo
     * scaricamento vero e proprio cominci. */
    if (vendor_annullato()) {
        registro_riga(REG_GUSCIO, "vendor: download cancelled before "
                      "it started");
        return 0;
    }
    if (!var_manifesto_piu_recente(manifesto, strlen(manifesto), &voce,
                                   errore, sizeof(errore))) {
        registro_riga(REG_GUSCIO, "vendor: manifest unreadable: %s", errore);
        return 0;
    }

    /* IL CONTROLLO SULLA BUILD: si avvisa e si procede, non si blocca -- un
     * vendor nuovo e' comunque meglio di nessun vendor (vedi
     * var_vendor_build_avviso in varianti.c). */
    if (var_vendor_build_avviso(voce.datetime, avviso, sizeof(avviso))) {
        registro_riga(REG_GUSCIO, "vendor: %s", avviso);
    }

    variante_cartella_di(VENDOR_IMMAGINE, cartella, sizeof(cartella));
    snprintf(archivio_percorso, sizeof(archivio_percorso), "%s%s", cartella,
             voce.file);
    /* voce.url e' l'indirizzo VERO letto dal manifesto, non piu' ricostruito:
     * vedi il commento sopra VENDOR_URL_MANIFESTO e VarVoce.url in
     * varianti.h. */
    rilascio_taglia(taglia, sizeof(taglia), (long long)voce.byte);

    registro_riga(REG_GUSCIO, "vendor: downloading %s (%s)", voce.file, taglia);
    vendor_percento_ultimo = -5;
    if (!scarica_file(voce.url, archivio_percorso, voce.sha256, voce.byte,
                      vendor_avanzamento, NULL, errore, sizeof(errore))) {
        registro_riga(REG_GUSCIO, "vendor: download failed: %s", errore);
        vendor_esito_serve_rete = true;
        return 0;
    }
    /* IL SECONDO CANCELLO: l'annullamento puo' essere arrivato nell'ultimo
     * istante dello scaricamento, come in variante_thread_scarica. */
    if (vendor_annullato()) {
        registro_riga(REG_GUSCIO, "vendor: downloaded, but the cancellation "
                      "arrived: not extracting. The archive stays on disk and "
                      "will not be downloaded again next time");
        return 0;
    }
    registro_riga(REG_GUSCIO, "vendor: %s downloaded and verified, extracting",
                  voce.file);

    if (!vendor_estrai(archivio_percorso, VENDOR_IMMAGINE, errore,
                       sizeof(errore))) {
        registro_riga(REG_GUSCIO, "vendor: extraction failed: %s", errore);
        return 0;
    }
    if (!DeleteFileA(archivio_percorso)) {
        registro_riga(REG_GUSCIO, "vendor: extracted, but I could not "
                      "delete the archive (%lu): it stays on disk",
                      GetLastError());
    }

    /* La riga di stato: letta di nuovo invece di partire da vuota, perche' a
     * questo punto dell'avvio (prima di variante_classifica_avvia) potrebbe
     * gia' esistere runtime/variants.txt con le righe delle varianti, che
     * non vanno perse. Una riga malformata si tratta come assente, stesso
     * principio di variante_classifica_tutte qui sopra. */
    if (!var_stato_leggi(&stato, "runtime/variants.txt", errore,
                         sizeof(errore), &riga_err)) {
        registro_riga(REG_GUSCIO, "vendor: runtime/variants.txt line %d "
                      "is malformed (%s), treating it as absent", riga_err,
                      errore);
        memset(&stato, 0, sizeof(stato));
    }
    stato.vendor = voce;
    if (!var_stato_scrivi(&stato, "runtime/variants.txt", errore,
                          sizeof(errore))) {
        /* NON e' un fallimento del vendor: l'immagine e' gia' al posto
         * giusto sul disco, e vm_verifica_file la trova. Manca solo la riga
         * di stato -- che oggi nessuno rilegge per il vendor, a differenza
         * delle varianti -- quindi si dichiara e si procede comunque. */
        registro_riga(REG_GUSCIO, "vendor: extracted and already in place, but "
                      "runtime/variants.txt cannot be written (%s)", errore);
    }

    registro_riga(REG_GUSCIO, "vendor: ready");
    vendor_esito_ok = true;
    return 0;
}

/* Pompa la coda dei messaggi della finestra finche' h -- un thread di
 * scaricamento avviato PRIMA che vm_apri() sia stato chiamato: oggi il
 * vendor (vendor_attendi, qui sotto) e la system.img della variante attiva
 * (variante_prepara_attiva, sezione del vendor piu' sotto) -- non e'
 * segnalato, o finche' la finestra non muore. A questo punto dell'avvio la
 * finestra esiste gia' ed e' gia' visibile (vedi il commento in testa alla
 * sezione del vendor).
 *
 * ALTERNA MsgWaitForMultipleObjects (si sveglia sul thread finito O su un
 * messaggio in coda, quale arrivi prima) e uno svuotamento completo della
 * coda con PeekMessageA: un singolo giro di GetMessageA/DispatchMessageA non
 * basterebbe, perche' fra un risveglio e l'altro il thread potrebbe gia'
 * essere finito senza che nessun messaggio lo segnali.
 *
 * NON E' IL CICLO DEI MESSAGGI DI main(): vm_apri() non e' stato ancora
 * chiamato quando questa funzione gira, quindi non c'e' nessuna macchina a
 * stati da far avanzare con vm_passo, e WM_TIMER si limita a travasare il
 * registro nel pannello (finestra_passo) -- il resto si dispatcha e basta,
 * esattamente come i messaggi che ne' WM_TIMER ne' WM_HOTKEY intercettano
 * nel ciclo vero.
 *
 * SE L'UTENTE CHIUDE LA FINESTRA: WM_CLOSE diventa WM_APP+1 (fin_wndproc) e
 * arriva a guscio_messaggio -- gia' agganciato da finestra_messaggi_guscio
 * prima che main() chiami sia vendor_prepara sia variante_prepara_attiva --
 * che decide CHIUSURA_DURA (vm_stato() vale ancora VM_PREPARA, vm_apri() non
 * e' stato chiamato) e chiama guscio_chiudi_finestra: la finestra si
 * distrugge e WM_DESTROY posta WM_QUIT, che questa funzione vede e ritorna
 * false.
 *
 * ESTRATTA da un'unica funzione (vendor_attendi) che oggi ha DUE chiamanti
 * con DUE bandiere di annullamento diverse (vendor_annulla per il vendor,
 * variante_annulla per l'immagine della variante attiva): il ciclo di
 * pompaggio -- la parte delicata, con MsgWaitForMultipleObjects e la
 * gestione di WM_QUIT e WM_TIMER -- e' IDENTICO per entrambi, solo la
 * bandiera da alzare DOPO cambia, ed e' il chiamante ad alzarla guardando il
 * valore che ritorniamo qui. Due copie identiche di un ciclo cosi' delicato
 * sarebbero il modo in cui diverge in silenzio alla prossima correzione --
 * la stessa ragione per cui variante_prepara_attiva CHIAMA
 * variante_thread_scarica invece di riscriverlo (vedi quella funzione).
 *
 * Ritorna true se h ha finito da SOLO (il caso comune: non resta altro da
 * fare). Ritorna false se la finestra e' morta nel frattempo: da quel
 * momento non c'e' piu' nessuna finestra da pompare, e chi chiama deve alzare
 * la PROPRIA bandiera di annullamento e aspettare h con una scadenza. */
static bool guscio_pompa_attesa(HANDLE h)
{
    MSG msg;

    for (;;) {
        DWORD esito = MsgWaitForMultipleObjects(1, &h, FALSE, INFINITE,
                                                QS_ALLINPUT);

        if (esito == WAIT_OBJECT_0) {
            return true;   /* il thread ha finito, non resta altro */
        }
        if (esito != WAIT_OBJECT_0 + 1) {
            return true;   /* WAIT_FAILED: si passa all'attesa nuda del chiamante */
        }
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return false;
            }
            if (msg.message == WM_TIMER && msg.hwnd == finestra_handle()) {
                /* NON vm_passo(): vedi il commento qui sopra. */
                finestra_passo();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
}

/* Aspetta h -- vendor_thread_scarica -- pompando la finestra
 * (guscio_pompa_attesa qui sopra), cosi' che l'avanzamento (le righe che
 * vendor_avanzamento scrive nel registro) continui a comparire nel pannello
 * e Windows non marchi la finestra "non risponde".
 *
 * SE LA FINESTRA MUORE mentre si pompa (vedi guscio_pompa_attesa per come),
 * si alza vendor_annulla, cosi' vendor_thread_scarica si ferma al prossimo
 * cancello o al prossimo blocco scaricato invece di continuare a scaricare
 * byte per una finestra che non esiste piu', e si passa a un'attesa nuda sul
 * solo thread. La scadenza e' VARIANTE_FERMA_MS: STESSA costante di
 * variante_ferma, per la STESSA ragione -- vendor_thread_scarica chiama le
 * stesse funzioni di rete (scarica.c) con le stesse scadenze WinHTTP, quindi
 * lo stesso conto (5+10+30+30+30 = 105 s, coperti da un margine a 120 s)
 * vale identico qui. */
static void vendor_attendi(HANDLE h)
{
    if (guscio_pompa_attesa(h)) {
        return;
    }

    InterlockedExchange(&vendor_annulla, 1);
    if (WaitForSingleObject(h, VARIANTE_FERMA_MS) != WAIT_OBJECT_0) {
        registro_riga(REG_GUSCIO, "vendor: the download thread did not "
                      "stop in time (%d s) after the window "
                      "closed, the process exits anyway",
                      VARIANTE_FERMA_MS / 1000);
    }
}

/* Prepara il vendor se manca: il controllo su disco resta qui, sincrono
 * (un GetFileAttributesA non blocca niente), ma lo scaricamento vero e
 * proprio gira su vendor_thread_scarica -- vedi il commento in testa alla
 * sezione per il perche'.
 *
 * Ritorna true sia quando il vendor era gia' sul disco sia quando e' stato
 * scaricato con successo: in entrambi i casi chi chiama puo' procedere.
 *
 * Ritorna false quando il vendor manca e non si e' riusciti a procurarlo,
 * O quando l'utente ha chiuso la finestra mentre lo si procurava (vedi
 * vendor_attendi): in questo secondo caso *serve_rete resta false, la VM
 * non va aperta comunque, e chi chiama deve limitarsi a uscire, non a
 * mostrare un messaggio -- la finestra a cui mostrarlo non c'e' piu'.
 * *serve_rete si alza SOLO quando la causa e' la rete (manifesto o archivio
 * irraggiungibili): e' il caso per cui chi chiama NON deve avviare la VM,
 * vedi il commento in testa alla sezione. Su ogni altro fallimento
 * (manifesto illeggibile, tar.exe assente, spostamento fallito, ...)
 * *serve_rete resta false, ma il vendor manca comunque: chi chiama non deve
 * avviare la VM neanche in quel caso, e la ragione e' gia' nel registro. */
static bool vendor_prepara(bool *serve_rete)
{
    HANDLE h;

    *serve_rete = false;

    if (GetFileAttributesA(VENDOR_IMMAGINE) != INVALID_FILE_ATTRIBUTES) {
        return true;   /* gia' sul disco: niente da fare, nessun thread serve */
    }

    InterlockedExchange(&vendor_annulla, 0);
    h = CreateThread(NULL, 0, vendor_thread_scarica, NULL, 0, NULL);
    if (!h) {
        registro_riga(REG_GUSCIO, "vendor: cannot start the download "
                      "thread (%lu)", GetLastError());
        return false;
    }
    vendor_attendi(h);
    CloseHandle(h);

    if (g_chiudendo) {
        /* L'utente ha chiuso la finestra mentre il vendor si preparava (vedi
         * vendor_attendi): non si apre la VM comunque, anche se lo
         * scaricamento fosse arrivato in fondo con successo un istante prima
         * che la finestra sparisse -- non c'e' piu' nessuna finestra a cui
         * mostrare l'avvio, ne' un ciclo dei messaggi che possa pomparlo. */
        registro_riga(REG_GUSCIO, "vendor: the window was closed "
                      "during preparation, not opening the VM");
        return false;
    }

    *serve_rete = vendor_esito_serve_rete;
    return vendor_esito_ok;
}

/* Prepara l'immagine della VARIANTE ATTIVA se manca, SUBITO DOPO il vendor
 * (vedi la chiamata in main()) e nello STESSO SPIRITO: il pacchetto di
 * rilascio non contiene NESSUNA immagine di Android, ne' il vendor (unico,
 * qui sopra) ne' la system.img della variante scelta in config.txt --
 * quest'ULTIMA mancanza non aveva finora alcun rimedio. Chi scompattava il
 * pacchetto e avviava arrivava dritto a vm_verifica_file (vm.c, chiamata da
 * vm_passo), che si fermava rimandando a build-guest-kernel.sh e
 * build-all.ps1: copioni di SVILUPPO, che pretendono un ambiente di sviluppo
 * che chi ha solo aperto uno zip non ha -- un vicolo cieco travestito da
 * istruzione. Vedi anche il commento corretto in vm.c (vm_passo, VM_PREPARA)
 * per il messaggio che questa correzione rende finalmente vero.
 *
 * NON RISCRIVE lo scaricamento: CHIAMA variante_thread_scarica, lo STESSO
 * thread che gira quando l'utente sceglie l'ALTRA variante dal menu (vedi la
 * sezione "la variante" qui sopra, in particolare variante_menu_mostra).
 * Quella funzione fa gia' tutto -- manifesto, scelta della voce piu' recente,
 * scaricamento con ripresa, verifica dello sha256, estrazione con tar,
 * spostamento con riprova, scrittura di variants.txt -- e un secondo percorso
 * che rifacesse le stesse cose sarebbe quello a DIVERGERE dal primo alla
 * prossima correzione, non il contrario. L'unica differenza col percorso del
 * menu e' che li' la conferma e il controllo dello spazio (variante_conferma)
 * vengono PRIMA: qui non c'e' nessun utente da interrompere con una domanda
 * "vuoi scaricare?" -- il pacchetto senza questa immagine non puo' avviare
 * proprio nulla, quindi non c'e' una scelta reale da offrire.
 *
 * Il controllo "manca?" e' un GetFileAttributesA sulla sola ESISTENZA, come
 * in vendor_prepara: non serve un'impronta qui, solo sapere se scaricare. Se
 * il file c'e' gia' (la build fissata dentro il pacchetto, o gia' scaricata
 * in una sessione precedente) non si tocca niente -- la classificazione VERA,
 * con lo sha256, arriva poco dopo con variante_classifica_avvia.
 *
 * L'ATTESA POMPA LA FINESTRA: stesso schema di vendor_attendi, con
 * guscio_pompa_attesa (qui sopra) a fare il lavoro condiviso -- 728 MB
 * (vanilla) o 1,17 GB (gapps) sono minuti, e senza pompare la coda dei
 * messaggi Windows dichiara "non risponde". La bandiera da alzare se la
 * finestra muore e' pero' variante_annulla, non vendor_annulla: sono due
 * thread diversi (vedi il commento su vendor_annulla).
 *
 * v == g_c.variante SEMPRE, ed e' per questo che il GUSCIO_MSG_VARIANTE_PRONTA
 * che variante_thread_scarica posta a scaricamento riuscito non fa niente di
 * pericoloso se arrivasse a essere dispacciato durante questo pompaggio (puo'
 * succedere: guscio_pompa_attesa dispaccia tutti i messaggi che non siano
 * WM_QUIT o il WM_TIMER della finestra): variante_richiedi, il suo gestore,
 * ritorna subito su "v == g_c.variante: gia' quella attiva, niente da fare"
 * (vedi quella funzione). Non serve quindi sopprimere o intercettare quel
 * messaggio: il codice che gia' esiste lo rende innocuo da solo.
 *
 * RITORNA true se l'immagine e' pronta (gia' c'era, o scaricata ora con
 * successo). RITORNA false SOLO quando serve la rete e non c'e' (in quel caso
 * *serve_rete si alza, stessa forma di vendor_prepara), o quando l'utente ha
 * chiuso la finestra mentre si scaricava, o per ogni altro fallimento gia'
 * scritto da variante_thread_scarica nel registro: in TUTTI i casi di false
 * chi chiama non deve aprire la VM -- un -drive su una system.img inesistente
 * e' il noto "il kernel si e' inchiodato", una diagnosi falsa. */
static bool variante_prepara_attiva(bool *serve_rete)
{
    VarNome v = g_c.variante;
    const char *immagine;
    HANDLE h;

    *serve_rete = false;
    var_percorsi(v, &immagine, NULL);
    if (GetFileAttributesA(immagine) != INVALID_FILE_ATTRIBUTES) {
        return true;   /* gia' sul disco: niente da fare, nessun thread serve */
    }

    registro_riga(REG_GUSCIO, "variant: %s is missing (freshly "
                  "unpacked package, or removed by hand), downloading it before "
                  "opening the VM", immagine);
    InterlockedExchange(&variante_annulla, 0);
    h = CreateThread(NULL, 0, variante_thread_scarica, (LPVOID)(INT_PTR)v, 0,
                     NULL);
    if (!h) {
        registro_riga(REG_GUSCIO, "variant: cannot start the download "
                      "thread (%lu)", GetLastError());
        return false;
    }
    if (!guscio_pompa_attesa(h)) {
        InterlockedExchange(&variante_annulla, 1);
        if (WaitForSingleObject(h, VARIANTE_FERMA_MS) != WAIT_OBJECT_0) {
            registro_riga(REG_GUSCIO, "variant: the download thread for "
                          "the initial image did not stop in time "
                          "(%d s) after the window closed, the "
                          "process exits anyway", VARIANTE_FERMA_MS / 1000);
        }
    }
    CloseHandle(h);

    if (g_chiudendo) {
        /* Stessa premessa di vendor_prepara: non c'e' piu' nessuna finestra a
         * cui mostrare l'avvio, ne' un ciclo dei messaggi che possa pomparlo,
         * anche se lo scaricamento fosse arrivato in fondo un istante prima
         * che la finestra sparisse. */
        registro_riga(REG_GUSCIO, "variant: the window was closed "
                      "while the initial image was downloading, not "
                      "opening the VM");
        return false;
    }

    *serve_rete = variante_esito_serve_rete;
    return variante_esito_ok;
}

int main(int argc, char **argv)
{
    MSG msg;
    VmStato precedente = VM_PREPARA;

    /* PRIMA DI QUALUNQUE ALTRA COSA CHE TOCCHI UN PERCORSO -- prima persino
     * di dpi_dichiara() qui sotto, e prima del ramo che sceglie fra i modi
     * --argomenti, --config e la finestra vera, perche' tutti e tre finiscono
     * per leggere o scrivere un percorso relativo (config.txt tramite
     * configurazione_risolta, il registro tramite registro_apri).
     *
     * IL BLOCCANTE 3 DELLA REVISIONE FINALE, misurato lanciando il pacchetto
     * di rilascio con la cwd che Esplora risorse impone al doppio clic
     * (runtime\bin\, la cartella di QUESTO STESSO eseguibile): ZERO processi,
     * nessuna finestra, nessun QEMU, nessun registro. Le immagini in
     * guest/images/... sono percorsi relativi alla cwd (vedi vm.c), e da
     * runtime\bin\ quella cartella e' un'altra -- il prodotto usciva in
     * silenzio, che al doppio clic vuol dire "non succede assolutamente
     * niente".
     *
     * Il rimedio non toglie la dipendenza dalla cwd (le immagini restano
     * percorsi relativi): la soddisfa il prodotto DA SOLO, risalendo dal
     * proprio eseguibile alla radice del pacchetto e imponendola come
     * cartella corrente, invece di pretenderla da chi lo lancia. Vedi
     * guscio_radice_imposta in adb.c (e il commento su di lei in guscio.h)
     * per il calcolo e la verifica.
     *
     * DEVE girare prima di registro_apri() (qui sotto, e dentro
     * modo_argomenti e modo_config): quest'ultimo apre un file con un
     * percorso RELATIVO (vedi REG_FILE_DEFAULT in registro.c), e se la cwd
     * cambiasse dopo, il registro finirebbe scritto nel posto sbagliato --
     * silenziosamente, che e' esattamente il difetto che questo blocco
     * corregge. g_radice_ok/g_radice/g_radice_provato sono globali di questo
     * file apposta: radice_registra() li rilegge subito dopo ogni
     * registro_apri(), nei tre punti diversi da cui puo' partire. */
    g_radice_ok = guscio_radice_imposta(g_radice, sizeof(g_radice),
                                        g_radice_provato,
                                        sizeof(g_radice_provato));
    if (!g_radice_ok) {
        /* NON USCIRE MUTO: e' esattamente il guasto misurato oggi, e un
         * messaggio che nomina la cartella cercata vale piu' della
         * correzione stessa. Il registro non e' ancora aperto in nessuno dei
         * tre modi possibili, quindi qui l'unico modo di dirlo e' una
         * finestra di messaggio -- nessuna finestra del prodotto esiste
         * ancora a quest'ora, quindi l'owner e' NULL. */
        char messaggio[MAX_PATH + 256];

        snprintf(messaggio, sizeof(messaggio),
                 "Habumi non trova la propria installazione.\n\n"
                 "Cercato: %s\n\n"
                 "Il pacchetto potrebbe essere incompleto, oppure "
                 "l'eseguibile e' stato spostato fuori dalla cartella "
                 "runtime\\bin\\ in cui deve stare.", g_radice_provato);
        MessageBoxA(NULL, messaggio, "Habumi", MB_OK | MB_ICONERROR);
    }

    /* PRIMA DI TUTTO IL RESTO, e non "presto": la consapevolezza del DPI non ha
     * effetto retroattivo su una finestra gia' creata, e da qui in avanti ogni
     * ramo puo' aprirne una. Sta prima anche dei modi --argomenti e --config
     * perche' quelli leggono la scala dello schermo per la chiave scala_guest, e
     * un valore letto senza consapevolezza sarebbe la meta' del vero. */
    dpi_dichiara();

    if (argc > 1 && !strcmp(argv[1], "--argomenti")) {
        return modo_argomenti();
    }
    if (argc > 1 && !strcmp(argv[1], "--config")) {
        return modo_config();
    }

    if (!istanza_unica()) {
        return 0;
    }

    registro_apri();
    /* SUBITO dopo l'apertura, prima di qualunque altra riga: vedi il
     * commento su radice_registra() per il perche' dell'ordine. */
    radice_registra();
    /* Il registro non esiste ancora quando dpi_dichiara() gira, quindi l'esito
     * si versa qui: una dichiarazione fallita in silenzio darebbe lo stesso
     * sintomo di ieri (finestra sfocata) senza nominare la causa. */
    registro_riga(REG_GUSCIO, "%s", dpi_esito());

    /* IL CONTROLLO DELL'HYPERVISOR, e la posizione e' scelta con cura.
     *
     * DOPO registro_apri() perche' la ragione deve finire NEL REGISTRO anche se
     * l'utente chiude subito la finestra del messaggio: quel file e' cio' che
     * gli si chiede quando segnala un problema.
     *
     * PRIMA DI TUTTO IL RESTO -- prima del vendor, prima della variante attiva,
     * prima di vm_apri -- e questo va oltre cio' che la spec chiedeva. La spec
     * diceva "prima di vm_apri", ma al primo avvio fra registro_apri e vm_apri
     * ci stanno 805 MB di scaricamento: controllare dopo vorrebbe dire far
     * scaricare quasi un gigabyte a qualcuno per poi dirgli che il suo computer
     * non puo' avviare l'emulatore. Si controlla quando non e' ancora costato
     * niente.
     *
     * NON si offre di accendere la funzionalita': e' una modifica alle
     * impostazioni di sistema, e chi e' arrivato fin qui ha appena cliccato
     * "Esegui comunque" su SmartScreen. Chiedergli l'elevazione subito dopo e'
     * il modo migliore per farlo desistere. Il comando si mostra e si copia. */
    {
        HypervEsito hv = hyperv_presente();

        if (hv == HYPERV_NON_SO) {
            registro_riga(REG_GUSCIO, "hypervisor: I could not "
                          "ask Windows -- trying to start "
                          "anyway, because the machine may well work");
        }
        if (hyperv_deve_fermare(hv)) {
            /* I \r\n non sono un refuso: MessageBoxA vuole i fine riga di
             * Windows, e con i soli \n il messaggio esce tutto su una riga. */
            static const char msg[] =
                "Windows virtualization is missing.\r\n\r\n"
                "This emulator needs the Windows feature\r\n"
                "\"Windows Hypervisor Platform\", which is not enabled.\r\n\r\n"
                "To enable it, open PowerShell AS ADMINISTRATOR and run:\r\n\r\n"
                "    Enable-WindowsOptionalFeature -Online"
                " -FeatureName HypervisorPlatform -All\r\n\r\n"
                "Then REBOOT the computer and start this program again.\r\n\r\n"
                "Note: this is the only thing that needs administrator\r\n"
                "rights, and it is done once.";

            registro_riga(REG_GUSCIO, "hypervisor MISSING: the "
                          "HypervisorPlatform feature is needed, then a reboot. "
                          "NOT starting QEMU, which would exit immediately, and "
                          "downloading nothing.");
            MessageBoxA(NULL, msg, "Habumi -- virtualization is missing",
                        MB_OK | MB_ICONERROR);
            registro_chiudi();
            return 1;
        }
    }

    /* L'evento con nome che winq segnalera' quando l'utente chiude la finestra
     * di Android. Va creato PRIMA di avviare QEMU: winq lo apre per nome
     * all'inizializzazione, e se non lo trova ricade sul comportamento vecchio
     * (qmp_quit immediato), che deve continuare a funzionare per chi lancia
     * QEMU a mano. */
    g_chiusura = CreateEventA(NULL, TRUE, FALSE, "AndroidRuntimeGuscio.chiusura");
    if (!g_chiusura) {
        registro_riga(REG_GUSCIO, "cannot create the close event "
                      "(%lu): closing the Android window will exit "
                      "hard, as it did before the shell", GetLastError());
    }

    config_default(&g_c);
    /* Accanto all'ESEGUIBILE, non alla cartella corrente: e' il Bloccante 3
     * della revisione finale. La radice del prodotto e' gia' stata scelta
     * (o segnalata mancante) in cima a questa stessa funzione, prima che si
     * arrivasse qui: vedi guscio_radice_imposta in adb.c. */
    configurazione_risolta(&g_c);

    if (!finestra_apri(&g_c)) {
        registro_riga(REG_GUSCIO, "cannot open the window (%lu)",
                      GetLastError());
        return 1;
    }

    /* SUBITO dopo la finestra e prima di chiunque possa postare: senza questa
     * riga fin_wndproc riceverebbe i messaggi del guscio e non avrebbe a chi
     * darli -- niente chiusura, niente bottoni, niente appunti. Prima di
     * appunti_avvia e di variante_classifica_avvia, che avviano i thread che
     * postano, e prima del ciclo dei messaggi, che e' l'unico posto da cui un
     * WM_CLOSE puo' arrivare. */
    finestra_messaggi_guscio(guscio_messaggio);

    if (!RegisterHotKey(finestra_handle(), ID_HOTKEY_TASTI,
                        MOD_CONTROL | MOD_ALT, 'T')) {
        /* Non e' fatale: il bottone continua a funzionare. Ma va detto,
         * perche' il sintomo sarebbe "la scorciatoia non fa niente" senza
         * nessuna spiegazione. */
        registro_riga(REG_GUSCIO, "keys: Ctrl+Alt+T already taken by another "
                      "program, the button still works");
    }

    registro_riga(REG_GUSCIO, "Habumi: %d vCPU, %d MB, %dx%d, gl=%s",
                  g_c.vcpu, g_c.memoria, g_c.larghezza, g_c.altezza, g_c.gl);

    /* DOPO finestra_apri e prima della VM. Dopo, perche' il thread del server
     * consegna cio' che riceve alla finestra e senza finestra non c'e' un posto
     * dove consegnarlo. Prima della VM, perche' l'app del guest chiama appena
     * Android e' partito e trovare gia' qualcuno in ascolto le risparmia il
     * primo giro di attesa a scalare.
     *
     * L'esito NON si guarda, e non e' distrazione: appunti_avvia scrive da se'
     * la ragione nel registro, e un guscio che non partisse perche' gli appunti
     * condivisi non funzionano rinuncerebbe alla VM per una funzione
     * accessoria. */
    appunti_avvia(g_c.porta_appunti);

    /* Il vendor si prepara QUI se manca, PRIMA del /data qui sotto e prima di
     * aprire la VM: vedi il commento in testa alla sezione "il vendor" per
     * il perche' di questo punto preciso e per come lo scaricamento gira su
     * un thread suo, pompando la finestra nel frattempo (vendor_attendi).
     *
     * Se manca la rete e il vendor non c'e', si dice e si esce SENZA aprire
     * la VM: un -drive su guest/images/android/vendor.img inesistente e' il
     * noto "il kernel si e' inchiodato", una diagnosi falsa che ha gia' fatto
     * perdere tempo una volta. Il messaggio va anche in una finestra, non
     * solo nel registro: chi lancia questo eseguibile per la prima volta,
     * scompattato da un archivio, non ha il sorgente ne' i copioni di
     * costruzione per capire da un log tecnico cosa gli manca. Se invece la
     * finestra e' stata chiusa mentre si scaricava (vendor_prepara ritorna
     * false con serve_rete a false anche in quel caso) non si mostra nulla:
     * la finestra a cui mostrarlo non c'e' piu'. */
    {
        bool serve_rete = false;

        if (!vendor_prepara(&serve_rete)) {
            if (serve_rete) {
                MessageBoxA(finestra_handle(),
                            "The first run needs an Internet "
                            "connection, to download the Android vendor "
                            "image.\n\n"
                            "Nothing has been lost: whatever was already "
                            "downloaded stays on disk, and starting Habumi "
                            "again picks up from there.\n\n"
                            "Check the connection and "
                            "start Habumi again.", "Habumi",
                            MB_OK | MB_ICONERROR);
            }
            return 1;
        }
    }

    /* La VARIANTE ATTIVA si prepara QUI se la sua system.img manca, SUBITO
     * DOPO il vendor qui sopra e PRIMA del /data qui sotto: vedi
     * variante_prepara_attiva (sezione del vendor) per il perche' di questo
     * punto preciso e per come lo scaricamento riusa variante_thread_scarica
     * invece di duplicarlo.
     *
     * STESSA FORMA del blocco del vendor qui sopra, per lo stesso motivo: la
     * finestra di messaggio compare SOLO se serve la rete -- ogni altro
     * fallimento e' gia' scritto nel registro da variante_thread_scarica --
     * e in ENTRAMBI i casi non si apre la VM: un -drive sulla system.img
     * ancora mancante e' il noto "il kernel si e' inchiodato", una diagnosi
     * falsa che vendor_prepara qui sopra esiste gia' per non mostrare. */
    {
        bool serve_rete = false;

        if (!variante_prepara_attiva(&serve_rete)) {
            if (serve_rete) {
                MessageBoxA(finestra_handle(),
                            "The first run needs an Internet "
                            "connection, to download the system image "
                            "of the chosen variant.\n\n"
                            "Nothing has been lost: whatever was already "
                            "downloaded stays on disk, and starting Habumi "
                            "again picks up from there.\n\nCheck the "
                            "connection and start Habumi again.",
                            "Habumi", MB_OK | MB_ICONERROR);
            }
            return 1;
        }
    }

    /* Il /data della variante scelta si crea QUI se manca, prima di aprire la
     * VM: l'initramfs fa un mount secco su quel file e si ferma se fallisce
     * (vedi dati.c per il perche' dell'intero meccanismo).
     *
     * NON si passa da var_cosa_manca. Quella funzione classifica anche
     * l'impronta dell'immagine di sistema, e verificarla vuole uno sha256 del
     * file -- che arriva con lo scaricatore, non ancora scritto. Chiamandola
     * oggi con uno sha_del_file a NULL il risultato sarebbe SEMPRE
     * VAR_IMPRONTA_SBAGLIATA (vedi il commento su var_cosa_manca in
     * varianti.c: "un file vero non ha mai quell'impronta" di una voce mai
     * scelta), anche per la vanilla gia' sul disco da prima di questo intero
     * selettore -- e il /data non nascerebbe mai. Si guarda quindi solo il
     * fatto che serve a QUESTA decisione, che var_cosa_manca chiede comunque
     * al chiamante di accertare da se': il file dei dati c'e' o no. */
    {
        const char *dati;
        char errore_dati[256];
        bool creato = false;

        var_percorsi(g_c.variante, NULL, &dati);
        if (!dati_crea_se_manca("runtime/empty-data.zip", dati, &creato,
                                errore_dati, sizeof(errore_dati))) {
            registro_riga(REG_GUSCIO, "%s", errore_dati);
        } else if (creato) {
            registro_riga(REG_GUSCIO, "/data created from "
                          "runtime/empty-data.zip: %s", dati);
        }
    }

    /* La verifica su disco delle due varianti parte QUI, su un thread suo, e
     * il menu da qui in poi la LEGGE soltanto: vedi il commento su
     * variante_classe_pronta per il perche' non si calcoli piu' aprendo il
     * menu.
     *
     * DOPO il blocco del /data qui sopra, non prima: quel blocco cambia proprio
     * cio' che si sta per classificare, e invertirli registrerebbe un
     * VAR_MANCA_DATI gia' falso nel momento in cui lo si scrive.
     *
     * PRIMA di vm_apri, cosi' i suoi secondi scorrono dentro i due minuti che
     * l'avvio della VM spende comunque, invece che dopo.
     *
     * LIMITE DICHIARATO: se qualcuno cancella o sostituisce un'immagine mentre
     * il guscio gira, questa classificazione non se ne accorge. Si rinfresca
     * alla fine di uno scaricamento e alla creazione di un /data, che sono i
     * due soli momenti in cui a cambiarla siamo NOI. */
    variante_classifica_avvia();

    vm_apri(&g_c);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_TIMER && msg.hwnd == finestra_handle()) {
            VmStato s = vm_passo();

            if (g_chiusura &&
                WaitForSingleObject(g_chiusura, 0) == WAIT_OBJECT_0) {
                ResetEvent(g_chiusura);
                registro_riga(REG_GUSCIO, "the user closed the Android "
                                          "window");
                PostMessageA(finestra_handle(), WM_APP + 1, 0, 0);
            }

            finestra_passo();
            if (s != precedente) {
                switch (s) {
                case VM_ATTESA_ANDROID:
                    finestra_fase("kernel", "started");
                    break;
                case VM_PRONTO:
                    finestra_fase("Android", "ready");
                    finestra_bottoni(true);
                    /* densita=0 (default) non tocca nulla: e' il comportamento
                     * di oggi. Applicata UNA volta qui, non ripetuta a ogni
                     * passo, perche' la macchina a stati raggiunge VM_PRONTO
                     * una sola volta per avvio (vedi vm.c: da li' si esce solo
                     * verso VM_MORTA). "wm density" scrive in /data e quindi
                     * PERSISTE finche' data.img non viene sostituita --
                     * verificato riavviando la VM -- ma su una data.img nuova
                     * quel valore non c'e' ancora, ed e' per quello che questa
                     * chiave serve. Una densita' applicata in silenzio
                     * sarebbe indistinguibile da una non applicata: si
                     * registra sempre l'esito, non solo il fallimento. */
                    if (g_c.densita != 0) {
                        char comando[64];
                        char uscita[256] = { 0 };

                        snprintf(comando, sizeof(comando), "wm density %d",
                                 g_c.densita);
                        if (adb_esegui(&g_c, comando, uscita, sizeof(uscita))) {
                            /* "wm density" e' muto su questa immagine, ma
                             * un'uscita con \n spezzerebbe la riga di
                             * registro: si toglie la fine di riga prima di
                             * versarla. */
                            taglia_fine_riga(uscita);
                            registro_riga(REG_GUSCIO, "density: applied %d "
                                          "(%s)", g_c.densita, uscita);
                        } else {
                            registro_riga(REG_GUSCIO, "density: applying "
                                          "%d FAILED (adb did not "
                                          "answer)", g_c.densita);
                        }
                    }
                    break;
                case VM_FALLITA:
                    finestra_fase("boot", "FAILED");
                    finestra_bottoni(false);
                    /* Tranne la variante: e proprio adesso che serve, e non
                     * passa da adb. Vedi finestra_bottone_variante. */
                    finestra_bottone_variante(true);
                    break;
                case VM_MORTA:
                    /* NON si chiude: il registro e' l'unico posto dove sta la
                     * ragione, e chiuderlo la porterebbe via. */
                    finestra_fase("QEMU", "exited on its own");
                    finestra_bottoni(false);
                    finestra_bottone_variante(true);
                    break;
                case VM_USCITO:
                    /* DUE ragioni possono aver portato qui: l'utente ha
                     * chiuso (guscio_chiudi_finestra, come sempre) oppure la
                     * VM si e' fermata apposta per cambiare variante
                     * (variante_richiedi l'ha chiesto, vedi quella sezione).
                     * La seconda NON chiude niente: applica il cambio e
                     * riparte, esattamente come al primo avvio del
                     * programma. */
                    if (variante_riavvio_pendente && !g_chiudendo) {
                        VarNome v = variante_riavvio_a;

                        variante_riavvio_pendente = false;
                        registro_riga(REG_GUSCIO, "variant: the VM has "
                                      "stopped, applying the switch to %s",
                                      var_testo(v));
                        variante_applica(v);
                    } else {
                        /* Qui l'uscita l'abbiamo chiesta noi (o l'utente ha
                         * chiuso MENTRE la VM si fermava per un cambio di
                         * variante gia' avviato -- vedi g_chiudendo), quindi
                         * il guscio ha finito il suo lavoro. */
                        variante_riavvio_pendente = false;
                        guscio_chiudi_finestra();
                    }
                    break;
                default:
                    break;
                }
                precedente = s;
            }
            continue;
        }
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == ID_HOTKEY_TASTI) {
                mappa_alterna();
            }
            continue;
        }
        /* DA QUI IN GIU' NON C'E' PIU' NESSUN ALTRO "if (msg.message == ...)",
         * ed e' voluto: i messaggi del guscio (WM_APP+1, WM_APP+2, i tre
         * GUSCIO_MSG_*) li esegue guscio_messaggio, chiamato da fin_wndproc.
         * Vedi il commento su quella funzione: guardarli QUI li perdeva ogni
         * volta che a pompare era il ciclo interno di una MessageBoxA o di un
         * TrackPopupMenu, e fra i perduti c'era lo spegnimento pulito durante
         * un arresto di Windows.
         *
         * I due rimasti sopra ci restano perche' NON passano da un wndproc che
         * li possa consegnare: WM_TIMER lo consuma fin_wndproc senza sapere
         * nulla della macchina a stati, e WM_HOTKEY riguarda la scorciatoia,
         * non la finestra. Perderli dentro una modale costa un tick e una
         * pressione di Ctrl+Alt+T, non uno spegnimento. */
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* PRIMA di registro_chiudi, tutti e due: il thread del server degli appunti
     * e quelli della variante scrivono nel registro, e chiuderlo mentre uno di
     * loro ci scrive sarebbe una scrittura su un FILE gia' chiuso e un
     * EnterCriticalSection su una sezione gia' distrutta -- la stessa trappola
     * per cui registro_chiudi prende il lucchetto prima di distruggerlo. Vedi
     * variante_ferma per cosa si perdeva, oltre a quello, quando il processo
     * usciva uccidendo il thread dello scaricamento dov'era.
     *
     * Nel percorso normale QEMU e' gia' uscito quando si arriva qui (VM_USCITO,
     * oppure vm_chiudi() gia' fatta da CHIUSURA_DURA), quindi queste attese non
     * tengono in vita nessuna macchina virtuale.
     *
     * SE UN'ATTESA SCADE, registro_chiudi NON SI CHIAMA, ed e' la rete di
     * sicurezza sotto le due attese qui sopra. Un'attesa scaduta vuol dire un
     * thread ancora VIVO, e quel thread puo' essere dentro registro_riga oltre
     * il controllo "if (!reg_aperto) return;" che sta FUORI dal lucchetto:
     * distruggere adesso la sezione critica sarebbe la stessa UB che le attese
     * esistono per evitare, ridotta da "sempre" a "raro" ma non chiusa. Le due
     * attese si fanno TUTTE E DUE, ciascuna nella propria variabile, e solo
     * dopo si guarda l'esito: sono thread diversi, e rinunciare al secondo
     * perche' il primo e' scaduto lascerebbe vivo proprio chi si poteva ancora
     * aspettare.
     *
     * NON SI PERDE NIENTE saltandolo: registro.c svuota a ogni riga, quindi il
     * file su disco e' gia' completo, e il processo che esce lo chiude da se'.
     * Si perde solo la riga di commiato -- ed e' un prezzo che si paga
     * volentieri per non corrompere il processo mentre esce. */
    {
        bool varianti_ferme = variante_ferma();
        bool appunti_fermi = appunti_ferma();

        vm_chiudi();
        if (varianti_ferme && appunti_fermi) {
            registro_chiudi();
        }
    }
    return 0;
}
