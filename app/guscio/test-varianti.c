/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-varianti.c -- le prove del modulo puro delle varianti: il testo delle
 * due varianti, i loro percorsi fissi, lo stato letto e scritto da
 * runtime/variants.txt, e la decisione su cosa manca per avviare.
 *
 * Come test-appunti-trama.c: una macro CHECK con contatore invece di
 * assert(). assert si ferma al primo fallimento; qui conviene vedere tutti i
 * casi rotti in un colpo, perche' un lettore di file che si sbaglia di solito
 * si sbaglia su piu' di un caso alla volta.
 *
 * Nessun include del guscio e nessun include di Windows: questo file compila
 * e prova varianti.c da solo, senza VM e senza emulatore, come garantisce la
 * riga vuota in test-guscio.sh (vedi il commento in cima a varianti.h).
 *
 * I FILE TEMPORANEI si scrivono nella cartella corrente e si rimuovono, come
 * in test-config.c: qui non c'e' un Config da caricare, ma la stessa forma di
 * lettura riga per riga chiede lo stesso genere di file rotti apposta.
 *
 * LE DUE IMPRONTE usate nelle prove sono i fatti verificati di
 * .superpowers/sdd/sv-vincoli.md (sha256 di VANILLA e di GAPPS 20260403), non
 * valori inventati: cosi' una prova che le confonde con un typo si vede
 * subito confrontandola con quel file. */
#include <stdio.h>
#include <string.h>
#include "varianti.h"

