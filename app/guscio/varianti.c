/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* varianti.c -- quale immagine di Android si avvia, e cosa manca per avviarla.
 *
 * Vedi varianti.h per il perche' di questo modulo e per il confine che
 * rispetta. Qui c'e' solo il come.
 *
 * SOLO LIBRERIA STANDARD, come appunti-trama.c: e' la ragione per cui questo
 * file si compila e si prova da solo in test-guscio.sh (riga delle dipendenze
 * vuota), senza VM e senza emulatore. Il primo #include di windows.h scritto
 * qui dentro spegnerebbe quella prova.
 *
 * NIENTE REGISTRO, per lo stesso motivo di appunti-trama.c: un errore si
 * comunica col valore di ritorno e con la stringa errore che il chiamante ha
 * fornito, non con una riga scritta da qualche parte -- chiamare il registro
 * da qui tirerebbe dentro un modulo del guscio e romperebbe la purezza che
 * questo file esiste apposta per avere. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "varianti.h"

/* La tabella dei due percorsi fissi, presa dal task cosi' com'e'.
 *
 * I NOMI SONO ASIMMETRICI DI PROPOSITO -- la vanilla non ha suffisso -- e
 * restano cosi' per non invalidare la documentazione gia' scritta sui
 * percorsi di oggi. La riga di registro all'avvio (task sv-3, che vive fuori
 * da questo modulo puro) dice quale variante e' attiva e da quali file: e'
 * la compensazione per l'asimmetria. */
static const struct {
    const char *immagine;
    const char *dati;
} tabella[VAR_QUANTE] = {
    { "guest/images/android/system.img",       "guest/images/android/data.img"       },
    { "guest/images/android/system-gapps.img", "guest/images/android/data-gapps.img" }
};

const char *var_testo(VarNome v)
{
    /* Qualunque valore che non sia VAR_GAPPS ripiega su "vanilla", incluso un
     * VarNome fuori tabella: e' lo stesso principio del resto del guscio (per
     * esempio dpi_scala con un DPI a zero) di non separare un caso quasi
     * impossibile in un ramo che nessuna prova puo' raggiungere per davvero. */
    return v == VAR_GAPPS ? "gapps" : "vanilla";
}

bool var_da_testo(const char *s, VarNome *fuori)
{
    if (s == NULL || fuori == NULL) {
        return false;
    }
    /* Un testo sconosciuto NON tocca *fuori, come ap_trama_estrai non tocca
     * *lung e *usati sui suoi esiti diversi da 1: chi chiama deve poter
     * distinguere "non lo so" da "e' vanilla" per decidere lui il ripiego,
     * che e' la spec (vedi il commento su var_da_testo in varianti.h) -- se
     * questa funzione scrivesse gia' VAR_VANILLA da sola, il chiamante non
     * saprebbe piu' se il file diceva davvero "vanilla" o se stava ripiegando. */
    if (strcmp(s, "vanilla") == 0) {
        *fuori = VAR_VANILLA;
        return true;
    }
    if (strcmp(s, "gapps") == 0) {
        *fuori = VAR_GAPPS;
        return true;
    }
    return false;
}

void var_percorsi(VarNome v, const char **immagine, const char **dati)
{
    int i = (v == VAR_GAPPS) ? VAR_GAPPS : VAR_VANILLA;

    if (immagine != NULL) {
        *immagine = tabella[i].immagine;
    }
    if (dati != NULL) {
        *dati = tabella[i].dati;
    }
}

/* Copia in dest la prossima parola di *p (delimitata da spazio o tab),
 * avanzando *p oltre di essa. Ritorna false se la parola non ci sta in max
 * caratteri (compreso il terminatore) -- caso che qui vuol dire "riga
 * malformata", non "campo troncato in silenzio" -- oppure se a quel punto
 * della riga non c'e' nessuna parola (fine riga o solo spazi). */
