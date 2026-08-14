/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-adb.c -- prove della costruzione della riga di comando di adb.
 *
 * Solo la parte PURA: eseguire adb davvero vorrebbe dire una VM accesa, e
 * quello lo verifica la prova sul prodotto vivo. Qui si protegge la cosa che
 * puo' rompersi in silenzio, cioe' una riga di comando sbagliata o troncata. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void la_riga_ha_le_virgolette_e_la_porta(void)
{
    char riga[256];

    CHECK(adb_riga(riga, sizeof(riga), "C:\\con spazi\\adb.exe", 15555,
                   "install -r \"C:\\app.apk\"") > 0);
    /* Le virgolette attorno al percorso di adb servono: sta in una cartella con
     * spazi (Desktop\AndroidRuntimeARM64\runtime\bin), e senza virgolette il
     * comando si spezzerebbe al primo spazio. */
    CHECK(strstr(riga, "\"C:\\con spazi\\adb.exe\"") == riga);
    CHECK(strstr(riga, "-s 127.0.0.1:15555") != NULL);
    CHECK(strstr(riga, "install -r \"C:\\app.apk\"") != NULL);
    /* Nessuno "shell" aggiunto: e' il senso della variante grezza. */
    CHECK(strstr(riga, "shell") == NULL);
}

static void una_riga_troncata_non_si_esegue(void)
{
    char corto[20];

    /* Una riga troncata non e' un comando mancante: e' un comando DIVERSO, e
     * eseguirlo sarebbe peggio che non eseguirlo. */
    CHECK(adb_riga(corto, sizeof(corto), "C:\\adb.exe", 15555,
                   "install -r \"C:\\un percorso molto lungo\\app.apk\"") == 0);
    CHECK(corto[0] == '\0');
}

static void gli_argomenti_nulli_non_fanno_danno(void)
{
    char riga[64];

    CHECK(adb_riga(NULL, 64, "adb", 1, "x") == 0);
    CHECK(adb_riga(riga, 0, "adb", 1, "x") == 0);
    CHECK(adb_riga(riga, sizeof(riga), NULL, 1, "x") == 0);
    CHECK(adb_riga(riga, sizeof(riga), "adb", 1, NULL) == 0);
}

/* Le due prove che seguono sono state SPOSTATE da test-apk.c senza cambiarne
 * un CHECK: leggono l'ultima riga dell'uscita di adb, che non e' un fatto degli
 * APK -- serve identica alla copia di un file. */
static void prende_l_ultima_riga_non_vuota(void)
{
    char riga[128];

    /* E' dove adb mette Success o Failure. Il resto e' rumore. */
    CHECK(adb_ultima_riga("Performing Streamed Install\nSuccess\n",
                          riga, sizeof(riga)) > 0);
    CHECK(strcmp(riga, "Success") == 0);

    /* Le righe vuote in coda non devono vincere sul messaggio vero. */
    CHECK(adb_ultima_riga("Failure [INSTALL_FAILED_ALREADY_EXISTS]\n\n\n",
                          riga, sizeof(riga)) > 0);
    CHECK(strcmp(riga, "Failure [INSTALL_FAILED_ALREADY_EXISTS]") == 0);

    /* adb su Windows chiude le righe con \r\n: il \r non deve finire nel
     * registro, dove stamperebbe un ritorno a capo in mezzo alla riga. */
    CHECK(adb_ultima_riga("Success\r\n", riga, sizeof(riga)) > 0);
    CHECK(strcmp(riga, "Success") == 0);

    /* Uscita senza ritorno a capo finale: succede quando il processo muore. */
    CHECK(adb_ultima_riga("Success", riga, sizeof(riga)) > 0);
    CHECK(strcmp(riga, "Success") == 0);
}

static void un_uscita_vuota_non_inventa_niente(void)
{
    char riga[128];

    CHECK(adb_ultima_riga("", riga, sizeof(riga)) == 0);
    CHECK(riga[0] == '\0');
    CHECK(adb_ultima_riga("\n\n", riga, sizeof(riga)) == 0);
    CHECK(adb_ultima_riga(NULL, riga, sizeof(riga)) == 0);
    CHECK(adb_ultima_riga("Success", NULL, 10) == 0);
}