static int passati, falliti;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (cond) {                                                        \
            passati++;                                                     \
        } else {                                                           \
            falliti++;                                                     \
            printf("FALLITO %s:%d: ", __FILE__, __LINE__);                 \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

/* sha256 di GAPPS 20260403 e di VANILLA 20260403, dai fatti verificati del
 * task: 64 cifre esadecimali ciascuno, contate a mano con wc -c prima di
 * scriverle qui, perche' un'impronta della lunghezza sbagliata nella prova
 * stessa la farebbe fallire per la ragione sbagliata. */
#define GAPPS_SHA    "7706b04e815802ac8918c71f1c364b647626f68ba281edf0164db4b5a4166d82"
#define VANILLA_SHA  "c3cf6ab5ded7496761bae4a49fe1a6f691923c5b84818755790f3ae4e239127b"

/* Lo sha256 della voce MAINLINE 20260403 del manifesto del vendor, dagli
 * stessi fatti verificati (.superpowers/sdd/rl-vincoli.md): non e' un valore
 * inventato, per lo stesso motivo delle due sopra. */
#define VENDOR_SHA   "bad5e84a2a47b877d8a85f6a212720f5562e3eeb26696bfd0e2de50dab1ec3ba"

static void scrivi(const char *nome, const char *contenuto)
{
    FILE *f = fopen(nome, "wb");
    if (f) {
        fputs(contenuto, f);
        fclose(f);
    }
}

static void prova_testo_e_da_testo(void)
{
    VarNome v;

    CHECK(strcmp(var_testo(VAR_VANILLA), "vanilla") == 0,
          "var_testo(VAR_VANILLA) e' \"%s\" invece di \"vanilla\"",
          var_testo(VAR_VANILLA));
    CHECK(strcmp(var_testo(VAR_GAPPS), "gapps") == 0,
          "var_testo(VAR_GAPPS) e' \"%s\" invece di \"gapps\"",
          var_testo(VAR_GAPPS));

    v = VAR_GAPPS;
    CHECK(var_da_testo("vanilla", &v) == true,
          "\"vanilla\" non e' stato riconosciuto");
    CHECK(v == VAR_VANILLA,
          "\"vanilla\" ha dato %d invece di VAR_VANILLA", (int)v);

    v = VAR_VANILLA;
    CHECK(var_da_testo("gapps", &v) == true,
          "\"gapps\" non e' stato riconosciuto");
    CHECK(v == VAR_GAPPS, "\"gapps\" ha dato %d invece di VAR_GAPPS", (int)v);

    /* Un testo sconosciuto ritorna false, e NON tocca *fuori: chi chiama deve
     * poter distinguere "non lo so" da "e' vanilla", per poter ripiegare su
     * vanilla lui stesso e scriverlo, come dice il commento in varianti.h. */
    v = VAR_GAPPS;
    CHECK(var_da_testo("qualcosa", &v) == false,
          "un testo sconosciuto e' stato accettato come variante");
    CHECK(v == VAR_GAPPS,
          "un testo sconosciuto ha comunque toccato *fuori: %d", (int)v);
}

static void prova_percorsi(void)
{
    const char *immagine = NULL, *dati = NULL;

    var_percorsi(VAR_VANILLA, &immagine, &dati);
    CHECK(strcmp(immagine, "guest/images/android/system.img") == 0,
          "immagine vanilla e' \"%s\"", immagine);
    CHECK(strcmp(dati, "guest/images/android/data.img") == 0,
          "dati vanilla e' \"%s\"", dati);

    var_percorsi(VAR_GAPPS, &immagine, &dati);
    CHECK(strcmp(immagine, "guest/images/android/system-gapps.img") == 0,
          "immagine gapps e' \"%s\"", immagine);
    CHECK(strcmp(dati, "guest/images/android/data-gapps.img") == 0,
          "dati gapps e' \"%s\"", dati);
}

/* Un file assente non e' un errore: e' lo stato di chi non ha ancora scelto
 * niente. Si sporca la VarStato apposta prima della chiamata: se
 * var_stato_leggi si limitasse a "non toccare niente" invece di azzerare, la
 * prova lo scoprirebbe subito. */
static void prova_leggi_file_assente(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;

    memset(&s, 0xAA, sizeof(s));
    ok = var_stato_leggi(&s, "questo-file-di-varianti-non-esiste.txt",
                         errore, sizeof(errore), &riga);
    CHECK(ok, "un file assente e' stato trattato come errore");
    CHECK(s.voci[VAR_VANILLA].file[0] == '\0',
          "la voce vanilla non e' vuota dopo un file assente");
    CHECK(s.voci[VAR_GAPPS].file[0] == '\0',
          "la voce gapps non e' vuota dopo un file assente");
}

static void prova_leggi_entrambe_le_voci(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;

    scrivi("prova-varianti-due.txt",
           "# un commento, e la riga vuota che segue si ignorano come\n"
           "# in ogni altro lettore del progetto\n"
           "\n"
           "gapps lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip "
           GAPPS_SHA " 1171824911\n"
           "vanilla lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip "
           VANILLA_SHA " 728517282\n");

    ok = var_stato_leggi(&s, "prova-varianti-due.txt", errore, sizeof(errore),
                         &riga);
    CHECK(ok, "un file con entrambe le voci e' stato rifiutato: %s", errore);

    CHECK(strcmp(s.voci[VAR_GAPPS].file,
                "lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip") == 0,
          "il nome del file gapps e' \"%s\"", s.voci[VAR_GAPPS].file);
    CHECK(strcmp(s.voci[VAR_GAPPS].sha256, GAPPS_SHA) == 0,
          "lo sha256 di gapps e' \"%s\"", s.voci[VAR_GAPPS].sha256);
    CHECK(s.voci[VAR_GAPPS].byte == 1171824911,
          "i byte di gapps sono %llu", (unsigned long long)s.voci[VAR_GAPPS].byte);

    CHECK(strcmp(s.voci[VAR_VANILLA].file,
                "lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip") == 0,
          "il nome del file vanilla e' \"%s\"", s.voci[VAR_VANILLA].file);
    CHECK(strcmp(s.voci[VAR_VANILLA].sha256, VANILLA_SHA) == 0,
          "lo sha256 di vanilla e' \"%s\"", s.voci[VAR_VANILLA].sha256);
    CHECK(s.voci[VAR_VANILLA].byte == 728517282,
          "i byte di vanilla sono %llu",
          (unsigned long long)s.voci[VAR_VANILLA].byte);

    remove("prova-varianti-due.txt");
}

static void prova_leggi_una_sola_voce(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;

    scrivi("prova-varianti-una.txt",
           "vanilla lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip "
           VANILLA_SHA " 728517282\n");

    ok = var_stato_leggi(&s, "prova-varianti-una.txt", errore, sizeof(errore),
                         &riga);
    CHECK(ok, "un file con una sola voce e' stato rifiutato: %s", errore);
    CHECK(s.voci[VAR_VANILLA].byte == 728517282,
          "i byte di vanilla sono %llu",
          (unsigned long long)s.voci[VAR_VANILLA].byte);
    CHECK(s.voci[VAR_GAPPS].file[0] == '\0',
          "la voce gapps, mai scritta nel file, non e' vuota");

    remove("prova-varianti-una.txt");
}

/* Una riga malformata fa fallire TUTTO il file, col numero di riga giusto, e
 * lo stato torna vuoto anche per la voce che alla riga precedente era stata
 * letta bene: uno stato letto a meta' farebbe credere di avere un'immagine
 * che non si ha (vedi il commento nel task). Qui la riga 1 e' buona e la
 * riga 2 ha il campo dei byte mancante del tutto. */
static void prova_leggi_riga_malformata(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;

    scrivi("prova-varianti-malformata.txt",
           "gapps lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip "
           GAPPS_SHA " 1171824911\n"
           "vanilla solo-due-campi.zip " VANILLA_SHA "\n");

    memset(&s, 0xAA, sizeof(s));
    ok = var_stato_leggi(&s, "prova-varianti-malformata.txt", errore,
                         sizeof(errore), &riga);
    CHECK(!ok, "una riga con un campo mancante e' stata accettata");
    CHECK(riga == 2, "la riga malformata e' stata segnata come %d invece di 2",
          riga);
    CHECK(errore[0] != '\0', "nessun messaggio d'errore e' stato scritto");
    CHECK(s.voci[VAR_GAPPS].file[0] == '\0',
          "la voce gapps della riga 1, buona, e' rimasta dopo il rifiuto del "
          "file: lo stato non e' tornato vuoto");
    CHECK(s.voci[VAR_VANILLA].file[0] == '\0',
          "la voce vanilla e' rimasta scritta a meta' dopo il rifiuto");

    remove("prova-varianti-malformata.txt");
}

static void prova_leggi_sha256_lunghezza_sbagliata(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;
    /* Un'impronta di 63 cifre invece di 64: tagliata dalla vera, cosi' il
     * solo difetto e' la lunghezza e non anche il contenuto. */
    char sha_corta[VAR_SHA256_CIFRE];
    char riga_prova[256];
    memcpy(sha_corta, VANILLA_SHA, sizeof(sha_corta) - 1);
    sha_corta[sizeof(sha_corta) - 1] = '\0';

    snprintf(riga_prova, sizeof(riga_prova), "vanilla file.zip %s 728517282\n",
            sha_corta);
    scrivi("prova-varianti-sha-corta.txt", riga_prova);

    memset(&s, 0xAA, sizeof(s));
    ok = var_stato_leggi(&s, "prova-varianti-sha-corta.txt", errore,
                         sizeof(errore), &riga);
    CHECK(!ok, "uno sha256 di 63 cifre e' stato accettato");
    CHECK(riga == 1, "la riga segnata e' %d invece di 1", riga);
    CHECK(s.voci[VAR_VANILLA].file[0] == '\0',
          "lo stato non e' tornato vuoto dopo il rifiuto");

    remove("prova-varianti-sha-corta.txt");
}

/* Il caso simmetrico a quello sopra: un'impronta di 65 cifre, una di troppo
 * rispetto alle 64 attese, deve essere rifiutata allo stesso modo di una
 * troppo corta -- il controllo strlen(sha_s) != VAR_SHA256_CIFRE non deve
 * badare solo al "troppo poco". */
static void prova_leggi_sha256_lunghezza_sbagliata_lunga(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;
    /* VANILLA_SHA (64 cifre) con una cifra in piu' incollata in coda: il
     * difetto resta solo la lunghezza, come per il caso corto sopra. */
    char sha_lunga[VAR_SHA256_CIFRE + 2];
    char riga_prova[256];

    memcpy(sha_lunga, VANILLA_SHA, VAR_SHA256_CIFRE);
    sha_lunga[VAR_SHA256_CIFRE] = '0';
    sha_lunga[VAR_SHA256_CIFRE + 1] = '\0';

    snprintf(riga_prova, sizeof(riga_prova), "vanilla file.zip %s 728517282\n",
            sha_lunga);
    scrivi("prova-varianti-sha-lunga.txt", riga_prova);

    memset(&s, 0xAA, sizeof(s));
    ok = var_stato_leggi(&s, "prova-varianti-sha-lunga.txt", errore,
                         sizeof(errore), &riga);
    CHECK(!ok, "uno sha256 di 65 cifre e' stato accettato");
    CHECK(riga == 1, "la riga segnata e' %d invece di 1", riga);
    CHECK(s.voci[VAR_VANILLA].file[0] == '\0',
          "lo stato non e' tornato vuoto dopo il rifiuto");

    remove("prova-varianti-sha-lunga.txt");
}

static void prova_leggi_byte_non_numerici(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;

    scrivi("prova-varianti-byte-lettere.txt",
           "gapps file.zip " GAPPS_SHA " dodicimila\n");

    memset(&s, 0xAA, sizeof(s));
    ok = var_stato_leggi(&s, "prova-varianti-byte-lettere.txt", errore,
                         sizeof(errore), &riga);
    CHECK(!ok, "un campo byte non numerico e' stato accettato");
    CHECK(riga == 1, "la riga segnata e' %d invece di 1", riga);

    remove("prova-varianti-byte-lettere.txt");
}

/* Un campo byte che eccede unsigned long long (24 cifre, tutte decimali)
 * satura dentro strtoull senza che *resto smetta di essere '\0': solo errno
 * segnala l'overflow con ERANGE. Se leggi_byte non controllasse errno,
 * questo valore verrebbe accettato come ULLONG_MAX invece di essere
 * rifiutato come "non numerico", che e' cio' che dice il commento sopra
 * leggi_byte in varianti.c. */
static void prova_leggi_byte_overflow(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;

    scrivi("prova-varianti-byte-overflow.txt",
           "gapps file.zip " GAPPS_SHA " 999999999999999999999999\n");

    memset(&s, 0xAA, sizeof(s));
    ok = var_stato_leggi(&s, "prova-varianti-byte-overflow.txt", errore,
                         sizeof(errore), &riga);
    CHECK(!ok,
          "un campo byte che eccede unsigned long long e' stato accettato");
    CHECK(riga == 1, "la riga segnata e' %d invece di 1", riga);
    CHECK(s.voci[VAR_GAPPS].file[0] == '\0',
          "lo stato non e' tornato vuoto dopo il rifiuto");

    remove("prova-varianti-byte-overflow.txt");
}

/* Un quinto campo dopo i quattro attesi (variante nomefile sha256 byte) deve
 * far fallire la riga: e' il ramo resto_vuoto, che altrimenti lascerebbe
 * ignorato in silenzio tutto cio' che segue il quarto campo. */
static void prova_leggi_campo_extra(void)
{
    VarStato s;
    char errore[128];
    int riga = -1;
    bool ok;

    scrivi("prova-varianti-campo-extra.txt",
           "vanilla file.zip " VANILLA_SHA " 728517282 campo-in-piu\n");

    memset(&s, 0xAA, sizeof(s));
    ok = var_stato_leggi(&s, "prova-varianti-campo-extra.txt", errore,
                         sizeof(errore), &riga);
    CHECK(!ok, "una riga con un quinto campo e' stata accettata");
    CHECK(riga == 1, "la riga segnata e' %d invece di 1", riga);
    CHECK(s.voci[VAR_VANILLA].file[0] == '\0',
          "lo stato non e' tornato vuoto dopo il rifiuto");

    remove("prova-varianti-campo-extra.txt");
}

/* Il giro completo: quello che si scrive e' quello che si rilegge, campo per
 * campo. Prova sia lo stato con entrambe le voci sia -- perche' e' un caso
 * diverso per var_stato_scrivi, che deve saltare le voci vuote invece di
 * scriverne una riga fatta di stringhe nulle -- lo stato con una sola voce. */
static void prova_scrivi_e_rileggi(void)
{
    VarStato scritta, letta;
    char errore[128];
    int riga = -1;
    bool ok;

    memset(&scritta, 0, sizeof(scritta));
    strcpy(scritta.voci[VAR_VANILLA].file,
          "lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip");
    strcpy(scritta.voci[VAR_VANILLA].sha256, VANILLA_SHA);
    scritta.voci[VAR_VANILLA].byte = 728517282;
    strcpy(scritta.voci[VAR_GAPPS].file,
          "lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip");
    strcpy(scritta.voci[VAR_GAPPS].sha256, GAPPS_SHA);
    scritta.voci[VAR_GAPPS].byte = 1171824911;

    ok = var_stato_scrivi(&scritta, "prova-varianti-scritta.txt", errore,
                         sizeof(errore));
    CHECK(ok, "la scrittura con entrambe le voci e' fallita: %s", errore);

    memset(&letta, 0xAA, sizeof(letta));
    ok = var_stato_leggi(&letta, "prova-varianti-scritta.txt", errore,
                        sizeof(errore), &riga);
    CHECK(ok, "la rilettura e' fallita: %s", errore);
    CHECK(memcmp(&scritta, &letta, sizeof(scritta)) == 0,
          "lo stato riletto non e' identico a quello scritto");

    remove("prova-varianti-scritta.txt");

    /* Una sola voce: l'altra deve restare vuota sia nello stato scritto sia,
     * dopo il giro, in quello riletto -- non una riga con campi vuoti. */
    memset(&scritta, 0, sizeof(scritta));
    strcpy(scritta.voci[VAR_GAPPS].file, "solo-gapps.zip");
    strcpy(scritta.voci[VAR_GAPPS].sha256, GAPPS_SHA);
    scritta.voci[VAR_GAPPS].byte = 42;

    ok = var_stato_scrivi(&scritta, "prova-varianti-scritta-una.txt", errore,
                         sizeof(errore));
    CHECK(ok, "la scrittura con una sola voce e' fallita: %s", errore);

    memset(&letta, 0xAA, sizeof(letta));
    ok = var_stato_leggi(&letta, "prova-varianti-scritta-una.txt", errore,
                        sizeof(errore), &riga);
    CHECK(ok, "la rilettura della sola voce e' fallita: %s", errore);
    CHECK(memcmp(&scritta, &letta, sizeof(scritta)) == 0,
          "lo stato con una sola voce non e' tornato identico dopo il giro");

    remove("prova-varianti-scritta-una.txt");
}

/* La riga "vendor": stessa parola chiave delle due varianti, ma un campo suo
 * in VarStato (vedi il commento su VarStato.vendor in varianti.h), non una
 * VarNome in piu'. Due casi: un file senza quella riga -- che non e' un
 * errore, e' lo stato di chi non ha ancora scaricato il vendor -- e il giro
 * completo scrittura/rilettura, che deve tornare identico come per le due
 * varianti. */
static void prova_vendor_riga(void)
{
    VarStato scritta, letta;
    char errore[128];
    int riga = -1;
    bool ok;

    scrivi("prova-vendor-assente.txt",
           "vanilla lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip "
           VANILLA_SHA " 728517282\n");
    memset(&letta, 0xAA, sizeof(letta));
    ok = var_stato_leggi(&letta, "prova-vendor-assente.txt", errore,
                        sizeof(errore), &riga);
    CHECK(ok, "un file senza la riga vendor e' stato rifiutato: %s", errore);
    CHECK(letta.vendor.file[0] == '\0',
          "il vendor, mai scritto nel file, non e' vuoto");
    remove("prova-vendor-assente.txt");

    memset(&scritta, 0, sizeof(scritta));
    strcpy(scritta.voci[VAR_VANILLA].file,
          "lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip");
    strcpy(scritta.voci[VAR_VANILLA].sha256, VANILLA_SHA);
    scritta.voci[VAR_VANILLA].byte = 728517282;
    strcpy(scritta.vendor.file,
          "lineage-20.0-20260403-MAINLINE-waydroid_arm64_only-vendor.zip");
    strcpy(scritta.vendor.sha256, VENDOR_SHA);
    scritta.vendor.byte = 77349347;

    ok = var_stato_scrivi(&scritta, "prova-vendor-scritta.txt", errore,
                         sizeof(errore));
    CHECK(ok, "la scrittura con la riga vendor e' fallita: %s", errore);

    memset(&letta, 0xAA, sizeof(letta));
    ok = var_stato_leggi(&letta, "prova-vendor-scritta.txt", errore,
                        sizeof(errore), &riga);
    CHECK(ok, "la rilettura della riga vendor e' fallita: %s", errore);
    CHECK(strcmp(letta.vendor.file, scritta.vendor.file) == 0,
          "il nome del file vendor riletto e' \"%s\"", letta.vendor.file);
    CHECK(strcmp(letta.vendor.sha256, scritta.vendor.sha256) == 0,
          "lo sha256 del vendor riletto e' \"%s\"", letta.vendor.sha256);
    CHECK(letta.vendor.byte == 77349347,
          "i byte del vendor riletti sono %llu",
          (unsigned long long)letta.vendor.byte);
    CHECK(memcmp(&scritta, &letta, sizeof(scritta)) == 0,
          "lo stato riletto (vendor compreso) non e' identico a quello "
          "scritto");

    remove("prova-vendor-scritta.txt");
}

/* Il controllo sulla build (VENDOR_BUILD_ATTESA, in varianti.h): con la build
 * attesa nessun avviso, con una build diversa un avviso che la nomina -- vedi
 * il commento su VENDOR_BUILD_ATTESA per il perche' esiste (l'HAL audio
 * nell'initramfs e' legato a quella build). */
static void prova_vendor_build_avviso(void)
{
    char avviso[256];
    bool serve;

    avviso[0] = '\0';
    serve = var_vendor_build_avviso(VENDOR_BUILD_ATTESA, avviso,
                                    sizeof(avviso));
    CHECK(!serve, "la build attesa ha comunque prodotto un avviso: %s",
          avviso);

    avviso[0] = '\0';
    serve = var_vendor_build_avviso(VENDOR_BUILD_ATTESA + 1, avviso,
                                    sizeof(avviso));
    CHECK(serve, "una build diversa da quella attesa non ha prodotto avviso");
    CHECK(avviso[0] != '\0',
          "l'avviso su build diversa e' stato dichiarato ma il testo e' "
          "vuoto");
}

/* Porta la stringa esadecimale minuscola s nella sua versione tutta
 * maiuscola, dentro dest (che deve avere almeno strlen(s) + 1 byte). Serve
 * alla prova sul confronto insensibile al caso: derivarla da VANILLA_SHA
 * invece di ritrascrivere a mano 64 cifre evita il rischio di un typo, che
 * farebbe fallire la prova per la ragione sbagliata (vedi il commento in
 * cima al file su GAPPS_SHA e VANILLA_SHA). */
static void in_maiuscolo(const char *s, char *dest)
{
    size_t i;

    for (i = 0; s[i] != '\0'; i++) {
        dest[i] = (s[i] >= 'a' && s[i] <= 'f') ? (char)(s[i] - 'a' + 'A')
                                                : s[i];
    }
    dest[i] = '\0';
}

/* I quattro esiti, compreso il caso della voce mai scelta. */
static void prova_cosa_manca(void)
{
    VarStato s;
    VarMancante m;
    char vanilla_sha_maiuscola[VAR_SHA256_CIFRE + 1];

    memset(&s, 0, sizeof(s));
    strcpy(s.voci[VAR_VANILLA].file, "qualsiasi-nome.zip");
    strcpy(s.voci[VAR_VANILLA].sha256, VANILLA_SHA);
    s.voci[VAR_VANILLA].byte = 728517282;

    m = var_cosa_manca(&s, VAR_VANILLA, false, false, NULL);
    CHECK(m == VAR_MANCA_IMMAGINE,
          "immagine assente: esito %d invece di VAR_MANCA_IMMAGINE", (int)m);

    m = var_cosa_manca(&s, VAR_VANILLA, true, false, VANILLA_SHA);
    CHECK(m == VAR_MANCA_DATI,
          "immagine giusta senza dati: esito %d invece di VAR_MANCA_DATI",
          (int)m);

    m = var_cosa_manca(&s, VAR_VANILLA, true, true, VANILLA_SHA);
    CHECK(m == VAR_PRONTA,
          "immagine giusta e dati presenti: esito %d invece di VAR_PRONTA",
          (int)m);

    /* Lo stesso confronto, ma con l'impronta scritta come la produce
     * certutil su Windows -- tutta in MAIUSCOLO -- mentre lo stato tiene
     * quella in minuscolo del manifesto di Waydroid: deve dare VAR_PRONTA
     * lo stesso, non VAR_IMPRONTA_SBAGLIATA. */
    in_maiuscolo(VANILLA_SHA, vanilla_sha_maiuscola);
    m = var_cosa_manca(&s, VAR_VANILLA, true, true, vanilla_sha_maiuscola);
    CHECK(m == VAR_PRONTA,
          "impronta scritta in maiuscolo (come da certutil): esito %d "
          "invece di VAR_PRONTA", (int)m);

    /* Lo sha del file sul disco e' quello di GAPPS: sicuramente diverso da
     * quello atteso per vanilla, qualunque sia il contenuto vero dei due
     * archivi. */
    m = var_cosa_manca(&s, VAR_VANILLA, true, true, GAPPS_SHA);
    CHECK(m == VAR_IMPRONTA_SBAGLIATA,
          "impronta sbagliata: esito %d invece di VAR_IMPRONTA_SBAGLIATA",
          (int)m);

    /* La voce mai scelta: nessuna riga in variants.txt per gapps, e infatti
     * sul disco non c'e' nessun file al suo posto. */
    memset(&s, 0, sizeof(s));
    m = var_cosa_manca(&s, VAR_GAPPS, false, false, NULL);
    CHECK(m == VAR_MANCA_IMMAGINE,
          "voce mai scelta: esito %d invece di VAR_MANCA_IMMAGINE", (int)m);

    /* Il caso delicato: la voce e' ancora mai scelta (nessuna impronta
     * attesa, voce->sha256 e' ""), ma sul disco un file c'e' davvero.
     * VAR_IMPRONTA_SBAGLIATA e' l'esito voluto e corretto -- un file
     * spuntato senza che nessuno abbia scelto quella variante non e' un
     * file di cui fidarsi -- ma finora nessun CHECK lo fissava: una
     * modifica futura poteva romperlo senza che niente se ne accorgesse. */
    m = var_cosa_manca(&s, VAR_GAPPS, true, true, GAPPS_SHA);
    CHECK(m == VAR_IMPRONTA_SBAGLIATA,
          "voce mai scelta con immagine presente sul disco: esito %d "
          "invece di VAR_IMPRONTA_SBAGLIATA", (int)m);
}

static void prova_spazio_necessario(void)
{
    uint64_t n;

    n = var_spazio_necessario(VAR_MANCA_IMMAGINE, 100, 200, 300);
    CHECK(n == 600, "MANCA_IMMAGINE: %llu invece di 600",
          (unsigned long long)n);

    n = var_spazio_necessario(VAR_MANCA_DATI, 100, 200, 300);
    CHECK(n == 300, "MANCA_DATI: %llu invece di 300", (unsigned long long)n);

    n = var_spazio_necessario(VAR_PRONTA, 100, 200, 300);
    CHECK(n == 0, "PRONTA: %llu invece di 0", (unsigned long long)n);

    /* Non elencato a parole nel task, ma e' la decisione presa scrivendo
     * varianti.c (vedi il commento su VAR_IMPRONTA_SBAGLIATA li'): un'impronta
     * sbagliata si ripara come un'immagine mancante, stesso conto. */
    n = var_spazio_necessario(VAR_IMPRONTA_SBAGLIATA, 100, 200, 300);
    CHECK(n == 600, "IMPRONTA_SBAGLIATA: %llu invece di 600",
          (unsigned long long)n);
}

/* Una voce sola e completa, coi valori reali del manifesto GAPPS del
 * 20260403 (i fatti verificati di .superpowers/sdd/sv-vincoli.md): i tre
 * campi che contano devono uscire esatti. */
static void prova_manifesto_voce_singola(void)
{
    static const char *json =
        "{\"response\":[{\"datetime\":1775188491,"
        "\"filename\":\"lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
        "\"url\":\"https://sourceforge.net/example\",\"version\":\"20.0\"}]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(ok, "una voce sola e completa e' stata rifiutata: %s", errore);
    CHECK(strcmp(voce.file,
                "lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip") == 0,
          "il filename estratto e' \"%s\"", voce.file);
    CHECK(strcmp(voce.sha256, GAPPS_SHA) == 0,
          "lo sha256 estratto e' \"%s\"", voce.sha256);
    CHECK(voce.byte == 1171824911,
          "i byte estratti sono %llu", (unsigned long long)voce.byte);
    CHECK(voce.datetime == 1775188491,
          "il datetime estratto e' %llu", (unsigned long long)voce.datetime);
    CHECK(strcmp(voce.url, "https://sourceforge.net/example") == 0,
          "l'url estratto e' \"%s\"", voce.url);
}

/* Tre voci con datetime in ordine SPARSO nel testo (1700000000, poi
 * 1900000000, poi 1800000000): la vincitrice deve essere quella col datetime
 * piu' alto -- la seconda del testo, ne' la prima ne' l'ultima -- o questa
 * prova non dimostrerebbe niente sul criterio di scelta. */
static void prova_manifesto_ordine_sparso(void)
{
    static const char *json =
        "{\"response\":["
        "{\"datetime\":1700000000,\"filename\":\"vecchia.zip\","
        "\"id\":\"" VANILLA_SHA "\",\"romtype\":\"VANILLA\",\"size\":111,"
        "\"url\":\"https://x\",\"version\":\"1\"},"
        "{\"datetime\":1900000000,"
        "\"filename\":\"lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
        "\"url\":\"https://y\",\"version\":\"20.0\"},"
        "{\"datetime\":1800000000,\"filename\":\"mediana.zip\","
        "\"id\":\"" VANILLA_SHA "\",\"romtype\":\"VANILLA\",\"size\":222,"
        "\"url\":\"https://z\",\"version\":\"2\"}"
        "]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(ok, "tre voci valide sono state rifiutate: %s", errore);
    CHECK(strcmp(voce.file,
                "lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip") == 0,
          "non e' stata scelta la voce col datetime piu' alto: file e' \"%s\"",
          voce.file);
    CHECK(voce.byte == 1171824911,
          "i byte scelti sono %llu", (unsigned long long)voce.byte);
    CHECK(voce.datetime == 1900000000,
          "il datetime scelto e' %llu invece del piu' alto",
          (unsigned long long)voce.datetime);
}

/* "response":[] e' un manifesto sintatticamente a posto ma senza nessuna
 * voce da scegliere: deve dare false, con un messaggio, non una VarVoce
 * vuota accettata come se fosse una scelta valida. */
static void prova_manifesto_risposta_vuota(void)
{
    static const char *json = "{\"response\":[]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    errore[0] = '\0';
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "un manifesto con \"response\":[] e' stato accettato");
    CHECK(errore[0] != '\0', "nessun messaggio d'errore su risposta vuota");
}

