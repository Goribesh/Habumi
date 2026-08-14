/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* guscio.h -- tipi condivisi e firme del guscio.
 *
 * Un solo header per tutti i moduli, di proposito: sono sei file che si
 * conoscono a vicenda, e sette header separati costringerebbero a inseguire
 * inclusioni per una dichiarazione. Quando un modulo crescera' abbastanza da
 * volere il proprio header, lo si separera' allora. */
#ifndef GUSCIO_H
#define GUSCIO_H

#include <stdarg.h>
#include <stdbool.h>

/* varianti.h serve solo per VarNome, il tipo del campo Config.variante qui
 * sotto: e' un'inclusione di un header, non di un modulo. varianti.c resta
 * PURO e non conosce guscio.h (vedi il commento in testa a varianti.h) --
 * chi include questo file per il tipo non gli chiede nulla in cambio. */
#include "varianti.h"

/* --- registro.c -------------------------------------------------------- */

/* Le tre sorgenti che scrivono nel log. Il prefisso serve a chi legge: una
 * riga del guest e una decisione del guscio si somigliano troppo senza. */
typedef enum {
    REG_GUSCIO,
    REG_QEMU,
    REG_GUEST
} RegSorgente;

void registro_apri(void);
void registro_riga(RegSorgente sorgente, const char *fmt, ...);

/* Copia in buf le righe non ancora consegnate, separate da "\r\n" (che e'
 * quello che vuole il controllo EDIT di Win32), termina con '\0', e ritorna i
 * byte scritti. Zero se non c'e' nulla di nuovo. Cio' che esce non torna.
 *
 * Se una riga (gia' col suo prefisso) non entra in max e nessun'altra riga e'
 * ancora stata copiata in questa chiamata, la riga viene comunque CONSUMATA:
 * si copia troncata cio' che ci sta, terminando con "..." per rendere
 * visibile il taglio, e si ritorna un valore maggiore di zero. Il chiamante
 * deve poter contare sul progresso: fermarsi senza consumare farebbe
 * incontrare alla chiamata successiva la stessa riga e la stessa condizione,
 * bloccando chi legge per sempre invece di solo ritardarlo. */
int registro_nuove(char *buf, int max);
void registro_chiudi(void);

/* --- config.c ---------------------------------------------------------- */

typedef struct {
    int vcpu;         /* 1..64   */
    int memoria;      /* MB, 1024..65536 */
    int larghezza;    /* px, 320..8192 */
    int altezza;      /* px, 320..8192 */
    int riprove;      /* 1..10   */
    int porta_adb;    /* 1024..65535, e MAI 5554..5585 */
    /* La porta su cui il guscio ascolta il canale degli appunti condivisi.
     * Stessi due divieti di porta_adb e per le stesse ragioni -- mai
     * 5554..5585, che adb esplora per scoprire gli emulatori -- piu' un terzo:
     * mai uguale a porta_adb, perche' due server non possono mettersi in
     * ascolto sulla stessa porta. Il confronto con porta_adb si fa DOPO aver
     * letto tutto il file, o l'esito dipenderebbe dall'ordine delle righe:
     * vedi config.c. */
    int porta_appunti;
    char gl[16];      /* "wgl" oppure "angle" */
    /* Conservare ST_FLUSH_WAIT uscendo da un contesto GL condiviso, cioe' il
     * comportamento di serie del frontend WGL di Mesa: un flush PIU' l'attesa
     * che la GPU abbia finito, a ogni cambio di contesto. Default (del codice)
     * false, cioe' NON si aspetta, e non e' una scelta a occhio -- il costo di
     * una commutazione e' MISURATO, 257 us con l'attesa contro 26-29 senza, con
     * il ciclo principale che passa dal 69% al 37% del secondo e glmark2 da
     * 803-856 a 1322-1463, su tre misure contro due. Il cancello visivo sullo
     * sfarfallio -- cio' per cui a
     * monte l'attesa era stata aggiunta -- e' stato dato sulla finestra vera e
     * non ha mostrato niente.
     * Vale solo con gl=wgl: e' una variabile della nostra opengl32.dll, e sul
     * percorso ANGLE non esiste nessuno che la legga. */
    bool gl_attesa_flush;
    bool audio;       /* aggancio: oggi il guest non ha /dev/snd */
    /* Moltiplicare la risoluzione del guest per la scala dello schermo. Default
     * (del codice) false: chi non scrive la chiave in config.txt non
     * paga nulla. NON e' piu' una leva ignota: il costo a 2560x1600 (200% su
     * questa macchina) e' MISURATO (tabella nel commento della chiave in
     * runtime/bin/config.txt), e la
     * densita' che Android calcolerebbe dall'EDID alla risoluzione alzata --
     * sbagliata di suo -- viene corretta dalla chiave densita' qui sotto. */
    bool scala_guest;
    /* Frequenza da annunciare al guest, 0 = automatico (winq legge il pannello
     * con GetDeviceCaps(VREFRESH), il comportamento di oggi), oppure 24..240.
     * Arriva a QEMU per ambiente (WINQ_HZ), non per riga di comando: vedi il
     * commento in vm.c dove si imposta, accanto a WINQ_GL. Vedi il commento
     * della chiave in runtime/bin/config.txt per la misura che ha
     * deciso 120 come default spedito, e winq-window.c (winq_hz_da_annunciare)
     * per come winq la usa DAVVERO in entrambi i punti che decidono la
     * frequenza -- prima uno solo dei due la leggeva. */
    int hz;
    /* Densita' logica da imporre al guest con "wm density", 0 = non toccare
     * (comportamento di oggi). Applicata UNA VOLTA da main.c appena Android e'
     * pronto (VM_PRONTO), non da qui: config.c non parla con adb. Vedi il
     * commento della chiave in runtime/bin/config.txt per la ragione
     * (l'EDID e la densita' sbagliata che ne segue a una risoluzione alzata)
     * e main.c per il fatto verificato che "wm density" scrive in /data e
     * quindi persiste finche' data.img non cambia. */
    int densita;
    /* Quale immagine di Android si avvia: VAR_VANILLA (default) o VAR_GAPPS.
     * I due percorsi (immagine + dati) vengono dalla tabella di varianti.c
     * tramite var_percorsi, che vm.c consulta invece di cablare i nomi.
     * vendor.img resta lo stesso per entrambe le varianti, perche' tutte le
     * personalizzazioni del progetto -- nome GodziDroid, animazione, appunti,
     * asound.conf, permessi dei nodi -- viaggiano nell'initramfs. */
    VarNome variante;
} Config;

