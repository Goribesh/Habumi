/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-appunti-trama.c -- le prove della trama degli appunti e della guardia
 * contro l'eco.
 *
 * Come test-winq-coda.c: una macro CHECK con contatore invece di assert().
 * assert si ferma al primo fallimento e sparisce sotto NDEBUG, e qui -- dove
 * ogni singolo guasto e' SILENZIOSO, perche' produce byte plausibili invece di
 * un errore -- conviene vedere tutti i casi rotti in un colpo.
 *
 * Nessun include del guscio e nessun include di Windows, ed e' la proprieta'
 * che questa prova esiste anche per difendere: vedi il commento in cima a
 * appunti-trama.h.
 *
 * I BUFFER GRANDI SONO STATICI, mai locali. Un ApEco e i buffer delle prove al
 * tetto pesano un mebibyte l'uno, e lo stack di default su Windows e' 1 MiB: in
 * una variabile locale la pila salterebbe prima ancora della prima CHECK, con
 * un arresto che non somiglia per niente alla sua causa.
 *
 * LE LETTERE ACCENTATE SONO SCRITTE COME \xNN perche' questo file e' ASCII puro
 * come tutti i .c del progetto -- ma i byte che finiscono sul filo sono
 * esattamente gli UTF-8 che manderebbe Android. Ogni escape esadecimale chiude
 * la sua stringa: "\xC3\xA8" attaccato a una lettera che e' anche una cifra
 * esadecimale verrebbe letto dal compilatore come un unico escape piu' lungo,
 * ed e' un errore che si vede solo a valle, nei byte sbagliati. */
#include <stdio.h>
#include <string.h>
#include "appunti-trama.h"

static int passati, falliti;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            passati++;                                                       \
        } else {                                                             \
            falliti++;                                                       \
            printf("FALLITO %s:%d: ", __FILE__, __LINE__);                   \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
        }                                                                    \
    } while (0)

/* Il testo con cui il guasto si vede: piu' righe, accenti, un simbolo a tre
 * byte, e i CRLF che gli appunti di Windows mettono davvero fra una riga e
 * l'altra. Un formato che perdesse i byte alti o si fermasse al primo \r
 * passerebbe con "ciao" e fallirebbe con questo. */
static const char testo_accentato[] =
    "Prima riga: caff" "\xC3\xA8" " e perc" "\xC3\xB2" "\r\n"
    "Seconda riga: 100 " "\xE2\x82\xAC" " gi" "\xC3\xA0" " pagati\r\n"
    "Terza riga: la fine, e sul filo non c'e' nessun terminatore";

#define LUNG_ACCENTATO (sizeof(testo_accentato) - 1)

/* Il giro completo: quello che si compone e' quello che si estrae, byte per
 * byte. E' la prova che tiene insieme le altre: se fallisce lei, le altre
 * stanno misurando un formato che non esiste. */
static void prova_giro_multiriga(void)
{
    static unsigned char filo[LUNG_ACCENTATO + AP_TRAMA_INTESTAZIONE + 16];
    static char fuori[AP_TETTO_BYTE];
    size_t scritti, lung = 0, usati = 0;

    scritti = ap_trama_componi(testo_accentato, LUNG_ACCENTATO, filo, sizeof(filo));
    CHECK(scritti == LUNG_ACCENTATO + AP_TRAMA_INTESTAZIONE,
          "composti %zu byte invece di %zu", scritti,
          LUNG_ACCENTATO + AP_TRAMA_INTESTAZIONE);

    CHECK(ap_trama_estrai(filo, scritti, fuori, sizeof(fuori), &lung, &usati) == 1,
          "una trama intera non e' stata estratta");
    CHECK(lung == LUNG_ACCENTATO, "lunghezza estratta %zu invece di %zu",
          lung, LUNG_ACCENTATO);
    CHECK(usati == scritti, "usati %zu invece di %zu", usati, scritti);
    CHECK(memcmp(fuori, testo_accentato, LUNG_ACCENTATO) == 0,
          "il testo multiriga accentato non e' tornato identico");
}

