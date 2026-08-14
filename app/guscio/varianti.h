/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* varianti.h -- quale immagine di Android si avvia, e cosa manca per avviarla.
 *
 * MODULO PURO: niente Win32, niente rete, nessun altro modulo del guscio.
 * In test-guscio.sh la riga delle dipendenze e' vuota, ed e' quel confine reso
 * eseguibile: il giorno che questo file aprisse una connessione o chiamasse il
 * registro, la sua prova non compilerebbe. */
#ifndef VARIANTI_H
#define VARIANTI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VAR_SHA256_CIFRE  64          /* 32 byte in esadecimale */
#define VAR_NOME_MAX     128
#define VAR_PERCORSO_MAX 260          /* MAX_PATH, senza includere windows.h */
/* Margine ampio: l'URL piu' lungo visto finora (l'estratto autentico in
 * prova_manifesto_spaziatura_vera, test-varianti.c) ne usa circa 160. Stessa
 * taglia dei buffer che main.c usava per l'URL ricostruito prima che questo
 * campo esistesse. */
#define VAR_URL_MAX      512

typedef enum { VAR_VANILLA = 0, VAR_GAPPS = 1, VAR_QUANTE = 2 } VarNome;

/* Cio' che si sa di un'immagine: come si chiama il file scaricato, la sua
 * impronta e quanti byte deve avere. Vuota (nome[0] == 0) se non e' mai stata
 * scelta.
 *
 * datetime E url SONO SOLO DI PASSAGGIO: var_manifesto_piu_recente li riempie
 * coi campi "datetime" e "url" della voce scelta -- datetime per il
 * controllo sulla build del vendor (var_vendor_build_avviso, piu' sotto), url
 * perche' e' l'indirizzo VERO da cui scaricare, invece di uno ricostruito da
 * una base piu' il nome del file. La ricostruzione e' una trappola: i
 * percorsi di SourceForge NON sono uguali fra i tipi di immagine (system ha
 * il segmento "lineage/", vendor no), e ci si cade in silenzio al prossimo
 * tipo che si aggiunge -- il manifesto invece il suo url ce l'ha gia'.
 * var_stato_leggi e var_stato_scrivi non toccano ne' l'uno ne' l'altro -- il
 * file di stato resta a quattro campi, come sempre -- quindi dopo un giro di
 * scrittura e rilettura valgono 0 e stringa vuota, non i valori di quando la
 * voce e' stata scelta. */
typedef struct {
    char     file[VAR_NOME_MAX];
    char     sha256[VAR_SHA256_CIFRE + 1];
    uint64_t byte;
    uint64_t datetime;
    char     url[VAR_URL_MAX];
} VarVoce;

typedef struct {
    VarVoce voci[VAR_QUANTE];
    VarVoce vendor;        /* unico, serve a entrambe le varianti, non ha un /data */
} VarStato;

/* Il nome della variante come si scrive in config.txt e nel file di
 * stato. Ritorna "vanilla" o "gapps". */
const char *var_testo(VarNome v);

/* Il contrario: ritorna false se il testo non e' una variante conosciuta. Chi
 * chiama decide cosa fare -- la spec dice: ripiegare su vanilla e scriverlo. */
bool var_da_testo(const char *s, VarNome *fuori);

/* I due percorsi con cui si avvia questa variante, relativi alla radice del
 * progetto. Sono fissi e non dipendono dallo stato: e' la tabella della spec. */
void var_percorsi(VarNome v, const char **immagine, const char **dati);

/* Legge runtime/variants.txt. Un file assente NON e' un errore: e' lo stato di
 * chi non ha ancora scelto niente, e si ottiene una VarStato vuota.
 * Su riga malformata ritorna false e riempie errore e riga. */
bool var_stato_leggi(VarStato *s, const char *percorso,
                     char *errore, size_t errore_n, int *riga);

/* Riscrive runtime/variants.txt per intero. Apre in "wb": su Windows "w"
 * tradurrebbe ogni \n in \r\n. */
bool var_stato_scrivi(const VarStato *s, const char *percorso,
                      char *errore, size_t errore_n);

/* Cosa manca per poter avviare una variante. I quattro stati sono quelli che il
 * menu deve saper mostrare. */
typedef enum {
    VAR_PRONTA,            /* immagine presente e con l'impronta attesa, dati presenti */
    VAR_MANCA_IMMAGINE,    /* mai scaricata, oppure il file non c'e' piu' */
    VAR_MANCA_DATI,        /* immagine a posto, il /data va creato */
    VAR_IMPRONTA_SBAGLIATA /* il file c'e' ma non e' quello atteso: NON si avvia */
} VarMancante;

/* immagine_c_e / dati_ci_sono / sha_del_file li accerta il chiamante, che e'
 * l'unico a poter guardare il disco: cosi' questa decisione resta pura e
 * provabile. sha_del_file puo' essere NULL quando l'immagine non c'e'. */