void config_default(Config *c);

/* Non fallisce mai: parte dai default, applica cio' che riesce a leggere, e
 * scrive nel registro ogni riga ignorata con la sua ragione. */
void config_carica(Config *c, const char *percorso);

/* --- percorsi accanto all'eseguibile ------------------------------------- */

/* Costruisce in dove il percorso di nome, nella cartella dell'eseguibile
 * corrente (via GetModuleFileNameA). UNICA implementazione di questa logica:
 * adb_percorso (per adb.exe) e il caricamento di config.txt in main.c
 * la condividono.
 *
 * SENZA QUESTA CONDIVISIONE il file di configurazione che il prodotto spedisce
 * non fa NIENTE: le immagini in guest/images/... vogliono cwd = radice del
 * progetto (dove config.txt non c'e' se letto dalla cartella
 * corrente), mentre lanciando da runtime/bin/ la configurazione si
 * leggerebbe ma mancherebbero le immagini. La lettura di config.txt e
 * di adb.exe non dipende quindi dalla cwd per niente: usa il percorso del
 * modulo, come gia' fa adb_percorso. E' la duplicazione gemella di quella
 * gia' unificata per adb_percorso (vedi il commento la'): sopravvissuta
 * perche' i due usi stavano in due task diversi. Non crearne una terza
 * copia. La cwd stessa la impone il prodotto da solo, PRIMA di tutto il
 * resto: vedi guscio_radice_imposta qui sotto. */
void guscio_percorso_accanto(const char *nome, char *dove, int max);

/* --- la radice del prodotto ----------------------------------------------
 *
 * IL BLOCCANTE 3 DELLA REVISIONE FINALE, misurato lanciando il pacchetto
 * spedito con la cwd che Esplora risorse impone al doppio clic
 * (runtime\bin\, dove sta l'eseguibile): ZERO processi, nessuna finestra,
 * nessun QEMU, nessun registro. Le immagini in guest/images/... si
 * risolvono rispetto alla cwd (vedi vm.c e registro.c), e da runtime\bin\
 * quelle cartelle non ci sono: il prodotto usciva in silenzio.
 *
 * IL RIMEDIO: il prodotto si sceglie la cwd DA SOLO, prima di leggere o
 * scrivere qualunque percorso relativo. guscio_radice_calcola e
 * guscio_radice_verifica sono la parte PURA -- niente Win32, solo stringhe e
 * (per la verifica) una lettura del disco -- e si provano senza un
 * eseguibile vero da spostare in giro. guscio_radice_imposta e' l'unica
 * chiamante di produzione: prende l'eseguibile vero da GetModuleFileNameA e,
 * se la verifica passa, applica SetCurrentDirectoryA. */

/* Calcola in radice la RADICE del prodotto a partire da eseguibile, il
 * percorso completo di un eseguibile che sta in
 * <radice>\runtime\bin\qualcosa.exe: la cartella dell'eseguibile e' un
 * livello sotto "runtime\bin", quindi la radice e' due livelli sopra quella
 * cartella -- tre tagli all'ultima barra in tutto, contando quello che toglie
 * il nome del file. Funzione pura, senza toccare il disco.
 *
 * Ritorna false (radice non toccata) se eseguibile non ha almeno tre barre:
 * capiterebbe solo con un percorso degenere da GetModuleFileNameA, e in quel
 * caso non c'e' una radice da calcolare, non solo da verificare dopo. */
bool guscio_radice_calcola(const char *eseguibile, char *radice, int max);

/* Verifica che radice sia DAVVERO la radice del prodotto, controllando che al
 * suo interno esista runtime\bin\config.txt -- il file che il
 * pacchetto di rilascio spedisce sempre (vedi .gitignore) e che nessun'altra
 * cartella puo' avere per caso. Scrive SEMPRE in provato il percorso
 * controllato, verificato o no: e' cio' che finisce nel messaggio quando la
 * verifica fallisce, perche' un avvertimento che non nomina cosa ha cercato
 * non vale piu' del silenzio che sostituisce. */
bool guscio_radice_verifica(const char *radice, char *provato, int max);

/* Orchestrazione impura e UNICA chiamante di produzione delle due funzioni
 * pure sopra: prende l'eseguibile vero da GetModuleFileNameA, calcola e
 * verifica la radice e, se la verifica passa, la impone come cartella
 * corrente col SetCurrentDirectoryA. Scrive SEMPRE in radice la radice
 * calcolata (verificata o no) e in provato il percorso controllato dalla
 * verifica, cosi' il chiamante compone il messaggio -- di successo o di
 * fallimento -- senza ripetere il calcolo. Ritorna l'esito della verifica.
 *
 * DEVE girare PRIMA di registro_apri(): quest'ultimo apre
 * guest/logs/registro-guscio.log con un percorso RELATIVO (vedi il commento
 * su REG_FILE_DEFAULT in registro.c), e se la cwd cambiasse dopo, il registro
 * finirebbe scritto nel posto sbagliato -- lo stesso sintomo "il registro non
 * si apre in silenzio" gia' documentato li', capovolto: qui sarebbe il
 * registro ad aprirsi nel posto silenzioso. */
bool guscio_radice_imposta(char *radice, int max_radice, char *provato,
                           int max_provato);

/* --- adb.c --------------------------------------------------------------- */

/* Esegue "adb shell <comando>" e cattura lo stdout in uscita (terminato a '\0').
 * Ritorna true se adb esce con 0. uscita puo' essere NULL se non interessa. */
bool adb_esegui(const Config *c, const char *comando, char *uscita, int max);

/* La riga di comando di adb, in un posto solo e senza eseguire niente: e' la
 * parte che si puo' provare. Ritorna la lunghezza scritta, 0 se non ci sta --
 * e in quel caso dest resta vuota, perche' una riga troncata e' un comando
 * DIVERSO, non un comando mancante. */
int adb_riga(char *dest, int max, const char *adb, int porta, const char *coda);

/* Copia in dest l'ultima riga non vuota di uscita, senza il \r finale. E' dove
 * adb scrive "Success" oppure "Failure [...]": il resto e' rumore. Ritorna la
 * lunghezza copiata, 0 se non c'e' niente da mostrare.
 *
 * Si guarda l'USCITA e non il codice di ritorno perche' adb install esce 0
 * anche quando stampa Failure. Stava in apk.c come apk_ultima_riga, dove il
 * nome mentiva: legge l'uscita di adb, e serve identica al push. */
int adb_ultima_riga(const char *uscita, char *dest, int max);