static void la_scadenza_cresce_con_i_byte(void)
{
    /* Zero e zero: resta la base. L'avvio del processo, la stretta di mano col
     * server adb e la risposta costano comunque, e non dipendono dal lavoro. */
    CHECK(adb_scadenza_trasferimento(0, 0) == 20000);
    /* 10 MB in un file: un secondo per i byte, 40 ms per il file. */
    CHECK(adb_scadenza_trasferimento(10485760, 1) == 21040);
    /* 100 MB: dieci secondi in piu'. MISURATO : 200 MB reali
     * costano 3,5 s, quindi qui il margine e' cinque volte. */
    CHECK(adb_scadenza_trasferimento(104857600, 1) == 30040);
    /* 1 GiB: 102,4 s concessi piu' la base. */
    CHECK(adb_scadenza_trasferimento(1073741824LL, 1) == 122440);
    /* Dimensioni negative non esistono, e non devono diventare una scadenza
     * eterna: il ritorno e' unsigned, quindi un sottozero girerebbe. */
    CHECK(adb_scadenza_trasferimento(-1, -1) == 20000);
    /* Un conteggio assurdo viene TAGLIATO a un'ora. Il tetto vale per il caso in
     * cui la dimensione e' sbagliata, non per un file grosso: nel guest ci stanno
     * 4 GB, che al pavimento fanno sette minuti. */
    CHECK(adb_scadenza_trasferimento(1000000000000LL, 1) == 3600000);
}

static void la_scadenza_conta_anche_i_FILE(void)
{
    /* IL CASO CHE I SOLI BYTE SBAGLIAVANO, e lo ha trovato una misura: una
     * cartella di file piccoli costa ~6,9 ms per file e byte quasi zero. Con i
     * soli byte questa cartella avrebbe avuto 20 s contro i ~14 che le servono,
     * cioe' un margine che si rompe sulla prima macchina piu' lenta. */
    CHECK(adb_scadenza_trasferimento(50000, 500) == 20004 + 20000);
    /* Al tetto dell'albero: 2000 file danno 80 s oltre la base, contro i ~14 s
     * misurati per duemila. */
    CHECK(adb_scadenza_trasferimento(0, 2000) == 100000);
    /* Un solo file da zero byte non e' un non-lavoro: la base piu' il suo costo
     * per file. */
    CHECK(adb_scadenza_trasferimento(0, 1) == 20040);
}

/* --- la radice del prodotto: guscio_radice_calcola e guscio_radice_verifica
 *
 * Il Bloccante 3 della revisione finale, misurato al doppio clic: cwd =
 * runtime\bin\ (dove Esplora risorse la mette), zero processi, nessuna
 * finestra, nessun registro. Queste due funzioni sono la parte PURA del
 * rimedio -- niente GetModuleFileNameA, niente SetCurrentDirectoryA -- cosi'
 * si provano senza un eseguibile vero da spostare in giro. L'orchestrazione
 * impura (guscio_radice_imposta) resta senza prova qui, come
 * guscio_percorso_accanto qui sopra: la esercitano le tre prove manuali con
 * la VM richieste dal compito. */

static void la_radice_e_due_livelli_sopra_la_cartella_dell_eseguibile(void)
{
    char radice[MAX_PATH];

    /* Il caso vero: il pacchetto di rilascio spedisce
     * <radice>\runtime\bin\Habumi.exe. */
    CHECK(guscio_radice_calcola(
        "C:\\Users\\pippo\\Desktop\\AndroidRuntimeARM64\\runtime\\bin\\"
        "Habumi.exe", radice, sizeof(radice)));
    CHECK(strcmp(radice,
                 "C:\\Users\\pippo\\Desktop\\AndroidRuntimeARM64") == 0);

    /* Una radice a un solo livello resta un taglio pulito. */
    CHECK(guscio_radice_calcola("C:\\runtime\\bin\\Habumi.exe",
                                radice, sizeof(radice)));
    CHECK(strcmp(radice, "C:") == 0);
}

static void un_eseguibile_senza_tre_barre_non_ha_radice(void)
{
    char radice[MAX_PATH];

    /* Meno di tre barre: non c'e' una struttura <radice>\runtime\bin\ da cui
     * risalire. Capiterebbe solo con un percorso degenere da
     * GetModuleFileNameA -- e in quel caso non c'e' niente da calcolare, non
     * solo da verificare dopo. */
    CHECK(!guscio_radice_calcola("Habumi.exe", radice,
                                 sizeof(radice)));
    CHECK(!guscio_radice_calcola("bin\\Habumi.exe", radice,
                                 sizeof(radice)));
    CHECK(!guscio_radice_calcola("runtime\\bin\\Habumi.exe", radice,
                                 sizeof(radice)));
}