VarMancante var_cosa_manca(const VarStato *s, VarNome v,
                           bool immagine_c_e, bool dati_ci_sono,
                           const char *sha_del_file);

/* Byte liberi che servono per portare una variante fino a PRONTA, partendo
 * dallo stato dato. Al PICCO: l'archivio convive con l'immagine estratta finche'
 * non viene cancellato. dati_byte e' la dimensione del /data da creare. */
uint64_t var_spazio_necessario(VarMancante m, uint64_t archivio_byte,
                               uint64_t estratto_byte, uint64_t dati_byte);

/* Sceglie dal manifesto la voce PIU' RECENTE, cioe' quella col datetime piu'
 * alto, e ne estrae nome del file, sha256, byte, datetime e url.
 *
 * Il manifesto e' il JSON ufficiale di Waydroid, a un URL fisso, e ha questa
 * forma (spazio dopo i due punti compreso -- e' quello che il server manda
 * davvero, non il JSON compatto senza spazi che si potrebbe immaginare):
 *     {"response": [{"datetime": 1775188491,
 *                    "filename": "lineage-...-GAPPS-...-system.zip",
 *                    "id": "7706b04e...",
 *                    "romtype": "GAPPS", "size": 1171824911,
 *                    "url": "https://...", "version": "20.0"}, ...]}
 * Lo spazio bianco (spazio, tab, ritorno a capo) fra i due punti di un campo
 * e il suo valore e' tollerato in entrambe le forme, compatta o spaziata:
 * vedi manifesto_salta_spazi in varianti.c.
 *
 * Il campo "id" E' lo sha256: verificato confrontando la voce
 * VANILLA del 20260403 con il file gia' scaricato e verificato --
 * c3cf6ab5...127b e 728517282 combaciano entrambi.
 *
 * Il campo "url" E' L'INDIRIZZO DA CUI SCARICARE, gia' completo: si legge
 * cosi' com'e' (vedi VarVoce.url per il perche' non si ricostruisce piu' da
 * una base fissa). Stessa regola del filename per la lunghezza: un url che
 * non sta in VAR_URL_MAX byte fa fallire la voce, non si tronca -- un
 * indirizzo troncato scaricherebbe una pagina d'errore HTML e la chiamerebbe
 * immagine.
 *
 * NON e' un lettore JSON generale, ed e' voluto: cerca i campi che servono e
 * rifiuta tutto il resto. Un lettore generale sarebbe piu' codice da provare
 * per leggere un file la cui forma non decidiamo noi.
 *
 * json non e' terminato da NUL: arriva dalla rete, e n e' la sua lunghezza
 * vera. Nessuna voce ne' campo viene mai letto oltre json + n. Su false,
 * *fuori non viene toccato ed errore spiega il motivo (nessuna voce, campo
 * mancante, lunghezza sbagliata, voce troncata, ...). */
bool var_manifesto_piu_recente(const char *json, size_t n, VarVoce *fuori,
                               char *errore, size_t errore_n);

/* La build del vendor per cui l'HAL audio nell'initramfs e' stato costruito.
 *
 * E' il campo ro.vendor.build.date.utc dell'immagine, che il manifesto di
 * Waydroid riporta come "datetime": lo stesso numero, verificato
 * sulla nostra vendor.img vergine e sulla voce MAINLINE 20260403 (vedi i
 * fatti verificati di .superpowers/sdd/rl-vincoli.md).
 *
 * Serve perche' l'HAL e' LEGATO a quella build: sovrapporlo a un'altra rompe
 * l'audio in silenzio, ed e' un guasto che nessuna prova puo' vedere. Con
 * questo numero il prodotto lo DICE invece di procedere alla cieca. */
#define VENDOR_BUILD_ATTESA ((uint64_t)1775198504ULL)

/* Confronta datetime -- il campo "datetime" della voce di manifesto scelta
 * per il vendor, che var_manifesto_piu_recente mette in VarVoce.datetime --
 * con VENDOR_BUILD_ATTESA.
 *
 * Se combaciano ritorna false e non tocca avviso: e' la build per cui l'HAL
 * audio nell'initramfs e' stato costruito, nessun avviso da dare.
 *
 * Se differiscono ritorna true e scrive in avviso (che deve avere almeno
 * 160 byte) il messaggio da versare nel registro, con le due date a
 * confronto: il chiamante PROCEDE comunque -- un vendor nuovo e' sempre
 * meglio di nessun vendor -- ma chi legge il registro sa cosi' cosa
 * guardare se l'audio suona storto. */
bool var_vendor_build_avviso(uint64_t datetime, char *avviso, size_t avviso_n);

#endif /* VARIANTI_H */