/* L'ORDINE DEI BYTE, guardato a occhio nudo.
 *
 * Il guest legge la lunghezza con DataInputStream.readInt(), che e' big-endian
 * per definizione; questa macchina e' little-endian. Uno scambio di byte non fa
 * fallire il giro qui sopra -- che compone e legge con lo stesso codice -- e si
 * manifesterebbe solo contro l'emulatore, come una trama da 32 MiB. Con 258
 * byte i quattro attesi sono 00 00 01 02, che invertiti sarebbero 02 01 00 00:
 * non c'e' modo di confonderli. */
static void prova_intestazione_big_endian(void)
{
    static char testo[258];
    static unsigned char filo[sizeof(testo) + AP_TRAMA_INTESTAZIONE];
    size_t scritti;

    memset(testo, 'x', sizeof(testo));
    scritti = ap_trama_componi(testo, sizeof(testo), filo, sizeof(filo));
    CHECK(scritti == sizeof(testo) + AP_TRAMA_INTESTAZIONE,
          "258 byte piu' intestazione non ci sono stati: %zu", scritti);
    CHECK(filo[0] == 0x00 && filo[1] == 0x00 && filo[2] == 0x01 && filo[3] == 0x02,
          "intestazione %02x %02x %02x %02x invece di 00 00 01 02",
          filo[0], filo[1], filo[2], filo[3]);
}

/* UNA TRAMA SPEZZATA IN DUE LETTURE. TCP e' un flusso, non un messaggio: recv
 * puo' consegnare tre byte oggi e il resto al giro dopo, ed e' la cosa che
 * succede per prima con un testo lungo. Chi non aspetta legge una lunghezza
 * a meta' e chiude una connessione buona. */
static void prova_spezzata(void)
{
    static unsigned char filo[256];
    static char fuori[256];
    const char *testo = "una trama sola, consegnata a pezzi";
    size_t n = strlen(testo);
    size_t scritti, lung, usati;

    scritti = ap_trama_componi(testo, n, filo, sizeof(filo));
    CHECK(scritti == n + AP_TRAMA_INTESTAZIONE, "composizione fallita: %zu", scritti);

    /* Intestazione incompleta: non si sa nemmeno quanto aspettare. */
    lung = 12345;
    usati = 54321;
    CHECK(ap_trama_estrai(filo, 2, fuori, sizeof(fuori), &lung, &usati) == 0,
          "due soli byte sono bastati per dichiarare una trama");
    CHECK(lung == 12345 && usati == 54321,
          "con 0 sono stati toccati *lung o *usati: %zu %zu", lung, usati);

    /* Intestazione intera, corpo mancante. */
    CHECK(ap_trama_estrai(filo, AP_TRAMA_INTESTAZIONE, fuori, sizeof(fuori),
                          &lung, &usati) == 0,
          "la sola intestazione e' bastata per dichiarare una trama");

    /* Manca un byte solo: e' il confine dove un <= al posto di < si nasconde. */
    CHECK(ap_trama_estrai(filo, scritti - 1, fuori, sizeof(fuori), &lung, &usati) == 0,
          "una trama a cui manca un byte e' stata dichiarata completa");

    /* E adesso e' arrivato tutto. */
    lung = 0;
    usati = 0;
    CHECK(ap_trama_estrai(filo, scritti, fuori, sizeof(fuori), &lung, &usati) == 1,
          "la trama completa non e' stata estratta");
    CHECK(lung == n && memcmp(fuori, testo, n) == 0,
          "il testo riassemblato non coincide");
    CHECK(usati == scritti, "usati %zu invece di %zu", usati, scritti);
}

/* DUE TRAME NELLO STESSO BUFFER. L'altro verso dello stesso problema: due copie
 * ravvicinate arrivano in una recv sola. Chi butta l'avanzo perde la seconda
 * copia, e il difetto si vede solo copiando in fretta. */