static bool prossima_parola(const char **p, char *dest, size_t max)
{
    size_t n = 0;
    const char *s = *p;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') {
        if (n + 1 >= max) {
            return false;
        }
        dest[n] = *s;
        n++;
        s++;
    }
    dest[n] = '\0';
    *p = s;
    return n > 0;
}

/* True se da qui alla fine della riga non resta altro che spazi: serve a
 * scoprire un quinto campo, che altrimenti verrebbe silenziosamente ignorato
 * invece di far fallire la riga. */
static bool resto_vuoto(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return *p == '\0' || *p == '\r' || *p == '\n';
}

/* Interpreta un campo byte: SOLO cifre decimali, non vuoto, senza segno --
 * "-1" non e' un uint64_t piu' piccolo, e' un errore di formato. Un byte
 * fuori dai limiti di unsigned long long (piu' di quanto qualunque disco di
 * questa macchina potra' mai contenere) e' trattato come non numerico:
 * strtoull satura e non lo direbbe da sola. */
static bool leggi_byte(const char *s, uint64_t *n)
{
    const char *p;
    char *resto;
    unsigned long long v;

    if (s == NULL || *s == '\0') {
        return false;
    }
    for (p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    /* errno va azzerato qui: strtoull non lo fa in caso di successo, e un
     * ERANGE lasciato da una chiamata precedente (a questa stessa funzione o
     * a qualunque altra) farebbe rifiutare un campo byte perfettamente
     * valido. Ed e' questo controllo, non *resto, a rendere vero il commento
     * sopra la funzione: su un campo tutto cifre che eccede unsigned long
     * long, strtoull satura a ULLONG_MAX ma lascia comunque *resto a '\0' --
     * la differenza si vede solo in errno. */
    errno = 0;
    v = strtoull(s, &resto, 10);
    if (*resto != '\0' || errno == ERANGE) {
        return false;
    }
    *n = (uint64_t)v;
    return true;
}

bool var_stato_leggi(VarStato *s, const char *percorso,
                     char *errore, size_t errore_n, int *riga)
{
    FILE *f;
    char linea[512];
    int n = 0;

    memset(s, 0, sizeof(*s));
    errore[0] = '\0';
    *riga = 0;

    f = fopen(percorso, "rb");
    if (f == NULL) {
        /* Assente NON e' un errore: e' lo stato di chi non ha ancora scelto
         * niente. s e' gia' vuota per il memset qui sopra. */
        return true;
    }

    while (fgets(linea, sizeof(linea), f)) {
        const char *p = linea;
        char variante_s[32], file_s[VAR_NOME_MAX], sha_s[256], byte_s[64];
        VarNome v = VAR_VANILLA;
        uint64_t byte;
        VarVoce *voce;
        bool e_vendor;

        n++;

        /* Riga piu' lunga del buffer: non si analizza una coda come se fosse
         * una riga a se', o il numero di riga si sfaserebbe per tutto il
         * resto del file. Si guarda il byte successivo e non feof(), che
         * dopo una fgets che ha riempito il buffer esatto non e' ancora
         * acceso. */
        if (strchr(linea, '\n') == NULL) {
            int prossimo = fgetc(f);

            if (prossimo != EOF) {
                ungetc(prossimo, f);
                snprintf(errore, errore_n,
                        "riga troppo lunga (oltre %d caratteri)",
                        (int)sizeof(linea) - 1);
                goto male;
            }
        }

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\r' || *p == '\n' || *p == '\0') {
            continue;
        }

        if (!prossima_parola(&p, variante_s, sizeof(variante_s)) ||
            !prossima_parola(&p, file_s, sizeof(file_s)) ||
            !prossima_parola(&p, sha_s, sizeof(sha_s)) ||
            !prossima_parola(&p, byte_s, sizeof(byte_s))) {
            snprintf(errore, errore_n,
                    "riga malformata: mancano campi, o un campo e' troppo "
                    "lungo (attesi variante nomefile sha256 byte)");
            goto male;
        }
        if (!resto_vuoto(p)) {
            snprintf(errore, errore_n, "campi in piu' dopo i quattro attesi");
            goto male;
        }

        /* "vendor" non e' una VarNome: e' unico, serve a entrambe le
         * varianti e non ha un /data (vedi VarStato.vendor in varianti.h),
         * quindi la sua riga si riconosce PRIMA di chiedere a var_da_testo,
         * che altrimenti la rifiuterebbe come "variante sconosciuta". */
        e_vendor = strcmp(variante_s, "vendor") == 0;
        if (!e_vendor && !var_da_testo(variante_s, &v)) {
            snprintf(errore, errore_n, "variante sconosciuta: %s", variante_s);
            goto male;
        }
        if (strlen(sha_s) != VAR_SHA256_CIFRE) {
            snprintf(errore, errore_n,
                    "sha256 di %d cifre invece di %d", (int)strlen(sha_s),
                    VAR_SHA256_CIFRE);
            goto male;
        }
        if (!leggi_byte(byte_s, &byte)) {
            snprintf(errore, errore_n, "byte non numerico: %s", byte_s);
            goto male;
        }

        /* file_s e' bloccato da prossima_parola a VAR_NOME_MAX - 1 caratteri e
         * sha_s a VAR_SHA256_CIFRE esatti (appena controllato sopra): le due
         * strcpy che seguono non possono traboccare i campi di VarVoce. */
        voce = e_vendor ? &s->vendor : &s->voci[v];
        strcpy(voce->file, file_s);
        strcpy(voce->sha256, sha_s);
        voce->byte = byte;
        continue;

    male:
        fclose(f);
        memset(s, 0, sizeof(*s));
        *riga = n;
        return false;
    }

    fclose(f);
    return true;
}

