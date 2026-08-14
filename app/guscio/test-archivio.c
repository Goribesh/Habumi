/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-archivio.c -- conservare le prove del giro precedente.
 *
 * PERCHE' QUESTO MODULO ESISTE: la seriale del guest viene cancellata a ogni
 * avvio (vm.c) e il registro riaperto con "w" (registro.c), per due ragioni
 * GIUSTE -- non leggere un boot_completed di due ore prima, e non prendere per
 * nuovo un messaggio del penultimo avvio. Ma insieme rendono impossibile
 * indagare un guasto che capita una volta ogni qualche ora: la
 * traccia della morte di system_server e' sopravvissuta solo perche' letta a
 * mano prima del riavvio successivo. Qui si sposta il file di prima invece di
 * distruggerlo, e il file ATTIVO resta pulito come prima.
 *
 * Queste prove toccano il filesystem VERO sotto il temporaneo di Windows: la
 * rotazione e' fatta di MoveFile e DeleteFile, e provarla con dei finti
 * proverebbe i finti. */
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

/* Un'ora fissa, non quella di adesso: il nome dell'archivio e' una decisione e
 * si prova con un valore noto, altrimenti la prova cambierebbe risposta a ogni
 * secondo che passa. */
static SYSTEMTIME ora_finta(WORD anno, WORD mese, WORD giorno, WORD h, WORD m,
                            WORD s)
{
    SYSTEMTIME t;

    memset(&t, 0, sizeof(t));
    t.wYear = anno;
    t.wMonth = mese;
    t.wDay = giorno;
    t.wHour = h;
    t.wMinute = m;
    t.wSecond = s;
    return t;
}

static void cartella_di_prova(char *dove, int max, const char *nome)
{
    const char *tmp = getenv("TEMP");

    if (!tmp || !tmp[0]) {
        tmp = "C:\\Windows\\Temp";
    }
    snprintf(dove, (size_t)max, "%s\\%s", tmp, nome);
}

static void scrivi(const char *percorso, const char *testo)
{
    FILE *f = fopen(percorso, "wb");

    if (f) {
        fputs(testo, f);
        fclose(f);
    }
}

static bool esiste(const char *percorso)
{
    return GetFileAttributesA(percorso) != INVALID_FILE_ATTRIBUTES;
}

static int quanti_file(const char *cartella)
{
    char modello[MAX_PATH];
    WIN32_FIND_DATAA t;
    HANDLE h;
    int n = 0;

    snprintf(modello, sizeof(modello), "%s\\*.log", cartella);
    h = FindFirstFileA(modello, &t);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        n++;
    } while (FindNextFileA(h, &t));
    FindClose(h);
    return n;
}

static void togli_cartella(const char *cartella)
{
    char cmd[MAX_PATH + 64];

    /* Guardia sul percorso corto: un "rd /s /q" con una radice vuota
     * colpirebbe la radice del disco. */
    if (!cartella || strlen(cartella) < 8) {
        printf("test-archivio: RIFIUTO di cancellare '%s'\n",
               cartella ? cartella : "(nullo)");
        return;
    }
    snprintf(cmd, sizeof(cmd), "cmd /c rd /s /q \"%s\" 2>nul", cartella);
    system(cmd);
}

static void il_nome_porta_la_data_ordinabile(void)
{
    char nome[MAX_PATH];
    SYSTEMTIME t = ora_finta(2026, 8, 4, 14, 30, 59);

    CHECK(archivio_nome(nome, sizeof(nome), "C:\\log\\archivio", "seriale", &t)
          > 0);
    /* ANNO-MESE-GIORNO e poi l'ora: cosi' l'ordine alfabetico E' l'ordine
     * cronologico, e potare i piu' vecchi diventa "cancella i primi". */
    CHECK(strcmp(nome, "C:\\log\\archivio\\seriale-20260804-143059.log") == 0);

    /* Le cifre si riempiono di zeri, o l'ordine alfabetico si rompe: "9" viene
     * dopo "10". */
    t = ora_finta(2026, 1, 2, 3, 4, 5);
    CHECK(archivio_nome(nome, sizeof(nome), "C:\\log", "r", &t) > 0);
    CHECK(strcmp(nome, "C:\\log\\r-20260102-030405.log") == 0);
}