/* Quanto tempo concedere a un trasferimento di TOT byte in TOT file, in ms.
 *
 * Non e' una scadenza di inattivita', e non per scelta: adb NON stampa il
 * progresso su un tubo rediretto -- misurato, 600 MB con sette
 * secondi di tubo muto, e l'unico flag che esiste (-q) serve a sopprimere il
 * progresso, non a pretenderlo. Vedi app/misure/pf-misure-preliminari.md.
 * Quindi il tempo si commisura al LAVORO invece di sorvegliare l'avanzamento.
 *
 * DUE termini, e il secondo e' stato aggiunto dopo una misura che ha smentito la
 * prima versione: i byte da soli non bastano, perche' una cartella di file
 * piccoli costa tempo per FILE (~6,9 ms l'uno misurati) e byte quasi zero. Con i
 * soli byte, una cartella al tetto di 2000 file avrebbe avuto 20 s contro i ~14
 * che serve, cioe' un margine che si rompe sulla prima macchina piu' lenta.
 * PURA, perche' e' l'unica cosa di questo meccanismo che si puo' sbagliare. */
unsigned adb_scadenza_trasferimento(long long byte, int file);

/* Come adb_esegui_grezzo ma con la scadenza DETTA dal chiamante, in ms.
 *
 * La usano push e install: un APK grosso sfonda i 20 s assoluti e veniva ucciso
 * a meta' installazione, quindi questo chiude anche un difetto latente del
 * trascinamento APK. Va chiamata da un THREAD: una scadenza lunga sul thread
 * della finestra la terrebbe ferma per tutto quel tempo. */
bool adb_esegui_a_lungo(const Config *c, const char *coda, char *uscita,
                        int max, unsigned scadenza_ms);

/* Esegue adb SENZA premettere "shell". Serve per i comandi che non sono comandi
 * di shell -- "install" e' il primo -- e adb_esegui ci si appoggia. */
bool adb_esegui_grezzo(const Config *c, const char *coda, char *uscita, int max);

/* true quando il guest ha sys.boot_completed=1. E' la fonte AUTOREVOLE: la
 * regex sulla seriale e' un'inferenza su un file di testo. */
bool adb_pronto(const Config *c);

/* Manda un tasto. keycode e' un nome Android, per esempio "KEYCODE_HOME".
 * MISURATO: questa via funziona, mentre i tasti hardware veri mandati col
 * monitor di QEMU arrivano al kernel e Android li ignora. */
bool adb_keyevent(const Config *c, const char *keycode);

/* Costruisce in dove il percorso di adb.exe, accanto all'eseguibile corrente.
 * Un involucro sottile su guscio_percorso_accanto (vedi sopra): adb_esegui/
 * adb_collega e vm_verifica_file la condividono, cosi' non esistono due copie
 * della stessa costruzione che possano divergere in silenzio (la revisione
 * di quella verifica ne ha trovate gia' due, e diverse). */
void adb_percorso(char *dove, int max);

/* Spegne Android pulito. MISURATO: la seriale chiude con "reboot: Power down" e
 * QEMU esce da se' in 7 s. */
bool adb_spegni(const Config *c);

/* Legge la risoluzione che il guest crede di avere, da "wm size". Serve a
 * verificare che una rotazione sia stata recepita. */
bool adb_risoluzione_guest(const Config *c, int *w, int *h);

/* --- vm.c -------------------------------------------------------------- */

/* Scrive in buf gli argomenti per QEMU, senza il nome dell'eseguibile, e
 * ritorna i byte scritti (0 se non ci stanno). */
int vm_argomenti(const Config *c, char *buf, int max);

/* Verifica che tutti i file necessari esistano. Se ne manca uno, ne scrive il
 * percorso in mancante e ritorna false. */
bool vm_verifica_file(const Config *c, char *mancante, int max);

/* Ritorna il percorso indice-esimo dell'elenco dei file necessari PER LA
 * VARIANTE DATA, o NULL oltre la fine. UNICA fonte di verita' per quell'elenco:
 * vm_verifica_file la usa invece di scorrere un array direttamente, e le prove
 * la usano per controllare che l'elenco concordi con vm_argomenti. Gli ultimi
 * due indici sono le immagini di sistema e di dati DELLA VARIANTE data, prese
 * da var_percorsi -- la STESSA funzione che vm_argomenti chiama per scegliere
 * cosa passare a QEMU -- cosi' i due elenchi non possono divergere come e'
 * successo quando l'array era cablato sulla vanilla. */
const char *vm_file_necessario(VarNome variante, int indice);

/* --- vm.c, ciclo di vita ----------------------------------------------- */

typedef enum {
    VM_PREPARA,
    VM_AVVIA,
    VM_ATTESA_KERNEL,
    VM_ATTESA_ANDROID,
    VM_PRONTO,
    VM_SPEGNIMENTO,
    VM_USCITO,        /* uscito perche' GLIELO ABBIAMO CHIESTO: il guscio chiude */
    VM_MORTA,         /* uscito DA SE': il guscio resta aperto col registro */
    VM_FALLITA
} VmStato;

void vm_apri(const Config *c);

/* Fa avanzare la macchina. Da chiamare ogni 500 ms da un timer: nessun passo
 * blocca, perche' il guscio deve restare reattivo mentre il guest avvia. */
VmStato vm_passo(void);
VmStato vm_stato(void);
bool vm_viva(void);
void vm_avvia_spegnimento(void);
void vm_chiudi(void);

/* Cosa fare quando l'utente chiude la finestra di Android, in funzione dello
 * stato in cui si trova la VM in quel momento. */
typedef enum {
    CHIUSURA_SPEGNI,   /* avviare lo spegnimento pulito (adb reboot -p) */
    CHIUSURA_ATTENDI,  /* uno spegnimento e' gia' in corso: non fare nulla */
    CHIUSURA_DURA      /* nessuno spegnimento pulito e' possibile: uccidere */
} AzioneChiusura;

/* Decide l'azione. ESTRATTA da main.c apposta: prima era un "if" a due rami
 * inline nel gestore di WM_APP+1, e nessuna prova poteva raggiungerla. E'
 * la decisione dietro il Bloccante Critico "chiudere due volte stacca la
 * corrente al guest": un secondo clic durante VM_SPEGNIMENTO finiva nello
 * stesso ramo di uscita dura di oggi, uccidendo QEMU a meta' di un
 * reboot -p su data.img montato cache=writeback. Vedi vm.c per la ragione
 * di ogni singolo stato. */