bool var_stato_scrivi(const VarStato *s, const char *percorso,
                      char *errore, size_t errore_n)
{
    FILE *f;
    int i;

    errore[0] = '\0';

    /* "wb" e non "w": su Windows "w" tradurrebbe ogni \n in \r\n, e il file
     * che ne uscirebbe non sarebbe piu' quello che var_stato_leggi si aspetta
     * di leggere byte per byte -- lo stesso motivo per cui registro.c e le
     * trame degli appunti se ne guardano. */
    f = fopen(percorso, "wb");
    if (f == NULL) {
        snprintf(errore, errore_n, "non riesco a scrivere %s", percorso);
        return false;
    }

    for (i = 0; i < VAR_QUANTE; i++) {
        const VarVoce *voce = &s->voci[i];

        if (voce->file[0] == '\0') {
            /* Mai scelta: niente riga per questa variante, non una riga di
             * campi vuoti che var_stato_leggi dovrebbe poi interpretare. */
            continue;
        }
        if (fprintf(f, "%s %s %s %llu\n", var_testo((VarNome)i), voce->file,
                    voce->sha256, (unsigned long long)voce->byte) < 0) {
            snprintf(errore, errore_n, "scrittura fallita su %s", percorso);
            fclose(f);
            return false;
        }
    }

    /* Il vendor, con la stessa parola chiave "vendor" letta sopra: stessa
     * regola delle due varianti, niente riga se non e' mai stato scaricato. */
    if (s->vendor.file[0] != '\0') {
        if (fprintf(f, "vendor %s %s %llu\n", s->vendor.file, s->vendor.sha256,
                    (unsigned long long)s->vendor.byte) < 0) {
            snprintf(errore, errore_n, "scrittura fallita su %s", percorso);
            fclose(f);
            return false;
        }
    }

    if (fclose(f) != 0) {
        snprintf(errore, errore_n, "chiusura fallita su %s", percorso);
        return false;
    }
    return true;
}

/* Porta una cifra esadecimale in minuscolo. Le cifre decimali e ogni altro
 * carattere passano invariati: qui contano solo le lettere A-F, che sono
 * l'unico punto in cui un'impronta sha256 puo' differire per maiuscole. */