/* I tre campi che si cercano nella voce -- filename, id, size -- devono
 * essere tutti presenti: se manca uno solo la voce (e quindi il manifesto,
 * che qui ha una voce sola) va rifiutata, non letta a meta'. */
static void prova_manifesto_campo_mancante(void)
{
    static const char *manca_id =
        "{\"response\":[{\"datetime\":1775188491,"
        "\"filename\":\"file.zip\","
        "\"romtype\":\"GAPPS\",\"size\":1171824911,"
        "\"url\":\"https://x\",\"version\":\"20.0\"}]}";
    static const char *manca_size =
        "{\"response\":[{\"datetime\":1775188491,"
        "\"filename\":\"file.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\","
        "\"url\":\"https://x\",\"version\":\"20.0\"}]}";
    static const char *manca_filename =
        "{\"response\":[{\"datetime\":1775188491,"
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
        "\"url\":\"https://x\",\"version\":\"20.0\"}]}";
    /* Il quarto campo che questa correzione aggiunge alla lista di quelli
     * obbligatori: una voce senza "url" deve fallire come le altre tre,
     * non essere accettata con un url vuoto (vedi il task). */
    static const char *manca_url =
        "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
        "\"version\":\"20.0\"}]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(manca_id, strlen(manca_id), &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "una voce senza \"id\" e' stata accettata");

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(manca_size, strlen(manca_size), &voce,
                                   errore, sizeof(errore));
    CHECK(!ok, "una voce senza \"size\" e' stata accettata");

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(manca_filename, strlen(manca_filename),
                                   &voce, errore, sizeof(errore));
    CHECK(!ok, "una voce senza \"filename\" e' stata accettata");

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(manca_url, strlen(manca_url), &voce,
                                   errore, sizeof(errore));
    CHECK(!ok, "una voce senza \"url\" e' stata accettata");
}