static void prova_due_trame(void)
{
    static unsigned char filo[256];
    static char fuori[256];
    const char *primo = "il primo testo";
    const char *secondo = "il secondo, piu' lungo del primo";
    size_t n1 = strlen(primo), n2 = strlen(secondo);
    size_t a, b, lung, usati, offset;

    a = ap_trama_componi(primo, n1, filo, sizeof(filo));
    CHECK(a > 0, "la prima trama non e' stata composta");
    b = ap_trama_componi(secondo, n2, filo + a, sizeof(filo) - a);
    CHECK(b > 0, "la seconda trama non e' stata composta");

    lung = 0;
    usati = 0;
    CHECK(ap_trama_estrai(filo, a + b, fuori, sizeof(fuori), &lung, &usati) == 1,
          "la prima delle due trame non e' uscita");
    CHECK(lung == n1 && memcmp(fuori, primo, n1) == 0,
          "la prima trama e' uscita sbagliata");
    CHECK(usati == a, "usati %zu invece di %zu: il resto verrebbe disallineato",
          usati, a);

    offset = usati;
    lung = 0;
    usati = 0;
    CHECK(ap_trama_estrai(filo + offset, a + b - offset, fuori, sizeof(fuori),
                          &lung, &usati) == 1,
          "la seconda trama e' stata persa nell'avanzo");
    CHECK(lung == n2 && memcmp(fuori, secondo, n2) == 0,
          "la seconda trama e' uscita sbagliata");
    CHECK(usati == b, "usati %zu invece di %zu sulla seconda", usati, b);

    /* Consumate tutte e due, non resta niente da dichiarare. */
    CHECK(ap_trama_estrai(filo + offset + usati, 0, fuori, sizeof(fuori),
                          &lung, &usati) == 0,
          "da un avanzo vuoto e' uscita una trama");
}

/* UNA LUNGHEZZA DICHIARATA OLTRE IL TETTO DEVE DARE -1, e deve darlo GUARDANDO
 * SOLO L'INTESTAZIONE. Qui il buffer contiene quattro byte e nient'altro: se
 * l'attuazione aspettasse i byte annunciati prima di controllare il tetto,
 * risponderebbe 0 e il chiamante si metterebbe ad accumulare fino a esaurire la
 * memoria. L'ascoltatore sta su 127.0.0.1, quindi la lunghezza puo' essere
 * corrotta oppure scelta apposta. */
static void prova_tetto_dichiarato(void)
{
    unsigned char filo[AP_TRAMA_INTESTAZIONE + 8];
    static char fuori[AP_TETTO_BYTE];
    size_t lung = 777, usati = 888;
    uint32_t oltre = (uint32_t)AP_TETTO_BYTE + 1;

    memset(filo, 0, sizeof(filo));
    filo[0] = (unsigned char)(oltre >> 24);
    filo[1] = (unsigned char)(oltre >> 16);
    filo[2] = (unsigned char)(oltre >> 8);
    filo[3] = (unsigned char)oltre;
    CHECK(ap_trama_estrai(filo, sizeof(filo), fuori, sizeof(fuori), &lung, &usati) == -1,
          "un byte oltre il tetto e' stato accettato");
    CHECK(lung == 777 && usati == 888,
          "con -1 sono stati toccati *lung o *usati: %zu %zu", lung, usati);

    /* Il caso ostile vero: quattro gigabyte annunciati. */
    filo[0] = 0xFF;
    filo[1] = 0xFF;
    filo[2] = 0xFF;
    filo[3] = 0xFF;
    CHECK(ap_trama_estrai(filo, AP_TRAMA_INTESTAZIONE, fuori, sizeof(fuori),
                          &lung, &usati) == -1,
          "una lunghezza da 4 GiB non ha chiuso la connessione");

    /* E il confine dall'altra parte: ESATTAMENTE il tetto e' legittimo, e chiede
     * solo altri byte. Un >= al posto di > qui butterebbe via connessioni buone. */
    filo[0] = (unsigned char)((uint32_t)AP_TETTO_BYTE >> 24);
    filo[1] = (unsigned char)((uint32_t)AP_TETTO_BYTE >> 16);
    filo[2] = (unsigned char)((uint32_t)AP_TETTO_BYTE >> 8);
    filo[3] = (unsigned char)(uint32_t)AP_TETTO_BYTE;
    CHECK(ap_trama_estrai(filo, sizeof(filo), fuori, sizeof(fuori), &lung, &usati) == 0,
          "una trama esattamente al tetto e' stata rifiutata");
}