static char sha256_minuscola(char c)
{
    if (c >= 'A' && c <= 'F') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/* Confronta due impronte sha256 senza distinguere maiuscole da minuscole.
 *
 * Il manifesto di Waydroid scrive l'impronta in minuscolo, ma certutil --
 * lo strumento con cui su Windows si calcola un'impronta a mano -- la
 * stampa di serie in MAIUSCOLO. Un variants.txt scritto o corretto a mano
 * copiando l'uscita di certutil avrebbe quindi cifre maiuscole, e un
 * confronto sensibile al caso farebbe risultare VAR_IMPRONTA_SBAGLIATA su
 * un'immagine che invece e' quella giusta -- esito che qui vuol dire "non
 * si avvia".
 *
 * NON si usa stricmp/strcasecmp: non sono C11 standard, e usarle romperebbe
 * la purezza di questo modulo (vedi il commento in testa al file). Le
 * uniche lettere che possono comparire in un'impronta sha256 sono A-F/a-f,
 * quindi basta normalizzare quelle. */
static bool sha256_uguale(const char *a, const char *b)
{
    size_t i;

    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (sha256_minuscola(a[i]) != sha256_minuscola(b[i])) {
            return false;
        }
    }
    return a[i] == b[i];
}

VarMancante var_cosa_manca(const VarStato *s, VarNome v,
                           bool immagine_c_e, bool dati_ci_sono,
                           const char *sha_del_file)
{
    const VarVoce *voce = &s->voci[v];

    /* Mai scaricata, oppure il file non c'e' piu': non importa cosa dice lo
     * stato, incluso il caso della voce mai scelta (voce->file[0] == 0), per
     * cui non esiste comunque un'impronta attesa con cui confrontare. */
    if (!immagine_c_e) {
        return VAR_MANCA_IMMAGINE;
    }
    /* Il file c'e' ma non e' quello atteso: NON si avvia "provando lo
     * stesso". Con una voce mai scelta, voce->sha256 e' la stringa vuota, e
     * un file vero non ha mai quell'impronta: si finisce comunque qui, che e'
     * il comportamento prudente -- un file spuntato senza passare da questo
     * stato non e' un file di cui fidarsi. */
    if (sha_del_file == NULL || !sha256_uguale(sha_del_file, voce->sha256)) {
        return VAR_IMPRONTA_SBAGLIATA;
    }
    if (!dati_ci_sono) {
        return VAR_MANCA_DATI;
    }
    return VAR_PRONTA;
}

/* Cerca la stringa C ordinaria `ago` (un letterale, quindi terminato da NUL)
 * dentro [inizio, fine), senza mai leggere da fine in poi. strstr non si
 * puo' usare qui: si aspetta un ARGOMENTO terminato da NUL, e il manifesto
 * non lo e' -- e' per questo che var_manifesto_piu_recente riceve n invece
 * di fidarsi di un terminatore che il testo arrivato dalla rete potrebbe non
 * avere, o potrebbe avere nel posto sbagliato. */
static const char *manifesto_trova(const char *inizio, const char *fine,
                                   const char *ago)
{
    size_t lung = strlen(ago);
    const char *p;

    if (lung == 0 || (size_t)(fine - inizio) < lung) {
        return NULL;
    }
    for (p = inizio; p + lung <= fine; p++) {
        if (memcmp(p, ago, lung) == 0) {
            return p;
        }
    }
    return NULL;
}

