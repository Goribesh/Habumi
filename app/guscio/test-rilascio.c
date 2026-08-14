/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-rilascio.c -- le decisioni dell'orchestrazione del rilascio.
 *
 * Il rilascio vero vuole un HDROP, che vale solo dentro il processo che l'ha
 * creata, e la copia vera vuole una VM: quelli li verifica la prova sul prodotto
 * vivo. Qui si provano il tetto, la taglia e il messaggio -- cioe' le cose che
 * l'utente legge prima di dire di si'. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include "guscio.h"

static int totali = 0;
static int fallimenti = 0;

#define CHECK(expr) do { \
    totali++; \
    if (!(expr)) { \
        fallimenti++; \
        printf("FALLITO %s:%d  %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static void il_tetto_e_provato_sui_confini(void)
{
    /* I CONFINI ESATTI, non un valore centrale: sui confini del divieto della
     * porta adb questo errore e' gia' stato fatto in questo repo, e un
     * off-by-one non si sarebbe visto. */
    CHECK(!rilascio_tetto_superato(16, 0));
    CHECK(rilascio_tetto_superato(17, 0));
    CHECK(!rilascio_tetto_superato(1, 2000));
    CHECK(rilascio_tetto_superato(1, 2001));
    CHECK(!rilascio_tetto_superato(0, 0));
}

static void scrive_la_taglia_in_italiano(void)
{
    char t[32];

    CHECK(rilascio_taglia(t, sizeof(t), 0) > 0);
    CHECK(strcmp(t, "0 byte") == 0);
    CHECK(rilascio_taglia(t, sizeof(t), 1023) > 0);
    CHECK(strcmp(t, "1023 byte") == 0);
    CHECK(rilascio_taglia(t, sizeof(t), 1024) > 0);
    CHECK(strcmp(t, "1,0 KB") == 0);
    CHECK(rilascio_taglia(t, sizeof(t), 1536) > 0);
    CHECK(strcmp(t, "1,5 KB") == 0);
    CHECK(rilascio_taglia(t, sizeof(t), 1048576) > 0);
    CHECK(strcmp(t, "1,0 MB") == 0);
    /* 1,3 GB: il numero che sta nell'esempio della spec. 1,3 GiB esatti sono
     * 1395864371,2 byte, quindi il primo intero che li raggiunge e' ...72. */
    CHECK(rilascio_taglia(t, sizeof(t), 1395864372LL) > 0);
    CHECK(strcmp(t, "1,3 GB") == 0);
    /* UN BYTE SOTTO, e la risposta cambia: la taglia TRONCA, non arrotonda.
     * Questo caso esiste perche' il piano aveva sbagliato proprio qui, e senza
     * di esso qualcuno passerebbe all'arrotondamento credendo di correggere un
     * difetto. Troncare e' la scelta: "1,2 GB" non promette mai piu' spazio di
     * quello che serve. */
    CHECK(rilascio_taglia(t, sizeof(t), 1395864371LL) > 0);
    CHECK(strcmp(t, "1,2 GB") == 0);
    /* Il confine fra KB e MB: 1048575 byte sono un byte sotto 1 MiB e devono
     * restare KB, o il salto di unita' avviene un byte troppo presto. */
    CHECK(rilascio_taglia(t, sizeof(t), 1048575) > 0);
    CHECK(strcmp(t, "1023,9 KB") == 0);
    /* Il ramo TB, l'ultima unita' dell'array: senza questo caso un errore
     * nell'indice ci arriverebbe senza che nessuna prova lo veda. */
    CHECK(rilascio_taglia(t, sizeof(t), 2199023255552LL) > 0);
    CHECK(strcmp(t, "2,0 TB") == 0);
    /* Una dimensione negativa non esiste: si scrive zero, non un numero
     * assurdo. */
    CHECK(rilascio_taglia(t, sizeof(t), -1) > 0);
    CHECK(strcmp(t, "0 byte") == 0);
}

static void il_messaggio_dei_soli_apk(void)
{
    Piano p;
    char m[512];

    memset(&p, 0, sizeof(p));
    p.voci_apk = 1;
    CHECK(rilascio_messaggio(m, sizeof(m), &p) > 0);
    CHECK(strcmp(m, "Installare 1 applicazione in Android?") == 0);
    /* Nessun avviso sulla sostituzione: non si sta copiando niente, e
     * adb install -r conserva i dati. Dirlo sarebbe falso. */
    CHECK(strstr(m, "sostituiti") == NULL);

    p.voci_apk = 3;
    CHECK(rilascio_messaggio(m, sizeof(m), &p) > 0);
    CHECK(strcmp(m, "Installare 3 applicazioni in Android?") == 0);
}

static void il_messaggio_dei_soli_file(void)
{
    Piano p;
    char m[512];

    memset(&p, 0, sizeof(p));
    p.voci_file = 1;
    p.file_totali = 1;
    p.byte = 1048576;
    CHECK(rilascio_messaggio(m, sizeof(m), &p) > 0);
    CHECK(strstr(m, "Copiare 1 file (1,0 MB) in /sdcard/Download?") == m);
    /* L'avviso c'e' SEMPRE quando si copia: nessuna sostituzione deve avvenire
     * senza che l'utente l'abbia letto. */
    CHECK(strstr(m, RIL_AVVISO) != NULL);

    p.file_totali = 214;
    p.byte = 1395864372LL;
    CHECK(rilascio_messaggio(m, sizeof(m), &p) > 0);
    CHECK(strstr(m, "Copiare 214 file (1,3 GB)") == m);
    CHECK(strstr(m, RIL_AVVISO) != NULL);
}

static void il_messaggio_del_rilascio_misto(void)
{
    Piano p;
    char m[512];

    memset(&p, 0, sizeof(p));
    p.voci_apk = 1;
    p.voci_file = 2;
    p.file_totali = 214;
    p.byte = 1395864372LL;
    CHECK(rilascio_messaggio(m, sizeof(m), &p) > 0);
    /* Le DUE azioni dichiarate in UN solo dialogo: e' la decisione dell'utente
     *, e serve a non avere il caso "ne ha fatta una parte e
     * sull'altra ha taciuto". */
    CHECK(strstr(m, "Installare 1 applicazione") == m);
    CHECK(strstr(m, "copiare 214 file (1,3 GB)") != NULL);
    CHECK(strstr(m, RIL_AVVISO) != NULL);

    p.voci_apk = 2;
    CHECK(rilascio_messaggio(m, sizeof(m), &p) > 0);
    CHECK(strstr(m, "Installare 2 applicazioni") == m);
}

static void un_piano_vuoto_non_produce_un_dialogo(void)
{
    Piano p;
    char m[512];

    memset(&p, 0, sizeof(p));
    /* Senza questo ramo il messaggio direbbe "Installare 0 applicazioni e
     * copiare 0 file", cioe' un dialogo che chiede il permesso di non fare
     * niente. */
    CHECK(rilascio_messaggio(m, sizeof(m), &p) == 0);
    CHECK(m[0] == '\0');
    CHECK(rilascio_messaggio(NULL, 10, &p) == 0);
    CHECK(rilascio_messaggio(m, sizeof(m), NULL) == 0);
}

static void un_messaggio_troncato_non_si_costruisce(void)
{
    Piano p;
    char m[8];

    memset(&p, 0, sizeof(p));
    p.voci_apk = 1;
    CHECK(rilascio_messaggio(m, sizeof(m), &p) == 0);
    CHECK(m[0] == '\0');
}

/* L'albero se lo costruisce la prova: un albero finto sotto il temporaneo di
 * Windows e' l'unico modo di provare una percorrenza senza dipendere da cosa c'e'
 * sul disco di chi esegue. */
static void scrivi_file(const wchar_t *percorso, int byte)
{
    HANDLE h = CreateFileW(percorso, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    char zero[64];
    DWORD scritti;
    int resta = byte;

    memset(zero, 0, sizeof(zero));
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    while (resta > 0) {
        DWORD quanti = (DWORD)(resta > 64 ? 64 : resta);

        if (!WriteFile(h, zero, quanti, &scritti, NULL) || scritti == 0) {
            break;
        }
        resta -= (int)scritti;
    }
    CloseHandle(h);
}

static void costruisci_albero(const wchar_t *radice, int quanti)
{
    wchar_t p[MAX_PATH];
    int i;

    CreateDirectoryW(radice, NULL);
    _snwprintf(p, MAX_PATH, L"%s\\dentro", radice);
    CreateDirectoryW(p, NULL);
    for (i = 0; i < quanti; i++) {
        /* Meta' nella radice e meta' nella sottocartella, cosi' la prova copre
         * anche la ricorsione e non solo il conteggio. */
        if (i % 2 == 0) {
            _snwprintf(p, MAX_PATH, L"%s\\f%d.bin", radice, i);
        } else {
            _snwprintf(p, MAX_PATH, L"%s\\dentro\\f%d.bin", radice, i);
        }
        scrivi_file(p, 100);
    }
}

static void togli_albero(const wchar_t *radice)
{
    wchar_t cmd[MAX_PATH + 64];
    int n;

    /* Guardia sul percorso vuoto: senza, un "rd /s /q" con la radice vuota
     * colpirebbe la radice del disco corrente. Non e' teorico -- ci arriva
     * percorso_temporaneo se TEMP esiste ma e' VUOTA, caso in cui _wgetenv NON
     * ritorna NULL e il ripiego non scatta. */
    if (!radice || !radice[0] || wcslen(radice) < 8) {
        printf("test-rilascio: RIFIUTO di cancellare '%ls': percorso troppo "
               "corto\n", radice ? radice : L"(nullo)");
        return;
    }
    n = _snwprintf(cmd, MAX_PATH + 64, L"cmd /c rd /s /q \"%ls\" 2>nul", radice);
    if (n < 0 || n >= MAX_PATH + 64) {
        return;
    }
    _wsystem(cmd);
}

static void percorso_temporaneo(wchar_t *dove, int max, const wchar_t *nome)
{
    const wchar_t *tmp = _wgetenv(L"TEMP");
    int n;

    /* Anche la stringa VUOTA va scartata, non solo NULL: con TEMP="" il percorso
     * diventerebbe "\nome" e togli_albero cancellerebbe dalla radice del disco. */
    if (!tmp || !tmp[0]) {
        tmp = L"C:\\Windows\\Temp";
    }
    n = _snwprintf(dove, max, L"%ls\\%ls", tmp, nome);
    if (n < 0 || n >= max) {
        dove[0] = L'\0';
    }
}

/* Un albero piu' profondo del limite della ricorsione, costruito annidando
 * cartelle: serve a provare l'esito INCERTO, che e' quello che il rilascio deve
 * rifiutare. */
static void costruisci_albero_profondo(const wchar_t *radice, int quanto)
{
    wchar_t p[MAX_PATH];
    int i;

    _snwprintf(p, MAX_PATH, L"%ls", radice);
    CreateDirectoryW(p, NULL);
    for (i = 0; i < quanto; i++) {
        wchar_t sotto[MAX_PATH];

        if (_snwprintf(sotto, MAX_PATH, L"%ls\\a", p) >= MAX_PATH) {
            return;
        }
        if (!CreateDirectoryW(sotto, NULL)) {
            return;
        }
        _snwprintf(p, MAX_PATH, L"%ls", sotto);
    }
    /* Un file in fondo: e' quello che adb copierebbe e noi non conteremmo. */
    if (_snwprintf(p, MAX_PATH, L"%ls\\infondo.bin", p) < MAX_PATH) {
        scrivi_file(p, 10);
    }
}

static void percorre_un_albero_contando_file_e_byte(void)
{
    wchar_t radice[MAX_PATH];
    int file = 0;
    long long byte = 0;

    percorso_temporaneo(radice, MAX_PATH, L"pf-albero");
    togli_albero(radice);
    costruisci_albero(radice, 10);

    CHECK(rilascio_percorri(radice, RIL_MAX_ALBERO, &file, &byte)
          == RIL_ALBERO_COMPLETO);
    CHECK(file == 10);
    /* 100 byte per file, e la sottocartella NON conta come file. */
    CHECK(byte == 1000);

    togli_albero(radice);
}

static void si_ferma_quando_supera_il_tetto(void)
{
    wchar_t radice[MAX_PATH];
    int file = 0;
    long long byte = 0;

    percorso_temporaneo(radice, MAX_PATH, L"pf-albero-tetto");
    togli_albero(radice);
    costruisci_albero(radice, 10);

    /* Con il tetto a 4 la percorrenza deve fermarsi e DIRLO col suo esito: oltre
     * il tetto il rilascio viene rifiutato comunque, e continuare a contare
     * costerebbe tempo dentro il wndproc. */
    CHECK(rilascio_percorri(radice, 4, &file, &byte) == RIL_ALBERO_TETTO);
    CHECK(file <= 5);

    togli_albero(radice);
}

static void una_cartella_che_non_si_legge_rende_il_conteggio_incerto(void)
{
    int file = 7;
    long long byte = 7;

    /* NON "zero file, tutto bene": e' il rilievo che la revisione ha trovato. Se
     * non si e' potuto contare, adb potrebbe copiare piu' di quanto dichiarato, e
     * su un conteggio piccolo si calcolerebbero una scadenza troppo corta e un
     * dialogo che promette meno del vero. Chi chiama deve rifiutare. */
    CHECK(rilascio_percorri(L"Z:\\non\\esiste", RIL_MAX_ALBERO, &file, &byte)
          == RIL_ALBERO_INCERTO);
    CHECK(file == 0);
    CHECK(byte == 0);
    CHECK(rilascio_percorri(NULL, RIL_MAX_ALBERO, &file, &byte)
          == RIL_ALBERO_INCERTO);
    CHECK(rilascio_percorri(L"Z:\\x", RIL_MAX_ALBERO, NULL, &byte)
          == RIL_ALBERO_INCERTO);
}

static void un_albero_troppo_profondo_e_incerto(void)
{
    wchar_t radice[MAX_PATH];
    int file = 0;
    long long byte = 0;

    percorso_temporaneo(radice, MAX_PATH, L"pf-profondo");
    togli_albero(radice);
    /* Oltre RIL_PROFONDITA (32): il file in fondo esiste e adb lo copierebbe,
     * quindi fermarsi dicendo "contati tutti" sarebbe una bugia. */
    costruisci_albero_profondo(radice, 40);

    CHECK(rilascio_percorri(radice, RIL_MAX_ALBERO, &file, &byte)
          == RIL_ALBERO_INCERTO);

    togli_albero(radice);
}

static void quanto_costa_percorrere_il_tetto(void)
{
    wchar_t radice[MAX_PATH];
    LARGE_INTEGER frequenza, prima, dopo;
    int file = 0;
    long long byte = 0;

    percorso_temporaneo(radice, MAX_PATH, L"pf-albero-2000");
    togli_albero(radice);
    costruisci_albero(radice, RIL_MAX_ALBERO);

    QueryPerformanceFrequency(&frequenza);
    QueryPerformanceCounter(&prima);
    CHECK(rilascio_percorri(radice, RIL_MAX_ALBERO, &file, &byte)
          == RIL_ALBERO_COMPLETO);
    QueryPerformanceCounter(&dopo);

    CHECK(file == RIL_MAX_ALBERO);
    /* Si MISURA e si STAMPA, non si asserisce una soglia: il tempo dipende dalla
     * macchina e dalla cache del filesystem, e una prova che asserisce un tempo
     * lampeggia. Il numero serve a decidere se RIL_MAX_ALBERO regge. */
    printf("test-rilascio: percorsi %d file in %.1f ms\n", file,
           (double)(dopo.QuadPart - prima.QuadPart) * 1000.0 /
           (double)frequenza.QuadPart);

    togli_albero(radice);
}

/* IL GUARDIANO, e questa prova esiste perche' il gesto NON si e' potuto fare.
 *
 * Il rifiuto durante l'avvio si sarebbe dovuto provare trascinando un file nei
 * trentacinque secondi fra l'avvio e "Android e' pronto": provato,
 * il rilascio e' arrivato tre minuti dopo e la finestra era gia' passata. Invece
 * di rincorrere il cronometro si prova la funzione, e si puo' fare per una
 * ragione di DESIGN: lo stato ARRIVA come parametro, quindi qui si passano tutti
 * gli stati possibili senza avviare nessuna VM. Se lo stato fosse stato letto
 * con vm_stato() dentro il modulo, questa prova non esisterebbe.
 *
 * Si provano solo gli stati NON pronti: con VM_PRONTO la funzione prenderebbe il
 * lucchetto e lancerebbe un thread che parla con adb, che in una prova senza VM
 * non si vuole.
 *
 * LIMITE DI QUESTA PROVA, misurato rompendo il guardiano di proposito: se il
 * guardiano cade, fallisce UN CHECK su otto e non otto. La prima chiamata passa e
 * prende il lucchetto, e le altre sette vengono rifiutate DA QUELLO -- un rifiuto
 * giusto per la ragione sbagliata. Un solo FALLITO qui va quindi letto come "il
 * guardiano non c'e' piu'", non come "un caso su otto e' rotto". */
static void il_guardiano_rifiuta_se_android_non_e_pronto(void)
{
    Config c;
    Piano p;
    const VmStato non_pronti[] = {
        VM_PREPARA, VM_AVVIA, VM_ATTESA_KERNEL, VM_ATTESA_ANDROID,
        VM_SPEGNIMENTO, VM_USCITO, VM_MORTA, VM_FALLITA
    };
    int i;

    config_default(&c);
    memset(&p, 0, sizeof(p));
    p.voci_file = 1;
    p.file_totali = 1;
    p.byte = 44;
    p.file_voce[0] = 1;
    snprintf(p.file[0], MAX_PATH, "%s", "C:\\prove\\appunti.txt");

    for (i = 0; i < (int)(sizeof(non_pronti) / sizeof(non_pronti[0])); i++) {
        CHECK(!rilascio_avvia(&c, &p, non_pronti[i]));
    }
}

static void gli_argomenti_assurdi_non_avviano_niente(void)
{
    Config c;
    Piano p;

    config_default(&c);
    memset(&p, 0, sizeof(p));

    /* Un piano vuoto non avvia un thread nemmeno a guest pronto: non c'e'
     * niente da fare, e prendere il lucchetto per niente lo terrebbe occupato. */
    CHECK(!rilascio_avvia(&c, &p, VM_PRONTO));
    CHECK(!rilascio_avvia(NULL, &p, VM_PRONTO));
    CHECK(!rilascio_avvia(&c, NULL, VM_PRONTO));
}

int main(void)
{
    il_tetto_e_provato_sui_confini();
    scrive_la_taglia_in_italiano();
    il_messaggio_dei_soli_apk();
    il_messaggio_dei_soli_file();
    il_messaggio_del_rilascio_misto();
    un_piano_vuoto_non_produce_un_dialogo();
    un_messaggio_troncato_non_si_costruisce();
    percorre_un_albero_contando_file_e_byte();
    si_ferma_quando_supera_il_tetto();
    una_cartella_che_non_si_legge_rende_il_conteggio_incerto();
    un_albero_troppo_profondo_e_incerto();
    quanto_costa_percorrere_il_tetto();
    il_guardiano_rifiuta_se_android_non_e_pronto();
    gli_argomenti_assurdi_non_avviano_niente();

    printf("test-rilascio: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