/* AP_TRAMA_COMPONI RIFIUTA OLTRE IL TETTO, e rifiutare vuol dire non scrivere
 * niente: non una trama troncata che un chiamante distratto spedirebbe lo
 * stesso. Il byte sentinella in cima al buffer e' li' per accorgersene.
 *
 * IL BUFFER E' PIU' GRANDE DEL TETTO DI PROPOSITO, e la ragione e' costata un
 * rosso provocato: con un buffer da AP_TETTO_BYTE + 4 il testo di un byte oltre
 * il tetto viene rifiutato PERCHE' NON CI STA, e la prova passa identica anche
 * togliendo del tutto il controllo del tetto. Con lo spazio che avanza, l'unica
 * ragione possibile del rifiuto e' quella che si vuole provare. Vale in
 * generale: una prova su un rifiuto deve escludere tutte le altre ragioni per
 * rifiutare, o non prova niente. */
static void prova_componi_tetto(void)
{
    static char grande[AP_TETTO_BYTE + 1];
    static unsigned char filo[AP_TETTO_BYTE + 1 + AP_TRAMA_INTESTAZIONE];
    unsigned char corto[16];
    size_t scritti;

    memset(grande, 'y', sizeof(grande));

    filo[0] = 0xEE;
    scritti = ap_trama_componi(grande, AP_TETTO_BYTE + 1, filo, sizeof(filo));
    CHECK(scritti == 0, "un byte oltre il tetto e' stato composto: %zu", scritti);
    CHECK(filo[0] == 0xEE, "il rifiuto ha scritto lo stesso in dest: e' un troncamento");

    /* Esattamente al tetto invece si compone: il tetto e' il massimo ammesso,
     * non il primo valore vietato. */
    scritti = ap_trama_componi(grande, AP_TETTO_BYTE, filo, sizeof(filo));
    CHECK(scritti == AP_TETTO_BYTE + AP_TRAMA_INTESTAZIONE,
          "un testo esattamente al tetto e' stato rifiutato: %zu", scritti);

    /* Destinazione troppo piccola: stesso rifiuto, stesso silenzio in dest. */
    corto[0] = 0xEE;
    scritti = ap_trama_componi("dodici byte!", 12, corto, 13);
    CHECK(scritti == 0, "dodici byte piu' intestazione sono entrati in tredici: %zu",
          scritti);
    CHECK(corto[0] == 0xEE, "il rifiuto per spazio ha scritto in dest");

    /* Nemmeno l'intestazione ci sta. */
    CHECK(ap_trama_componi("x", 1, corto, 3) == 0,
          "una trama e' stata composta in tre byte");
}

/* Il testo vuoto e' una trama legittima di soli quattro byte a zero. Non e' un
 * caso di scuola: gli appunti di Windows possono contenere una stringa vuota, e
 * un formato che ci inciampasse chiuderebbe la connessione per un incidente. */
static void prova_testo_vuoto(void)
{
    unsigned char filo[8];
    char fuori[8];
    size_t scritti, lung = 9, usati = 9;

    memset(filo, 0xEE, sizeof(filo));
    scritti = ap_trama_componi("", 0, filo, sizeof(filo));
    CHECK(scritti == AP_TRAMA_INTESTAZIONE, "il testo vuoto ha prodotto %zu byte",
          scritti);
    CHECK(filo[0] == 0 && filo[1] == 0 && filo[2] == 0 && filo[3] == 0,
          "la lunghezza zero non e' quattro byte a zero");
    CHECK(ap_trama_estrai(filo, scritti, fuori, sizeof(fuori), &lung, &usati) == 1,
          "la trama vuota non e' stata estratta");
    CHECK(lung == 0 && usati == AP_TRAMA_INTESTAZIONE,
          "trama vuota: lung %zu usati %zu", lung, usati);
}