/* Avanza p oltre lo spazio bianco -- spazio, tab, CR, LF -- che il JSON
 * ammette fra i due punti di un campo e il suo valore, senza mai leggere da
 * fine in poi.
 *
 * IL MANIFESTO VERO scrive "datetime": 1775188491 con uno spazio dopo i due
 * punti, non "datetime":1775188491 senza spazio come si era assunto scrivendo
 * questo file la prima volta: manifesto_numero e manifesto_stringa si
 * fermavano proprio su quello spazio -- un carattere che non e' ne' una cifra
 * ne' la virgoletta di apertura attesa -- e il manifesto vero veniva sempre
 * rifiutato con "voce senza datetime, o datetime non numerico".
 *
 * COME E' SFUGGITO ALLE 83 PROVE GIA' VERDI: il JSON di prova era scritto a
 * mano in forma compatta, senza nessuno spazio dopo i due punti -- una prova
 * costruita sul formato immaginato, non su quello che il server di Waydroid
 * manda davvero. Vedi prova_manifesto_spaziatura_vera in test-varianti.c, che
 * usa un estratto autentico del manifesto e sarebbe fallita su questo difetto
 * prima della correzione.
 *
 * Non e' un lettore JSON generale (vedi il commento sopra
 * var_manifesto_piu_recente): tollera lo spazio bianco solo nel punto preciso
 * in cui questo modulo guarda, fra la chiave e il valore. */