/* Il caso simmetrico di prova_leggi_sha256_lunghezza_sbagliata* in
 * varianti.c, ma sul campo "id" del manifesto: 63 cifre e poi 65, entrambe
 * derivate da GAPPS_SHA per non rischiare un typo che farebbe fallire la
 * prova per la ragione sbagliata. */
static void prova_manifesto_id_lunghezza_sbagliata(void)
{
    char sha_corta[VAR_SHA256_CIFRE];
    char sha_lunga[VAR_SHA256_CIFRE + 2];
    char json[512];
    VarVoce voce;
    char errore[128];
    bool ok;

    memcpy(sha_corta, GAPPS_SHA, sizeof(sha_corta) - 1);
    sha_corta[sizeof(sha_corta) - 1] = '\0';
    snprintf(json, sizeof(json),
            "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
            "\"id\":\"%s\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
            "\"url\":\"https://x\",\"version\":\"20.0\"}]}",
            sha_corta);
    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "un id di 63 cifre e' stato accettato");

    memcpy(sha_lunga, GAPPS_SHA, VAR_SHA256_CIFRE);
    sha_lunga[VAR_SHA256_CIFRE] = '0';
    sha_lunga[VAR_SHA256_CIFRE + 1] = '\0';
    snprintf(json, sizeof(json),
            "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
            "\"id\":\"%s\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
            "\"url\":\"https://x\",\"version\":\"20.0\"}]}",
            sha_lunga);
    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "un id di 65 cifre e' stato accettato");
}