AzioneChiusura vm_azione_chiusura(VmStato s);

/* --- finestra.c -------------------------------------------------------- */

#include <windows.h>

bool finestra_apri(const Config *c);

/* Da chiamare dal timer: riversa le righe nuove del registro nel pannello. */
void finestra_passo(void);
HWND finestra_handle(void);
void finestra_fase(const char *nome, const char *esito);
void finestra_bottoni(bool attivi);

/* Aggiorna SOLO l'etichetta del bottone "tasti" (tasti / tasti ON). Lo
 * stato acceso/spento vive in main.c e in nessun altro posto (vedi il
 * commento su ID_HOTKEY_TASTI in main.c): qui si scrive soltanto il testo
 * che main.c ha gia' deciso, stesso principio di finestra_fase, che riceve
 * l'esito gia' deciso invece di calcolarlo. */
void finestra_bottone_tasti(bool accesa);

/* I MESSAGGI DEL GUSCIO (WM_APP+1 chiusura, WM_APP+2 bottoni, i tre
 * GUSCIO_MSG_* piu' sotto) e chi li esegue: main.c registra qui la propria
 * funzione, e fin_wndproc gliela chiama.
 *
 * PERCHE' NON BASTA GUARDARLI NEL CICLO DEI MESSAGGI, dove stavano. Sono
 * messaggi POSTATI A UNA FINESTRA: il ciclo di main() li vedeva perche'
 * controllava msg.message prima di DispatchMessage, ma quel ciclo non e'
 * l'unico a pompare. Ogni MessageBoxA e ogni TrackPopupMenu ne fa girare uno
 * PROPRIO, dentro USER32, che di quel controllo non sa nulla: si limita a
 * consegnarli a fin_wndproc, dove cadevano in DefWindowProcA e SPARIVANO.
 *
 * COSA SI ROMPEVA, e non era un caso di scuola: WM_QUERYENDSESSION arriva
 * anche a finestra disabilitata da una modale, posta WM_APP+1, e quel WM_APP+1
 * veniva buttato -- durante un ARRESTO DI WINDOWS lo spegnimento pulito della
 * VM non partiva mai, e Android moriva di corrente staccata con data.img
 * montata cache=writeback. Stesso destino per un WM_CLOSE dal Gestione
 * attivita' e per un GUSCIO_MSG_VARIANTE_PRONTA arrivato mentre l'utente
 * guardava una conferma. La finestra di perdita si e' aperta quando la
 * conferma del cambio variante e' passata sul thread della finestra: prima la
 * modale girava altrove e il ciclo di main() continuava a pompare.
 *
 * Consegnarli dal wndproc li rende immuni: il wndproc lo chiamano TUTTI i
 * cicli, annidati compresi. */
typedef void (*FinMsgGuscio)(UINT msg, WPARAM wp, LPARAM lp);
void finestra_messaggi_guscio(FinMsgGuscio f);

/* Identificativi dei bottoni, usati anche da main.c per il cablaggio. */
#define ID_POWER    1001
#define ID_VOL_SU   1002
#define ID_VOL_GIU  1003
#define ID_HOME     1004
#define ID_BACK     1005
#define ID_RECENTI  1006
#define ID_RUOTA    1007
#define ID_TASTI    1008
#define ID_VARIANTE 1009

/* --- tubo.c -------------------------------------------------------------- */

bool tubo_risoluzione(int w, int h);

/* Le tre funzioni della mappa dei tasti, modellate su tubo_risoluzione:
 * aprono il tubo, scrivono un comando, chiudono. Il fallimento (tubo
 * assente o occupato) e' gia' diagnosticato dentro tubo.c: chi chiama vede
 * solo true/false e decide se aggiornare il proprio stato.
 *
 * tubo_mappa_impara manda "mappa impara %s %s\n" con TIPO ("tocco" oppure
 * "joystick") E percorso, NON solo percorso: il piano di questo task
 * diceva "mappa impara <percorso>", ma mandarlo al prodotto vivo ha
 * risposto `comando "mappa impara" malformato` -- winq si aspetta il tipo
 * prima del percorso, perche' senza saperlo non distingue se aspettarsi un
 * tasto piu' un clic (tocco) oppure due clic (joystick). percorso e' tutto
 * cio' che segue il primo spazio dopo tipo e non si spezza a campi, perche'
 * puo' contenere spazi. */
bool tubo_mappa_carica(const char *percorso);
bool tubo_mappa_spegni(void);
bool tubo_mappa_impara(const char *tipo, const char *percorso);

/* --- dpi.c --------------------------------------------------------------- */

/* Dichiara al sistema che questo processo conosce il DPI. DA CHIAMARE COME
 * PRIMA ISTRUZIONE DI main(), prima di qualunque finestra: la dichiarazione NON
 * ha effetto retroattivo su una finestra gia' creata, quindi "presto" non basta
 * -- serve "prima". */
void dpi_dichiara(void);

/* Una riga che dice cosa e' stato dichiarato, da versare nel registro DOPO
 * registro_apri(). Esiste perche' dpi_dichiara() gira prima che il registro
 * esista, e una dichiarazione fallita in silenzio sarebbe indistinguibile da
 * una riuscita: il sintomo (finestra sfocata) e' identico a quello di ieri.
 * Mai NULL. */
const char *dpi_esito(void);

/* Il DPI della finestra. Ritorna 96 -- il riferimento di Win32 -- quando non si
 * puo' sapere, cosi' chi chiama non deve distinguere il caso. */
UINT dpi_di_finestra(HWND h);

/* Il DPI dello schermo primario. Serve dove una finestra non c'e' ancora, come
 * nei modi --argomenti e --config, che non ne aprono nessuna. Ritorna 96 quando
 * non si puo' sapere. */
UINT dpi_di_sistema(void);

/* Scala un valore in pixel da 96 DPI al DPI dato, arrotondando al piu' vicino.
 * Funzione PURA, ed e' l'unica parte di dpi.c che si prova: il resto e' Win32 e
 * si verifica dall'esterno con qemu/scripts/verifica-dpi.ps1. Un dpi di 0
 * viene trattato come 96, o ogni controllo uscirebbe di dimensione zero. */
int dpi_scala(int valore, UINT dpi);

/* Moltiplica larghezza e altezza di c per la scala, se c->scala_guest e' accesa.
 * Funzione PURA -- il DPI si passa invece di leggerlo -- cosi' si prova senza
 * aprire una finestra. Rifiuta INTERO l'aumento se una delle due dimensioni
 * superasse 8192, lo stesso tetto che config_carica impone alle due chiavi:
 * limitarne una sola cambierebbe le proporzioni del guest. Scrive nel registro
 * sia cio' che ha fatto sia cio' che ha rifiutato. */