static const char *manifesto_salta_spazi(const char *p, const char *fine)
{
    while (p < fine && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

/* Estrae il valore di un campo stringa "nome":"valore", dove `campo` e' la
 * chiave coi due punti compresi ma SENZA la virgoletta di apertura (per
 * esempio "\"filename\":"): quella virgoletta puo' avere spazio bianco
 * davanti nel testo vero (vedi manifesto_salta_spazi), quindi non fa parte
 * del letterale cercato -- la si cerca a runtime dopo aver saltato lo
 * spazio. Il valore finisce alla prima virgoletta successiva.
 *
 * Fallisce, senza mai guardare oltre `fine`, in quattro casi: il campo non
 * c'e' dentro [inizio, fine); dopo il campo (e lo spazio bianco che puo'
 * seguirlo) non c'e' una virgoletta di apertura; la stringa non si chiude
 * prima di `fine` (voce troncata); il valore non ci sta in max byte,
 * compreso il terminatore -- e in quest'ultimo caso NON si tronca, si
 * rifiuta, perche' un filename troncato produrrebbe un URL che non esiste. */
static bool manifesto_stringa(const char *inizio, const char *fine,
                              const char *campo, char *dest, size_t max)
{
    const char *p = manifesto_trova(inizio, fine, campo);
    const char *q;
    size_t len;

    if (p == NULL) {
        return false;
    }
    p += strlen(campo);
    p = manifesto_salta_spazi(p, fine);
    if (p >= fine || *p != '"') {
        return false;
    }
    p++;
    q = p;
    while (q < fine && *q != '"') {
        q++;
    }
    if (q >= fine) {
        return false;
    }
    len = (size_t)(q - p);
    if (len + 1 > max) {
        return false;
    }
    memcpy(dest, p, len);
    dest[len] = '\0';
    return true;
}

/* Estrae il valore di un campo numerico "nome":123, dove `campo` e' la
 * chiave con i due punti compresi (per esempio "\"size\":"). Fra i due punti
 * e la prima cifra puo' esserci lo spazio bianco che il manifesto vero
 * scrive -- vedi il commento sopra manifesto_salta_spazi -- e lo si salta
 * prima di cercare le cifre. Il valore e' poi la sequenza di cifre decimali
 * che segue, niente segno, letta con leggi_byte esattamente come i byte di
 * variants.txt: stesso rifiuto di campo vuoto e di overflow. Un valore non
 * numerico (lettere, un segno meno) da' zero cifre e fallisce qui, prima
 * ancora di chiamare leggi_byte.
 *
 * DOPO le cifre si controlla anche cosa segue, perche' fermarsi alla prima
 * non-cifra non basta: "1e10" darebbe le cifre "1" e "123.5" darebbe "123",
 * ed entrambe verrebbero lette come numeri validi (1 e 123) invece di essere
 * rifiutate -- esattamente il contrario della regola di questo modulo, che
 * rifiuta invece di fraintendere. Un size letto per difetto in questo modo
 * entra nel calcolo dello spazio libero necessario e fa partire uno
 * scaricamento che poi non ci sta. In JSON valido dopo un numero puo' venire
 * solo spazio, la virgola di un altro campo, o la graffa che chiude
 * l'oggetto: qualunque altra cosa (una lettera, un punto, un altro segno) e'
 * un errore di formato, non un numero da troncare al pezzo buono. */
static bool manifesto_numero(const char *inizio, const char *fine,
                             const char *campo, uint64_t *fuori)
{
    const char *p = manifesto_trova(inizio, fine, campo);
    const char *q;
    char buf[32]; /* un uint64_t ha al massimo 20 cifre, piu' il terminatore */
    size_t len;

    if (p == NULL) {
        return false;
    }
    p += strlen(campo);
    p = manifesto_salta_spazi(p, fine);
    q = p;
    while (q < fine && *q >= '0' && *q <= '9') {
        q++;
    }
    len = (size_t)(q - p);
    if (len == 0 || len >= sizeof(buf)) {
        return false;
    }
    /* Se q e' arrivato esattamente a fine, il carattere dopo le cifre e'
     * quello che il chiamante ha usato per delimitare questo intervallo --
     * in ogni caso di questo file, la graffa che chiude l'oggetto -- ed e'
     * uno dei separatori ammessi: si accetta senza leggere *fine, perche' le
     * funzioni di questo file non guardano mai a fine o oltre. Solo quando
     * resta un carattere DENTRO l'intervallo (q < fine) va controllato
     * davvero: e' li' che si scoprono "e" di "1e10" o "." di "123.5". */
    if (q < fine) {
        char dopo = *q;

        if (dopo != ' ' && dopo != '\t' && dopo != '\r' && dopo != '\n' &&
            dopo != ',' && dopo != '}') {
            return false;
        }
    }
    memcpy(buf, p, len);
    buf[len] = '\0';
    return leggi_byte(buf, fuori);
}

bool var_manifesto_piu_recente(const char *json, size_t n, VarVoce *fuori,
                               char *errore, size_t errore_n)
{
    const char *fine;
    const char *p;
    uint64_t miglior_datetime = 0;
    bool trovata = false;
    VarVoce migliore;

    if (fuori == NULL) {
        return false;
    }
    if (json == NULL && n > 0) {
        return false;
    }
    errore[0] = '\0';
    memset(&migliore, 0, sizeof(migliore));

    if (n == 0) {
        snprintf(errore, errore_n, "manifesto vuoto");
        return false;
    }
    fine = json + n;

    /* Si scorre cercando "datetime": -- e' L'ANCORA con cui questo ciclo
     * trova ogni voce, non solo "il primo campo" della forma vera del
     * manifesto. Una voce SENZA quella chiave non ha nulla da cui essere
     * trovata: il ciclo non la vede affatto, quindi la salta IN SILENZIO,
     * senza far fallire il manifesto -- diversamente da un campo mancante
     * dentro una voce che invece e' stata trovata. Per ogni "datetime"
     * trovato si cerca poi la graffa di chiusura DENTRO IL BUFFER: se non
     * c'e', la voce e' troncata e tutto il manifesto si rifiuta li', senza
     * guardare oltre fine. Un altro campo mancante o fuori misura in una
     * voce trovata (filename, id, size) fa invece fallire lo stesso tutto
     * il manifesto, non solo quella voce: e' lo stesso principio di
     * var_stato_leggi su una riga malformata, per lo stesso motivo -- un
     * risultato letto a meta' e' peggio di nessun risultato. */
    p = json;
    while ((p = manifesto_trova(p, fine, "\"datetime\":")) != NULL) {
        const char *oggetto_fine;
        char nome_file[VAR_NOME_MAX];
        char id[VAR_SHA256_CIFRE + 2]; /* +2: un id di 65 cifre deve stare
                                         * per intero nel buffer, cosi' il
                                         * controllo di lunghezza qui sotto
                                         * lo scopre invece di confonderlo
                                         * con "non ci sta". */
        char url[VAR_URL_MAX];
        uint64_t datetime, byte;

        oggetto_fine = manifesto_trova(p, fine, "}");
        if (oggetto_fine == NULL) {
            snprintf(errore, errore_n,
                    "manifesto troncato: una voce non si chiude entro il "
                    "buffer ricevuto");
            return false;
        }

        if (!manifesto_numero(p, oggetto_fine, "\"datetime\":", &datetime)) {
            snprintf(errore, errore_n,
                    "voce senza datetime, o datetime non numerico");
            return false;
        }
        if (!manifesto_stringa(p, oggetto_fine, "\"filename\":", nome_file,
                               sizeof(nome_file))) {
            snprintf(errore, errore_n,
                    "voce senza filename, o filename di %d caratteri o piu'",
                    VAR_NOME_MAX);
            return false;
        }
        if (!manifesto_stringa(p, oggetto_fine, "\"id\":", id,
                               sizeof(id))) {
            snprintf(errore, errore_n, "voce senza id, o id troppo lungo");
            return false;
        }
        if (strlen(id) != VAR_SHA256_CIFRE) {
            snprintf(errore, errore_n, "id di %d cifre invece di %d",
                    (int)strlen(id), VAR_SHA256_CIFRE);
            return false;
        }
        if (!manifesto_numero(p, oggetto_fine, "\"size\":", &byte)) {
            snprintf(errore, errore_n, "voce senza size, o size non numerico");
            return false;
        }
        /* "url": il campo che var_manifesto_piu_recente non leggeva prima di
         * questa correzione -- vedi VarVoce.url per il perche' leggerlo,
         * invece di ricostruirlo, e' quello che serve. Stessa regola del
         * filename: non ci sta nel buffer, la voce si rifiuta invece di
         * troncare. */
        if (!manifesto_stringa(p, oggetto_fine, "\"url\":", url,
                               sizeof(url))) {
            snprintf(errore, errore_n,
                    "voce senza url, o url di %d caratteri o piu'",
                    VAR_URL_MAX);
            return false;
        }

        if (!trovata || datetime > miglior_datetime) {
            miglior_datetime = datetime;
            strcpy(migliore.file, nome_file);
            strcpy(migliore.sha256, id);
            migliore.byte = byte;
            migliore.datetime = datetime;
            strcpy(migliore.url, url);
            trovata = true;
        }

        p = oggetto_fine + 1;
    }

    if (!trovata) {
        snprintf(errore, errore_n, "nessuna voce nel manifesto");
        return false;
    }

    *fuori = migliore;
    return true;
}

uint64_t var_spazio_necessario(VarMancante m, uint64_t archivio_byte,
                               uint64_t estratto_byte, uint64_t dati_byte)
{
    switch (m) {
    case VAR_PRONTA:
        return 0;
    case VAR_MANCA_DATI:
        return dati_byte;
    case VAR_MANCA_IMMAGINE:
    case VAR_IMPRONTA_SBAGLIATA:
        /* Un'impronta sbagliata si ripara come un'immagine mancante: si
         * riscarica l'archivio e si estrae. Si conta anche dati_byte pure se
         * il /data esistesse gia' -- sovrastimare qui e' l'unico errore che
         * non fa fermare un'installazione a meta' per mancanza di spazio non
         * previsto, e la decisione opposta (contare solo archivio+estratto)
         * richiederebbe sapere se il /data c'e' gia', informazione che questa
         * funzione PURA non riceve: var_cosa_manca l'ha gia' consumata per
         * arrivare all'esito, e non la restituisce. */
        return archivio_byte + estratto_byte + dati_byte;
    default:
        return 0;
    }
}

bool var_vendor_build_avviso(uint64_t datetime, char *avviso, size_t avviso_n)
{
    if (datetime == VENDOR_BUILD_ATTESA) {
        return false;
    }
    snprintf(avviso, avviso_n,
            "il vendor scelto ha build %llu, diversa da quella attesa %llu: "
            "l'HAL audio nell'initramfs potrebbe non corrispondere",
            (unsigned long long)datetime,
            (unsigned long long)VENDOR_BUILD_ATTESA);
    return true;
}