/* Un campo "size" che non e' cifre decimali -- qui una parola, come nella
 * prova simmetrica su var_stato_leggi -- deve dare false, non un byte a
 * zero o a caso. */
static void prova_manifesto_size_non_numerico(void)
{
    static const char *json =
        "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":dodicimila,"
        "\"url\":\"https://x\",\"version\":\"20.0\"}]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "un campo size non numerico e' stato accettato");
}

/* Le prove della revisione su manifesto_numero: fermarsi alla prima
 * non-cifra non basta, va controllato anche cosa segue.
 *
 * "size":1e10 e "size":123.5 fermano la scansione delle cifre decimali
 * rispettivamente dopo "1" e dopo "123" -- "e" e "." non sono cifre -- ma
 * quel che segue non e' ne' spazio, ne' virgola, ne' la graffa che chiude
 * l'oggetto: prima della correzione questo non veniva controllato, e
 * manifesto_numero restituiva "1" o "123" come se il campo finisse li',
 * fraintendendo un size sbagliato per difetto invece di rifiutarlo (vedi il
 * task: un size cosi' entra nel calcolo dello spazio libero necessario e fa
 * partire uno scaricamento che poi non ci sta).
 *
 * Il terzo caso, un size CON SEGNO, e' gia' rifiutato da prima di questa
 * correzione: il segno "-" non e' una cifra, la scansione da' zero cifre, e
 * il controllo "len == 0" esistente rifiuta senza nemmeno arrivare al
 * controllo nuovo. Resta qui per fissare anche questo esito, non solo
 * quello nuovo. */