void dpi_applica_scala_guest(Config *c, UINT dpi);

/* La geometria del framebuffer da chiedere a QEMU per un dato VERSO.
 *
 * PERCHE' NON BASTA SCAMBIARE w e h, ed e' il difetto che questa funzione chiude:
 * la rotazione chiedeva "scambia cio' che dice wm size", e wm size in questo QEMU
 * NON CAMBIA MAI. MISURATO nel registro del prodotto -- tre
 * pressioni, tre volte lo stesso bersaglio (2560x1600 -> 1600x2560) mentre il
 * verso alternava correttamente. Effetto: dopo la prima pressione il framebuffer
 * restava verticale per sempre, e quando Android tornava orizzontale il contenuto
 * finiva scalato dentro le bande.
 *
 * Qui il bersaglio dipende SOLO dal verso: orizzontale = lato lungo in larghezza,
 * verticale = lato corto in larghezza, qualunque sia il verso di partenza. w e h
 * servono solo per le due MISURE, non per decidere. Con una geometria non valida
 * (zero o negativa) azzera il bersaglio, perche' mandare a QEMU una risoluzione
 * assurda e' peggio che non mandare niente. */
void dpi_ruota(int w, int h, bool verso_orizzontale, int *bw, int *bh);

/* --- archivio.c: conservare le prove del giro precedente ----------------- */

/* Quanti archivi si tengono per ciascuno dei due file. Dieci perche' il difetto
 * che ha reso necessario questo modulo (system_server che muore) si e' presentato
 * tre volte in quattro giorni: dieci avvii coprono la finestra in cui e'
 * ragionevole aspettarsi la prossima occorrenza, senza far crescere la cartella
 * senza fine. Un tetto a 0 disattiva l'archiviazione e ripristina il
 * comportamento di prima di questo modulo. */
#define ARCH_QUANTI 10

/* La cartella dove finiscono, accanto ai log che archivia. */
#define ARCH_CARTELLA "guest/logs/archivio"

/* "<cartella>\<prefisso>-AAAAMMGG-hhmmss.log", con gli zeri davanti perche'
 * l'ordine alfabetico deve essere quello cronologico: la potatura cancella i
 * primi, e senza gli zeri "9" verrebbe dopo "10". L'ora arriva come parametro e
 * non si legge qui: cosi' il nome e' una decisione PURA e si prova con un valore
 * noto invece di cambiare risposta a ogni secondo. Ritorna 0 e azzera dest se non
 * ci sta. */
int archivio_nome(char *dest, int max, const char *cartella,
                  const char *prefisso, const SYSTEMTIME *ora);

/* Sposta `percorso` dentro `cartella` con un nome datato, poi cancella i piu'
 * vecchi finche' non ne restano al massimo `quanti`.
 *
 * true = il file di prima e' stato conservato. false = non c'era niente da
 * conservare (primo avvio), oppure non si e' potuto conservarlo -- e in quel caso
 * il file attivo viene CANCELLATO comunque, perche' un file che sopravvive
 * all'avvio e' la trappola del "boot_completed di due ore prima" che vm.c evita
 * per costruzione: perdere una prova e' meno grave che leggere dati vecchi
 * credendoli nuovi. */
bool archivio_ruota(const char *percorso, const char *cartella,
                    const char *prefisso, int quanti, const SYSTEMTIME *ora);

/* --- apk.c: installare un APK trascinato -------------------------------- */

/* Vero se il nome finisce per ".apk", senza distinguere le maiuscole (Windows
 * non le distingue). Guarda solo il NOME: l'esistenza del file la scopre adb, e
 * un controllo in piu' qui sarebbe una seconda verita' da tenere allineata. */
bool apk_e_apk(const char *percorso);

/* Costruisce "install -r \"<percorso>\"". Ritorna la lunghezza, 0 se non ci sta
 * (e dest resta vuota). Nessuna fuga di caratteri: un percorso di Windows non
 * puo' contenere una virgoletta doppia. */
int apk_comando(char *dest, int max, const char *percorso);

/* --- rilascio.c: l'orchestrazione del trascinamento --------------------- */

/* Voci trascinate in un colpo. Sedici come per gli APK: oltre, il rilascio si
 * rifiuta INTERO invece di farne una parte e tacere sul resto. */
#define RIL_MAX_VOCI   16

/* File dentro le cartelle trascinate.
 *
 * Il conteggio avviene dentro il wndproc -- serve per dichiarare "214 file,
 * 1,3 GB" nel dialogo, e per dirlo bisogna averlo contato -- quindi un albero
 * enorme congelerebbe la finestra proprio mentre deve raccontare cosa sta
 * facendo. MISURATO (test-rilascio, che stampa il tempo):
 * duemila file percorsi in 0,5 ms, contro il battito da 500 ms della finestra e
 * i 5 ms della pompa dell'input. Il margine e' di tre ordini di grandezza.
 *
 * RISERVA sulla misura: l'albero era stato creato dalla prova un istante prima,
 * quindi i metadati NTFS erano caldi. Su un albero freddo, o su una cartella di
 * rete, costerebbe di piu' -- e resta comunque il tetto a fermarlo. */
#define RIL_MAX_ALBERO 2000

/* L'avviso che il dialogo porta SEMPRE quando si copiano file.
 *
 * Dice "rimosso" e non "sostituito" per una ragione MISURATA : un
 * file era stato copiato (44 byte, verificati), il secondo tentativo sullo stesso
 * nome e' fallito con "remote couldn't create file", e nel guest non e' rimasto
 * NIENTE -- ne' il nuovo ne' il vecchio. adb apre il file remoto prima di sapere
 * se riuscira', quindi la promessa "verranno sostituiti" descrive solo l'esito
 * buono. Sostituire riesce nel caso normale (provato piu' volte), ma chi conferma
 * deve sapere che il file di prima e' comunque perduto.
 *
 * Controllare le collisioni prima costerebbe un adb dentro il percorso della UI,
 * che bloccherebbe la finestra fino alla propria scadenza: si dichiara invece di
 * controllare. */
#define RIL_AVVISO "Un file con lo stesso nome viene rimosso, e se la copia " \
                   "non riesce non si recupera."