static void un_nome_troncato_non_si_costruisce(void)
{
    char nome[16];
    SYSTEMTIME t = ora_finta(2026, 8, 4, 14, 30, 59);

    /* Un nome troncato non e' un nome mancante: e' un nome DIVERSO, e
     * archivierebbe altrove o sovrascriverebbe. */
    CHECK(archivio_nome(nome, sizeof(nome), "C:\\cartella\\lunga", "seriale", &t)
          == 0);
    CHECK(nome[0] == '\0');
    CHECK(archivio_nome(NULL, 10, "C:\\a", "b", &t) == 0);
    CHECK(archivio_nome(nome, sizeof(nome), NULL, "b", &t) == 0);
    CHECK(archivio_nome(nome, sizeof(nome), "C:\\a", NULL, &t) == 0);
    CHECK(archivio_nome(nome, sizeof(nome), "C:\\a", "b", NULL) == 0);
}

static void sposta_il_file_invece_di_cancellarlo(void)
{
    char base[MAX_PATH];
    char vivo[MAX_PATH];
    char arch[MAX_PATH];
    SYSTEMTIME t = ora_finta(2026, 8, 4, 14, 30, 59);
    char letto[64];
    FILE *f;

    cartella_di_prova(base, sizeof(base), "pf-arch-1");
    togli_cartella(base);
    CreateDirectoryA(base, NULL);
    snprintf(vivo, sizeof(vivo), "%s\\seriale.log", base);
    snprintf(arch, sizeof(arch), "%s\\vecchi", base);
    scrivi(vivo, "la prova che non va perduta");

    CHECK(archivio_ruota(vivo, arch, "seriale", 5, &t));
    /* Il file attivo NON c'e' piu': chi lo riapre lo trova pulito, che e' la
     * proprieta' per cui vm.c lo cancellava. */
    CHECK(!esiste(vivo));
    CHECK(quanti_file(arch) == 1);

    /* E il contenuto e' quello di prima: spostato, non ricreato vuoto. */
    f = fopen(arch, "rb");
    CHECK(f == NULL);   /* arch e' una cartella, non un file */
    snprintf(letto, sizeof(letto), "%s\\seriale-20260804-143059.log", arch);
    f = fopen(letto, "rb");
    CHECK(f != NULL);
    if (f) {
        char buf[64] = {0};

        CHECK(fread(buf, 1, sizeof(buf) - 1, f) > 0);
        CHECK(strcmp(buf, "la prova che non va perduta") == 0);
        fclose(f);
    }

    togli_cartella(base);
}

static void senza_file_da_archiviare_non_fa_niente(void)
{
    char base[MAX_PATH];
    char vivo[MAX_PATH];
    char arch[MAX_PATH];
    SYSTEMTIME t = ora_finta(2026, 8, 4, 14, 30, 59);

    cartella_di_prova(base, sizeof(base), "pf-arch-2");
    togli_cartella(base);
    CreateDirectoryA(base, NULL);
    snprintf(vivo, sizeof(vivo), "%s\\non-esiste.log", base);
    snprintf(arch, sizeof(arch), "%s\\vecchi", base);

    /* Primo avvio in assoluto: non c'e' niente da conservare, e non e' un
     * errore da segnalare. Ma nemmeno si crea una cartella vuota. */
    CHECK(!archivio_ruota(vivo, arch, "seriale", 5, &t));
    CHECK(!esiste(arch));

    togli_cartella(base);
}