static void prova_manifesto_size_numero_malformato(void)
{
    static const char *notazione_esponenziale =
        "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":1e10,"
        "\"url\":\"https://x\",\"version\":\"20.0\"}]}";
    static const char *virgola_decimale =
        "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":123.5,"
        "\"url\":\"https://x\",\"version\":\"20.0\"}]}";
    static const char *con_segno =
        "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":-5,"
        "\"url\":\"https://x\",\"version\":\"20.0\"}]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(notazione_esponenziale,
                                   strlen(notazione_esponenziale), &voce,
                                   errore, sizeof(errore));
    CHECK(!ok,
          "\"size\":1e10 e' stato letto come 1 invece di essere rifiutato");

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(virgola_decimale, strlen(virgola_decimale),
                                   &voce, errore, sizeof(errore));
    CHECK(!ok,
          "\"size\":123.5 e' stato letto come 123 invece di essere rifiutato");

    memset(&voce, 0xAA, sizeof(voce));
    ok = var_manifesto_piu_recente(con_segno, strlen(con_segno), &voce,
                                   errore, sizeof(errore));
    CHECK(!ok, "\"size\":-5 (con segno) e' stato accettato");
}

/* Il manifesto arriva dalla rete e puo' fermarsi a meta' di una voce: qui
 * "completo" e' un JSON valido, ma si passa a var_manifesto_piu_recente solo
 * il pezzo fino a subito prima di "romtype" (compresi "datetime", "filename"
 * e "id" per intero) -- prima che quella voce si chiuda con una graffa.
 *
 * SUBITO DOPO taglio, nella STESSA memoria, ci sono ancora i byte veri di
 * "romtype":"GAPPS","size":1171824911,...}]} -- se l'implementazione
 * leggesse anche un solo byte oltre n invece di fermarsi rigorosamente a
 * json + n, troverebbe quella graffa di chiusura e una voce che sembra
 * completa, e la prova non lo scoprirebbe. Passando n = taglio si dimostra
 * che non lo fa. */