typedef struct {
    int voci_apk;
    int voci_file;                     /* file singoli + cartelle */
    int file_totali;                   /* dopo aver percorso gli alberi */
    long long byte;                    /* somma, per dichiararla nel dialogo */
    char apk[RIL_MAX_VOCI][MAX_PATH];
    char file[RIL_MAX_VOCI][MAX_PATH];
    /* La dimensione di OGNI voce, non solo la somma: la scadenza di adb si
     * calcola per trasferimento, e usare il totale per ognuno concederebbe a un
     * file da 1 MB il tempo di tutto il rilascio. Il totale serve al dialogo,
     * questi alla scadenza: due usi diversi, due numeri diversi. */
    long long byte_apk[RIL_MAX_VOCI];
    long long byte_file[RIL_MAX_VOCI];
    /* Quanti file contiene ogni voce: 1 per un file, il conteggio dell'albero
     * per una cartella. Serve alla scadenza insieme ai byte, perche' una cartella
     * di file piccoli costa tempo per FILE e byte quasi zero -- misurato. */
    int file_voce[RIL_MAX_VOCI];
} Piano;

/* Vero se il rilascio va rifiutato INTERO. I confini sono provati, non un valore
 * centrale: sul divieto della porta adb questo errore e' gia' stato fatto. */
bool rilascio_tetto_superato(int voci, int file_totali);

/* La taglia in italiano: "1,3 GB", "740 MB", "980 byte". Una cifra decimale
 * sola, perche' chi legge deve decidere se e' tanto, non fare i conti. */
int  rilascio_taglia(char *dest, int max, long long byte);

/* Il testo del dialogo. PURA, cosi' singolari, plurali e numeri si provano senza
 * aprire una finestra. Ritorna 0 (e dest vuota) se non c'e' niente da chiedere:
 * un dialogo che chiede il permesso di non fare niente non si mostra. */
int  rilascio_messaggio(char *dest, int max, const Piano *p);

/* Tre esiti di una percorrenza, e sono tre perche' "contati tutti" e "il
 * conteggio non e' affidabile" hanno conseguenze diverse: il primo procede, il
 * secondo deve rifiutare il rilascio. Un bool li avrebbe fusi, ed e' quello che
 * faceva la prima versione. */
typedef enum {
    RIL_ALBERO_COMPLETO,   /* contati tutti i file dell'albero */
    RIL_ALBERO_TETTO,      /* fermata: oltre il tetto, e il rilascio si rifiuta */
    RIL_ALBERO_INCERTO     /* il conteggio NON e' affidabile: vedi sotto */
} RilAlbero;

/* Percorre una cartella sommando file e byte, ricorsivamente.
 *
 * RIL_ALBERO_TETTO: si e' fermata perche' oltre il tetto il rilascio viene
 * rifiutato comunque, e continuare a contare costerebbe tempo nel wndproc.
 *
 * RIL_ALBERO_INCERTO: la cartella non si e' potuta leggere, oppure l'albero e'
 * piu' profondo di RIL_PROFONDITA, oppure un percorso sfonda MAX_PATH. PERCHE'
 * CONTA, e perche' non e' "zero file": `adb push` e' ricorsivo per conto suo e
 * NON conosce i nostri limiti, quindi copierebbe piu' di quanto abbiamo contato
 * -- e su un conteggio piccolo si calcolano una scadenza troppo corta (il
 * trasferimento sano viene ucciso) e un dialogo che dichiara all'utente meno di
 * cio' che sta per fare. Chi chiama deve rifiutare, non procedere.
 *
 * I collegamenti (reparse point) si saltano, perche' una giunzione che punta a un
 * antenato farebbe girare la ricorsione per sempre; ogni salto finisce nel
 * registro, perche' cosa faccia `adb push` con un reparse point NON e' misurato e
 * il conteggio potrebbe restare sotto anche per quella via. */
RilAlbero rilascio_percorri(const wchar_t *cartella, int tetto,
                            int *file, long long *byte);

/* Classifica un rilascio e misura gli alberi. Niente UI, niente adb.
 *
 * Il Piano lo alloca il chiamante: ~8,6 KB, che stanno sulla pila del wndproc
 * senza malloc. false = niente da fare (nessuna voce valida, oppure il rilascio
 * e' stato rifiutato INTERO perche' sfonda un tetto): la ragione e' gia' nel
 * registro, e il chiamante non deve aggiungerne una seconda. */
bool rilascio_prepara(HDROP h, Piano *p);

/* Esegue: install prima, push dopo, su un thread, uno per volta.
 *
 * Lo stato ARRIVA come parametro invece di essere letto con vm_stato(): cosi'
 * questo modulo non dipende da vm.c, il guardiano si prova senza linkare la
 * macchina a stati, e il modulo resta "gli si dice in che stato siamo" invece di
 * "va a cercarlo". Chi chiama e' finestra.c, che la conosce gia'. */
bool rilascio_avvia(const Config *c, const Piano *p, VmStato stato);

/* --- file.c: copiare un file trascinato nel guest ----------------------- */

/* Vero se il nome sopravvive alla conversione nella tabella ANSI di questa
 * macchina senza sostituzioni.
 *
 * Serve perche' tutto il guscio usa le API ...A: un nome accentato passa (sulla
 * 1252 ha byte propri, e Windows lo riconverte in UTF-16 per il processo
 * figlio), uno cirillico o con emoji no -- DragQueryFileA lo restituisce gia'
 * rovinato. Meglio saltare quella voce dicendolo che copiarla con un nome
 * diverso da quello che l'utente vede. */
bool file_nome_trasportabile(const wchar_t *nome);

/* Costruisce "push \"<percorso>\" \"/sdcard/Download/\"". Ritorna la lunghezza,
 * 0 se non ci sta (e dest resta vuota).
 *
 * La barra finale del bersaglio non e' cosmetica: senza, adb crea un FILE di
 * nome Download quando la cartella non esiste. */
int file_comando_push(char *dest, int max, const char *percorso);

/* --- dati.c: creare il /data vuoto quando manca ------------------------- */

/* Il comando "tar.exe" per estrarre archivio dentro cartella_temp.
 *
 * PURA, ed e' la parte che si sbaglia in silenzio: le virgolette attorno ai
 * due percorsi non sono ornamento, sia l'archivio (dentro "runtime\") sia la
 * cartella temporanea (accanto a percorso_dati, vedi dati_cartella_temp)
 * possono avere spazi. Ritorna la lunghezza scritta, 0 se non ci sta (e dest
 * resta vuota) -- un comando troncato e' un comando DIVERSO, non un comando
 * mancante. */
int dati_comando_estrai(char *dest, int max, const char *archivio,
                        const char *cartella_temp);