static void gli_argomenti_nulli_alla_radice_non_fanno_danno(void)
{
    char radice[MAX_PATH];

    CHECK(!guscio_radice_calcola(NULL, radice, sizeof(radice)));
    CHECK(!guscio_radice_calcola("C:\\a\\b\\c.exe", NULL, sizeof(radice)));
    CHECK(!guscio_radice_calcola("C:\\a\\b\\c.exe", radice, 0));
}

/* Cartella temporanea vera per le prove di guscio_radice_verifica: la
 * verifica tocca il disco (GetFileAttributesA), quindi qui non basta una
 * stringa finta. Copia di percorso_temporaneo/togli_albero di
 * test-rilascio.c, in ANSI invece che wide: due prove non giustificano
 * condividere l'helper fra file di prova diversi. */
static void percorso_temporaneo_prova(char *dove, int max, const char *nome)
{
    const char *tmp = getenv("TEMP");
    int n;

    if (!tmp || !tmp[0]) {
        tmp = "C:\\Windows\\Temp";
    }
    n = snprintf(dove, (size_t)max, "%s\\%s", tmp, nome);
    if (n < 0 || n >= max) {
        dove[0] = '\0';
    }
}

static void togli_albero_prova(const char *radice)
{
    char cmd[MAX_PATH + 64];
    int n;

    /* Guardia sul percorso corto: senza, un "rd /s /q" con la radice vuota
     * o quasi colpirebbe una cartella che non e' la nostra temporanea. */
    if (!radice || !radice[0] || strlen(radice) < 8) {
        return;
    }
    n = snprintf(cmd, sizeof(cmd), "cmd /c rd /s /q \"%s\" 2>nul", radice);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        return;
    }
    system(cmd);
}

static void la_verifica_trova_configurazione_sotto_runtime_bin(void)
{
    char radice[MAX_PATH];
    char cartella[MAX_PATH];
    char atteso[MAX_PATH];
    char provato[MAX_PATH];
    FILE *f;

    percorso_temporaneo_prova(radice, sizeof(radice), "pf-radice-ok");
    togli_albero_prova(radice);
    CreateDirectoryA(radice, NULL);
    snprintf(cartella, sizeof(cartella), "%s\\runtime", radice);
    CreateDirectoryA(cartella, NULL);
    snprintf(cartella, sizeof(cartella), "%s\\runtime\\bin", radice);
    CreateDirectoryA(cartella, NULL);
    snprintf(atteso, sizeof(atteso), "%s\\runtime\\bin\\config.txt",
             radice);
    f = fopen(atteso, "w");
    CHECK(f != NULL);
    if (f) {
        fclose(f);
    }

    CHECK(guscio_radice_verifica(radice, provato, sizeof(provato)));
    CHECK(strcmp(provato, atteso) == 0);

    togli_albero_prova(radice);
}

static void la_verifica_fallisce_e_nomina_cosa_cercava(void)
{
    char radice[MAX_PATH];
    char provato[MAX_PATH];

    /* Radice vera come cartella, ma senza runtime\bin\config.txt
     * dentro: e' esattamente il pacchetto rotto o la cartella sbagliata --
     * il caso che oggi uscirebbe muto. */
    percorso_temporaneo_prova(radice, sizeof(radice), "pf-radice-vuota");
    togli_albero_prova(radice);
    CreateDirectoryA(radice, NULL);

    CHECK(!guscio_radice_verifica(radice, provato, sizeof(provato)));
    /* Il messaggio deve nominare cosa ha cercato: e' il punto di tutto il
     * rimedio, misurato oggi come "il prodotto esce IN SILENZIO". */
    CHECK(strstr(provato, "runtime\\bin\\config.txt") != NULL);

    togli_albero_prova(radice);
}

int main(void)
{
    la_riga_ha_le_virgolette_e_la_porta();
    una_riga_troncata_non_si_esegue();
    gli_argomenti_nulli_non_fanno_danno();
    prende_l_ultima_riga_non_vuota();
    un_uscita_vuota_non_inventa_niente();
    la_scadenza_cresce_con_i_byte();
    la_scadenza_conta_anche_i_FILE();
    la_radice_e_due_livelli_sopra_la_cartella_dell_eseguibile();
    un_eseguibile_senza_tre_barre_non_ha_radice();
    gli_argomenti_nulli_alla_radice_non_fanno_danno();
    la_verifica_trova_configurazione_sotto_runtime_bin();
    la_verifica_fallisce_e_nomina_cosa_cercava();

    printf("test-adb: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