static void prova_manifesto_troncato(void)
{
    static const char *completo =
        "{\"response\":[{\"datetime\":1775188491,"
        "\"filename\":\"lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
        "\"url\":\"https://sourceforge.net/example\",\"version\":\"20.0\"}]}";
    size_t taglio = (size_t)(strstr(completo, "\"romtype\"") - completo);
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    errore[0] = '\0';
    ok = var_manifesto_piu_recente(completo, taglio, &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "un manifesto troncato a meta' voce e' stato accettato");
    CHECK(errore[0] != '\0', "nessun messaggio d'errore su manifesto troncato");
}

/* Un filename piu' lungo di quanto VarVoce.file possa contenere deve fare
 * fallire la voce, NON essere tagliato a VAR_NOME_MAX - 1 caratteri: un nome
 * troncato produrrebbe un URL che non esiste (vedi il task). VAR_NOME_MAX
 * caratteri di 'a', senza terminatore, sono gia' uno di troppo per un campo
 * di VAR_NOME_MAX byte compreso il terminatore. */
static void prova_manifesto_filename_troppo_lungo(void)
{
    char filename_lungo[VAR_NOME_MAX + 1];
    char json[VAR_NOME_MAX + 256];
    VarVoce voce;
    char errore[128];
    bool ok;
    size_t i;

    for (i = 0; i < VAR_NOME_MAX; i++) {
        filename_lungo[i] = 'a';
    }
    filename_lungo[VAR_NOME_MAX] = '\0';

    snprintf(json, sizeof(json),
            "{\"response\":[{\"datetime\":1775188491,\"filename\":\"%s\","
            "\"id\":\"%s\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
            "\"url\":\"https://x\",\"version\":\"20.0\"}]}",
            filename_lungo, GAPPS_SHA);

    memset(&voce, 0xAA, sizeof(voce));
    errore[0] = '\0';
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "un filename di %d caratteri e' stato accettato o troncato",
          VAR_NOME_MAX);
    CHECK(errore[0] != '\0', "nessun messaggio d'errore su filename troppo lungo");
}

/* Lo stesso principio della prova sopra, ma sul campo "url": un indirizzo
 * piu' lungo di quanto VarVoce.url possa contenere deve fare fallire la
 * voce, NON essere tagliato a VAR_URL_MAX - 1 caratteri (vedi il task: "un
 * url piu' lungo del buffer fa fallire invece di troncare, perche' un
 * indirizzo troncato scarica un errore HTML e lo chiama immagine"). */
static void prova_manifesto_url_troppo_lungo(void)
{
    char url_lungo[VAR_URL_MAX + 1];
    char json[VAR_URL_MAX + 256];
    VarVoce voce;
    char errore[128];
    bool ok;
    size_t i;

    for (i = 0; i < VAR_URL_MAX; i++) {
        url_lungo[i] = 'a';
    }
    url_lungo[VAR_URL_MAX] = '\0';

    snprintf(json, sizeof(json),
            "{\"response\":[{\"datetime\":1775188491,\"filename\":\"file.zip\","
            "\"id\":\"%s\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
            "\"url\":\"%s\",\"version\":\"20.0\"}]}",
            GAPPS_SHA, url_lungo);

    memset(&voce, 0xAA, sizeof(voce));
    errore[0] = '\0';
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(!ok, "un url di %d caratteri e' stato accettato o troncato",
          VAR_URL_MAX);
    CHECK(errore[0] != '\0', "nessun messaggio d'errore su url troppo lungo");
}

/* IL DIFETTO PIU' IMPORTANTE DI QUESTA TORNATA: il manifesto vero arriva con
 * uno spazio dopo i due punti di ogni campo -- "datetime": 1775188491, non
 * "datetime":1775188491 -- ma tutte le prove sul manifesto scritte finora
 * (comprese quelle sopra, prova_manifesto_voce_singola inclusa) usano JSON
 * compatto scritto a mano, senza quello spazio. Una prova costruita sul
 * formato immaginato non prova il formato che arriva davvero: senza questa
 * prova, var_manifesto_piu_recente falliva SEMPRE sul manifesto reale con
 * "voce senza datetime, o datetime non numerico", e il selettore delle
 * varianti non poteva funzionare affatto.
 *
 * Il testo qui sotto e' copiato PAROLA PER PAROLA, spazi compresi, da un
 * estratto autentico del manifesto di Waydroid (task sv-5): due voci GAPPS,
 * la piu' recente elencata per prima nel testo -- cosi' la prova dimostra
 * sia la tolleranza allo spazio bianco sia, di striscio, che il criterio
 * "datetime piu' alto" non dipende dalla posizione nel testo. */