/* LA PROVA CHE CONTA PIU' DI TUTTE.
 *
 * Senza questa guardia i due lati si rimbalzano lo stesso testo per sempre:
 * l'host scrive negli appunti, Windows notifica il cambio, l'host rimanda al
 * guest, il guest scrive nei suoi appunti, notifica, rimanda. Il ciclo non
 * finisce da solo e non somiglia a un difetto della guardia: somiglia a una
 * macchina lenta. */
static void prova_eco(void)
{
    static ApEco e;

    memset(&e, 0, sizeof(e));

    ap_eco_ricorda(&e, "ciao", 4);
    CHECK(ap_eco_uguale(&e, "ciao", 4),
          "l'ultimo ricevuto non e' stato riconosciuto: l'anello dell'eco e' aperto");
    CHECK(!ap_eco_uguale(&e, "ciaoo", 5),
          "un testo piu' lungo che comincia uguale e' stato scambiato per l'eco");
    CHECK(!ap_eco_uguale(&e, "cia", 3),
          "un prefisso e' stato scambiato per l'eco: si confronta con memcmp, non con strncmp");
    CHECK(!ap_eco_uguale(&e, "ciaa", 4),
          "un byte diverso a pari lunghezza e' passato per uguale");

    /* E' l'ULTIMO valore, non un insieme: ricordarne uno nuovo dimentica il
     * vecchio, altrimenti ricopiare un testo gia' visto smetterebbe di
     * funzionare per sempre. */
    ap_eco_ricorda(&e, "altro testo", 11);
    CHECK(ap_eco_uguale(&e, "altro testo", 11), "il valore nuovo non e' stato ricordato");
    CHECK(!ap_eco_uguale(&e, "ciao", 4), "il valore vecchio e' rimasto attivo");

    /* Il confronto e' su n byte e non su una stringa C: un NUL a meta' non
     * chiude niente. Serve a tenere memcmp dove qualcuno metterebbe strcmp. */
    ap_eco_ricorda(&e, "a\0b", 3);
    CHECK(ap_eco_uguale(&e, "a\0b", 3), "il NUL interno ha rotto il confronto");
    CHECK(!ap_eco_uguale(&e, "a\0c", 3),
          "due testi che differiscono dopo il NUL sono stati confusi");
    CHECK(!ap_eco_uguale(&e, "a", 1), "la lunghezza non e' stata confrontata");
}

/* La guardia al tetto e oltre. Oltre il tetto non puo' arrivarci nulla dal filo
 * -- ap_trama_estrai chiude prima -- ma una copia di piu' di un mebibyte dentro
 * un campo da un mebibyte e' esattamente il difetto che non si vuole scoprire in
 * produzione. Il valore ricordato prima deve restare intatto: dimenticarlo
 * riaprirebbe l'anello dell'eco proprio mentre si sta rifiutando qualcosa. */
static void prova_eco_al_tetto(void)
{
    static ApEco e;
    static char grande[AP_TETTO_BYTE + 1];

    memset(&e, 0, sizeof(e));
    memset(grande, 'z', sizeof(grande));

    ap_eco_ricorda(&e, grande, AP_TETTO_BYTE);
    CHECK(ap_eco_uguale(&e, grande, AP_TETTO_BYTE),
          "un testo esattamente al tetto non e' stato ricordato");

    ap_eco_ricorda(&e, grande, AP_TETTO_BYTE + 1);
    CHECK(ap_eco_uguale(&e, grande, AP_TETTO_BYTE),
          "un valore oltre il tetto ha cancellato quello che era ricordato");
    CHECK(!ap_eco_uguale(&e, grande, AP_TETTO_BYTE + 1),
          "un valore oltre il tetto risulta ricordato: allora e' stato copiato");
}

int main(void)
{
    prova_giro_multiriga();
    prova_intestazione_big_endian();
    prova_spezzata();
    prova_due_trame();
    prova_tetto_dichiarato();
    prova_componi_tetto();
    prova_testo_vuoto();
    prova_eco();
    prova_eco_al_tetto();

    printf("test-appunti-trama: %d su %d passati\n", passati, passati + falliti);
    return falliti ? 1 : 0;
}