/* Costruisce in dest la cartella temporanea dove estrarre l'archivio,
 * ACCANTO a percorso_dati (stessa cartella madre) -- MAI sotto %TEMP%.
 *
 * PERCHE' NON %TEMP%, nominando il caso che questa funzione chiude: la
 * cartella del prodotto puo' stare su un volume DIVERSO da %TEMP%, perche'
 * chi installa puo' scegliere un altro disco e niente lo impedisce. Estraendo
 * la' e poi spostando il risultato su percorso_dati con MoveFileA, un salto
 * fra due volumi non e' una rinomina: e' una COPIA di 4 GiB seguita da una
 * cancellazione, che dura secondi interi. Uno spegnimento forzato o un crash
 * a meta' di quella copia lascia un data.img TRONCATO gia' al percorso
 * definitivo -- e la guardia di dati_crea_se_manca (il controllo
 * GetFileAttributesA sul file di destinazione) e' un semplice controllo di
 * ESISTENZA: quel file mutilato verrebbe creduto "gia' a posto" PER SEMPRE,
 * senza mai ritentare, e l'initramfs lo monterebbe cosi' pur di avviare.
 *
 * Estraendo invece nella STESSA cartella di percorso_dati, i due percorsi
 * sono sullo stesso volume per costruzione (non serve interrogare Windows per
 * saperlo), e MoveFileA torna una rinomina atomica del filesystem: o il file
 * arriva intero al percorso finale, o non arriva affatto. Il caso sopra non
 * puo' piu' esistere.
 *
 * PURA -- nessuna chiamata Win32, solo costruzione di stringa -- si prova
 * senza toccare il disco. Ritorna la lunghezza scritta, 0 se non ci sta (e
 * dest resta vuota), stesso principio di dati_comando_estrai. */
int dati_cartella_temp(char *dest, int max, const char *percorso_dati);

/* Vero se l'errore Win32 di uno spostamento fallito (MoveFileA/MoveFileExA)
 * puo' essere TRANSITORIO -- il genere che un antivirus o un indicizzatore
 * causano tenendo per un istante un file appena scritto, e che percio' vale
 * la pena riprovare invece di arrendersi subito: ERROR_ACCESS_DENIED (5) ed
 * ERROR_SHARING_VIOLATION (32). Vedi dati_sposta_con_riprova qui sotto per
 * la misura sul prodotto vivo che giustifica l'elenco.
 *
 * PURA -- nessuna chiamata Win32, solo un confronto -- ed e' la sola
 * decisione di questo schema che si possa sbagliare in silenzio senza un
 * test: il resto tocca il disco davvero e si verifica a mano. */
bool dati_sposta_errore_e_transitorio(DWORD errore);

/* Sposta origine su destinazione con MoveFileExA(flag_move), riprovando fino
 * a cinque volte IN PIU' (sei tentativi in tutto) quando l'errore e'
 * dati_sposta_errore_e_transitorio, con pause crescenti di 200, 400, 800,
 * 1600 e 3200 ms fra un tentativo e il successivo -- poco piu' di sei
 * secondi in tutto. Ogni altro errore fa uscire al primo tentativo, senza
 * aspettare su un guasto che il tempo non cambia.
 *
 * NON E' UN'IPOTESI. Sul prodotto vivo, dopo aver scaricato e verificato
 * 1,17 GB della GAPPS (...system.zip), l'estrazione e' riuscita ma questa
 * stessa MoveFileExA e' fallita con ERROR_ACCESS_DENIED (5) spostando
 * system.img sulla destinazione finale -- che NON era di sola lettura
 * (attributi: solo Archive). Misurando il blocco su quel file a intervalli
 * dopo un avvio: BLOCCATO a 3 s (mentre se ne calcola l'impronta
 * all'avvio), LIBERO a 15 s e a 45 s -- a regime nessuno lo tiene.
 * Riprovando A MANO lo stesso identico cambio pochi minuti dopo -- senza
 * riscaricare, perche' l'archivio verificato era rimasto sul disco -- E'
 * RIUSCITO. Il candidato piu' probabile e' un antivirus che si sveglia sui
 * 2,6 GB appena scritti; non lo controlliamo, ma un conflitto di
 * condivisione su Windows E' transitorio per natura, e riprovarlo e' il
 * modo corretto di gestirlo -- buttare via 1,17 GB scaricati e verificati
 * al primo no e' IL DIFETTO, non il blocco stesso.
 *
 * fase entra in ogni riga di registro scritta qui dentro ("immagine" per
 * variante_estrai_immagine, "/data" per dati_crea_se_manca): cosi' due
 * spostamenti che riprovano nello stesso momento non lascerebbero righe
 * indistinguibili.
 *
 * true al primo tentativo che riesce, con una riga di registro SOLO se non
 * e' stato il primo (dice a quale tentativo): il caso comune -- riuscito
 * subito -- non deve costare una riga in piu' a ogni singolo spostamento
 * del prodotto. Ogni tentativo fallito e riprovato scrive la propria riga
 * PRIMA di aspettare, cosi' il registro dice da solo quanti se ne sono
 * fatti e con quali pause, invece di doverlo dedurre.
 *
 * false se anche l'ultimo tentativo fallisce: *ultimo_errore riceve il
 * codice Win32 di QUELL'ultimo tentativo, *tentativi_fatti quanti se ne
 * sono fatti in tutto. Non compone nessun messaggio per l'utente: lo fa chi
 * chiama, che conosce origine e destinazione con tutto il loro significato
 * (un'immagine di sistema o il /data, un archivio scaricato che resta sul
 * disco o no) -- questa funzione conosce solo la meccanica dello
 * spostamento. */
bool dati_sposta_con_riprova(const char *origine, const char *destinazione,
                             DWORD flag_move, const char *fase,
                             DWORD *ultimo_errore, int *tentativi_fatti);