static void prova_manifesto_spaziatura_vera(void)
{
    static const char *json =
        "{\"response\": [{\"datetime\": 1775188491, "
        "\"filename\": \"lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip\", "
        "\"id\": \"" GAPPS_SHA "\", "
        "\"romtype\": \"GAPPS\", "
        "\"size\": 1171824911, "
        "\"url\": \"https://sourceforge.net/projects/waydroid/files/images/system/lineage/waydroid_arm64_only/lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip/download\", "
        "\"version\": \"20.0\"}, "
        "{\"datetime\": 1773317846, "
        "\"filename\": \"lineage-20.0-20260312-GAPPS-waydroid_arm64_only-system.zip\", "
        "\"id\": \"49237b8066ab31731b62bd32f3d2ee8d94365508f10214bb5594ce3d9cf37c3f\", "
        "\"romtype\": \"GAPPS\", "
        "\"size\": 1170961501, "
        "\"url\": \"https://sourceforge.net/x\", "
        "\"version\": \"20.0\"}]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    errore[0] = '\0';
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(ok, "il manifesto vero, con la sua spaziatura vera, e' stato "
          "rifiutato: %s", errore);
    CHECK(strcmp(voce.file,
                "lineage-20.0-20260403-GAPPS-waydroid_arm64_only-system.zip") == 0,
          "dal manifesto vero e' uscito il file \"%s\"", voce.file);
    CHECK(strcmp(voce.sha256, GAPPS_SHA) == 0,
          "dal manifesto vero e' uscita l'impronta \"%s\"", voce.sha256);
    CHECK(voce.byte == 1171824911,
          "dal manifesto vero sono usciti %llu byte",
          (unsigned long long)voce.byte);
    /* IL CAMPO CHE QUESTA CORREZIONE AGGIUNGE: l'url va confrontato
     * carattere per carattere con quello scritto nell'estratto autentico qui
     * sopra, non solo verificato "non vuoto" -- e' esattamente il controllo
     * che ha mancato finche' questo campo non si leggeva affatto (vedi il
     * task: "verificato carattere per carattere contro il manifesto vero"). */
    CHECK(strcmp(voce.url,
                "https://sourceforge.net/projects/waydroid/files/images/"
                "system/lineage/waydroid_arm64_only/lineage-20.0-20260403-"
                "GAPPS-waydroid_arm64_only-system.zip/download") == 0,
          "dal manifesto vero e' uscito l'url \"%s\"", voce.url);
}

/* Spaziatura ESAGERATA -- ritorni a capo e tabulazioni fra chiave e valore,
 * non solo lo spazio singolo del manifesto vero -- per dimostrare che il
 * salto dello spazio bianco copre ogni forma che il JSON ammette (spazio,
 * tab, CR, LF), non solo il caso piu' comune visto sopra. */
static void prova_manifesto_spaziatura_esagerata(void)
{
    static const char *json =
        "{\"response\":[{\"datetime\":\n\t1775188491,\"filename\":\t\n"
        "\"esagerato.zip\",\"id\":\r\n\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\","
        "\"size\":\t\t1171824911,\"url\":\"https://x\",\"version\":\"1\"}]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    errore[0] = '\0';
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(ok, "una spaziatura esagerata (a capo e tab) e' stata rifiutata: %s",
          errore);
    CHECK(strcmp(voce.file, "esagerato.zip") == 0,
          "il filename estratto con spaziatura esagerata e' \"%s\"",
          voce.file);
    CHECK(strcmp(voce.sha256, GAPPS_SHA) == 0,
          "l'impronta estratta con spaziatura esagerata e' \"%s\"",
          voce.sha256);
    CHECK(voce.byte == 1171824911,
          "i byte estratti con spaziatura esagerata sono %llu",
          (unsigned long long)voce.byte);
}

/* Il JSON compatto -- senza nessuno spazio dopo i due punti -- e' la forma
 * usata da tutte le prove scritte prima di questa correzione (per esempio
 * prova_manifesto_voce_singola): deve continuare a funzionare esattamente
 * come prima. Questa prova affianca le due forme -- compatta qui, spaziata
 * nelle due prove sopra -- cosi' che il lettore si dimostri capace di
 * entrambe, non solo di quella nuova che ha scoperto il difetto. */
static void prova_manifesto_json_compatto_continua_a_passare(void)
{
    static const char *json =
        "{\"response\":[{\"datetime\":1775188491,\"filename\":\"compatto.zip\","
        "\"id\":\"" GAPPS_SHA "\",\"romtype\":\"GAPPS\",\"size\":1171824911,"
        "\"url\":\"https://x\",\"version\":\"1\"}]}";
    VarVoce voce;
    char errore[128];
    bool ok;

    memset(&voce, 0xAA, sizeof(voce));
    errore[0] = '\0';
    ok = var_manifesto_piu_recente(json, strlen(json), &voce, errore,
                                   sizeof(errore));
    CHECK(ok, "il JSON compatto, senza spazi, e' stato rifiutato: %s", errore);
    CHECK(strcmp(voce.file, "compatto.zip") == 0,
          "il filename estratto dal JSON compatto e' \"%s\"", voce.file);
    CHECK(strcmp(voce.sha256, GAPPS_SHA) == 0,
          "l'impronta estratta dal JSON compatto e' \"%s\"", voce.sha256);
    CHECK(voce.byte == 1171824911,
          "i byte estratti dal JSON compatto sono %llu",
          (unsigned long long)voce.byte);
}

int main(void)
{
    prova_testo_e_da_testo();
    prova_percorsi();
    prova_leggi_file_assente();
    prova_leggi_entrambe_le_voci();
    prova_leggi_una_sola_voce();
    prova_leggi_riga_malformata();
    prova_leggi_sha256_lunghezza_sbagliata();
    prova_leggi_sha256_lunghezza_sbagliata_lunga();
    prova_leggi_byte_non_numerici();
    prova_leggi_byte_overflow();
    prova_leggi_campo_extra();
    prova_scrivi_e_rileggi();
    prova_vendor_riga();
    prova_vendor_build_avviso();
    prova_cosa_manca();
    prova_spazio_necessario();
    prova_manifesto_voce_singola();
    prova_manifesto_ordine_sparso();
    prova_manifesto_risposta_vuota();
    prova_manifesto_campo_mancante();
    prova_manifesto_id_lunghezza_sbagliata();
    prova_manifesto_size_non_numerico();
    prova_manifesto_size_numero_malformato();
    prova_manifesto_troncato();
    prova_manifesto_filename_troppo_lungo();
    prova_manifesto_url_troppo_lungo();
    prova_manifesto_spaziatura_vera();
    prova_manifesto_spaziatura_esagerata();
    prova_manifesto_json_compatto_continua_a_passare();

    printf("test-varianti: %d su %d passati\n", passati, passati + falliti);
    return falliti ? 1 : 0;
}