static void pota_i_piu_vecchi_e_tiene_gli_ultimi(void)
{
    char base[MAX_PATH];
    char vivo[MAX_PATH];
    char arch[MAX_PATH];
    char atteso[MAX_PATH];
    int i;

    cartella_di_prova(base, sizeof(base), "pf-arch-3");
    togli_cartella(base);
    CreateDirectoryA(base, NULL);
    snprintf(vivo, sizeof(vivo), "%s\\seriale.log", base);
    snprintf(arch, sizeof(arch), "%s\\vecchi", base);

    /* Sei rotazioni con ore crescenti, tetto a 3. */
    for (i = 1; i <= 6; i++) {
        SYSTEMTIME t = ora_finta(2026, 8, 4, (WORD)(10 + i), 0, 0);

        scrivi(vivo, "giro");
        CHECK(archivio_ruota(vivo, arch, "seriale", 3, &t));
    }

    CHECK(quanti_file(arch) == 3);
    /* E si tengono i TRE PIU' RECENTI, non tre qualsiasi: senza questo controllo
     * una potatura che cancella dalla coda passerebbe con lo stesso conteggio. */
    snprintf(atteso, sizeof(atteso), "%s\\seriale-20260804-140000.log", arch);
    CHECK(esiste(atteso));
    snprintf(atteso, sizeof(atteso), "%s\\seriale-20260804-150000.log", arch);
    CHECK(esiste(atteso));
    snprintf(atteso, sizeof(atteso), "%s\\seriale-20260804-160000.log", arch);
    CHECK(esiste(atteso));
    snprintf(atteso, sizeof(atteso), "%s\\seriale-20260804-110000.log", arch);
    CHECK(!esiste(atteso));

    togli_cartella(base);
}

static void due_avvii_nello_stesso_secondo_non_si_sovrascrivono(void)
{
    char base[MAX_PATH];
    char vivo[MAX_PATH];
    char arch[MAX_PATH];
    SYSTEMTIME t = ora_finta(2026, 8, 4, 14, 30, 59);

    cartella_di_prova(base, sizeof(base), "pf-arch-4");
    togli_cartella(base);
    CreateDirectoryA(base, NULL);
    snprintf(vivo, sizeof(vivo), "%s\\seriale.log", base);
    snprintf(arch, sizeof(arch), "%s\\vecchi", base);

    /* Il nome ha la precisione del secondo, e due avvii dentro lo stesso secondo
     * sono possibili (il guscio riprova l'avvio fino a tre volte). Sovrascrivere
     * il primo archivio distruggerebbe proprio la prova che stiamo conservando:
     * il secondo prende un suffisso. */
    scrivi(vivo, "primo");
    CHECK(archivio_ruota(vivo, arch, "seriale", 5, &t));
    scrivi(vivo, "secondo");
    CHECK(archivio_ruota(vivo, arch, "seriale", 5, &t));

    CHECK(quanti_file(arch) == 2);

    togli_cartella(base);
}

static void un_tetto_a_zero_si_comporta_come_prima(void)
{
    char base[MAX_PATH];
    char vivo[MAX_PATH];
    char arch[MAX_PATH];
    SYSTEMTIME t = ora_finta(2026, 8, 4, 14, 30, 59);

    cartella_di_prova(base, sizeof(base), "pf-arch-5");
    togli_cartella(base);
    CreateDirectoryA(base, NULL);
    snprintf(vivo, sizeof(vivo), "%s\\seriale.log", base);
    snprintf(arch, sizeof(arch), "%s\\vecchi", base);
    scrivi(vivo, "niente da conservare");

    /* Tetto a zero = "non voglio archivi": il file attivo si cancella come
     * faceva prima questo modulo, e non si crea nessuna cartella. E' la via per
     * tornare al comportamento vecchio senza togliere il codice. */
    CHECK(archivio_ruota(vivo, arch, "seriale", 0, &t));
    CHECK(!esiste(vivo));
    CHECK(!esiste(arch));

    togli_cartella(base);
}

int main(void)
{
    il_nome_porta_la_data_ordinabile();
    un_nome_troncato_non_si_costruisce();
    sposta_il_file_invece_di_cancellarlo();
    senza_file_da_archiviare_non_fa_niente();
    pota_i_piu_vecchi_e_tiene_gli_ultimi();
    due_avvii_nello_stesso_secondo_non_si_sovrascrivono();
    un_tetto_a_zero_si_comporta_come_prima();

    printf("test-archivio: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