/* Crea percorso_dati estraendo archivio, se percorso_dati non c'e' gia'.
 *
 * Se il file c'e' gia' (il caso comune, e anche quello di ogni riavvio dopo
 * il primo), non fa NIENTE e ritorna true senza toccare il disco oltre a
 * quel controllo: creato, se non NULL, resta false. Se manca, cerca
 * "tar.exe" nel PATH -- quello che Windows 11 spedisce di serie -- estrae
 * archivio in una cartella temporanea ACCANTO a percorso_dati (vedi
 * dati_cartella_temp per il perche' non e' %TEMP%; tar.exe -xf non lascia
 * comunque scegliere il nome in uscita, quindi non si puo' estrarre
 * direttamente su percorso_dati) e sposta il "data.img" che ne esce su
 * percorso_dati CON dati_sposta_con_riprova (stesso rischio di conflitto di
 * condivisione transitorio dell'immagine di sistema, su un file da 4 GiB
 * invece che da 1,6-2,6 GB), mettendo *creato a true. La cartella temporanea viene
 * cancellata sia sul successo sia su ogni fallimento successivo alla sua
 * creazione, cosi' un tentativo andato storto non abbandona 4 GiB sul disco.
 *
 * false con errore riempito su ogni fallimento: tar.exe assente (nominato per
 * nome, insieme a Windows 11, invece di un errore di processo che non
 * spiega niente), la cartella temporanea non creabile (con la ragione di
 * Windows, distinta dal caso "esiste gia'" che invece va bene), l'avvio o
 * l'uscita di tar.exe, lo spostamento finale. */
bool dati_crea_se_manca(const char *archivio, const char *percorso_dati,
                        bool *creato, char *errore, size_t errore_n);

/* --- appunti.c: gli appunti condivisi con il guest ---------------------- */

/* I TRE MESSAGGI hanno un nome, mentre WM_APP+1 (chiusura) e WM_APP+2
 * (bottoni) stanno scritti a numero nudo dove nascono e dove si consumano. La
 * differenza non e' gusto: quei due li manda finestra.c a main.c, due file che
 * chi legge apre insieme; questi li manda un THREAD (il server degli appunti,
 * lo scaricatore delle varianti), e un "WM_APP + 4" sparso in due file lontani
 * e' esattamente il numero che un giorno qualcuno riusa per un altro messaggio
 * -- con l'effetto di far incollare negli appunti la pressione di un bottone. */
#define GUSCIO_MSG_APPUNTI_HOST   (WM_APP + 3)   /* fin_wndproc -> main.c */
#define GUSCIO_MSG_APPUNTI_GUEST  (WM_APP + 4)   /* thread server -> main.c */
/* Il cambio di variante e' pronto per essere applicato (scaricato, estratto,
 * verificato, scritto in variants.txt). Sta QUI e non piu' dentro main.c
 * perche' adesso lo conosce anche finestra.c: e' fin_wndproc a consegnarlo,
 * vedi finestra_messaggi_guscio. */
#define GUSCIO_MSG_VARIANTE_PRONTA (WM_APP + 5)  /* thread varianti -> main.c */

/* Mette il guscio in ascolto su 127.0.0.1:porta e avvia il thread del server.
 *
 * false = gli appunti condivisi non funzioneranno, e la ragione e' gia' nel
 * registro. NON e' un motivo per non avviare il guscio: e' una funzione in piu',
 * e la sua assenza non tocca nient'altro. Va chiamata DOPO finestra_apri: il
 * thread consegna cio' che riceve alla finestra, e senza finestra non c'e' un
 * posto dove consegnarlo. */
bool appunti_avvia(int porta);

/* Ferma il thread e chiude tutto. Va chiamata PRIMA di registro_chiudi: il
 * thread scrive nel registro, e chiuderlo sotto di lui sarebbe una scrittura su
 * un FILE gia' chiuso.
 *
 * false = l'attesa e' SCADUTA e il thread e' ancora vivo. Chi chiama deve
 * allora saltare registro_chiudi: quel thread puo' essere dentro registro_riga
 * oltre il controllo "if (!reg_aperto) return;", che sta fuori dal lucchetto,
 * e distruggere la sezione critica sotto di lui e' comportamento non definito.
 * Il registro svuota a ogni riga, quindi non chiuderlo non perde niente.
 * Stessa forma e stessa ragione in variante_ferma (main.c). */
bool appunti_ferma(void);

/* Gli appunti di Windows sono cambiati: da chiamare sul thread della finestra
 * quando arriva GUSCIO_MSG_APPUNTI_HOST. Legge, converte in UTF-8 e lascia il
 * testo al thread del server -- che lo manda al giro dopo, perche' una send
 * lenta sul thread della finestra congelerebbe il pannello del registro.
 *
 * Un formato non testuale si ignora IN SILENZIO (succede a ogni copia di
 * un'immagine); l'assenza del guest costa una riga di registro e nient'altro. */
void appunti_host_cambiati(void);

/* E' arrivato del testo dal guest: da chiamare sul thread della finestra quando
 * arriva GUSCIO_MSG_APPUNTI_GUEST, che il thread del server posta appunto
 * perche' SetClipboardData vuole il thread che possiede la finestra.
 *
 * QUARTA funzione oltre alle tre del piano, e la ragione e' che le altre tre non
 * bastavano: il verso guest -> Windows deve attraversare i due thread, e il
 * salto ha bisogno di un punto in cui main.c lo faccia atterrare -- come gia'
 * fa per i bottoni con WM_APP+2. L'alternativa (una finestra nascosta tutta di
 * questo modulo) avrebbe evitato la riga in main.c introducendo un secondo
 * wndproc nel guscio: piu' codice per meno cablaggio visibile. */
void appunti_guest_arrivato(void);

/* Il thread e il lucchetto NON stanno piu' qui: se ne e' andati in rilascio.c,
 * dove un lucchetto SOLO copre install e push insieme. Due adb in parallelo sullo
 * stesso dispositivo non danno un errore chiaro, ne danno uno confuso, e con due
 * lucchetti separati quel caso sarebbe stato raggiungibile. APK_MAX_FILE e'
 * diventata RIL_MAX_VOCI, perche' il limite e' del rilascio e non degli APK. */

/* --- l'hypervisor di Windows -------------------------------------------
 *
 * QEMU gira con -accel whpx. Se la funzionalita' "Piattaforma hypervisor
 * Windows" non e' attiva, QEMU esce SUBITO e chi installa vede un errore di
 * QEMU senza nessuna riga che nomini la causa vera.
 *
 * TRE esiti e non due, ed e' la decisione di disegno che conta: "non sono
 * riuscito a chiederlo" NON e' "non c'e'". Solo HYPERV_NO ferma l'avvio; se una
 * versione di Windows che non conosciamo, o un antivirus, impedisse la
 * chiamata, fermare sarebbe peggio che provarci. Vedi hyperv_deve_fermare, che
 * e' separata proprio per poter essere provata. */
typedef enum {
    HYPERV_SI,
    HYPERV_NO,
    HYPERV_NON_SO
} HypervEsito;

HypervEsito hyperv_presente(void);
bool hyperv_deve_fermare(HypervEsito e);

#endif /* GUSCIO_H */
