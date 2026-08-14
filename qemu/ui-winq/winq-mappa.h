/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-mappa.h -- la mappa dei tasti, e NIENTE ALTRO.
 *
 * Questo modulo non include Windows, non include QEMU e non tocca lo schermo:
 * prende un percorso, restituisce azioni in permille. E' la stessa proprieta'
 * di winq-coda.c e winq-coord.c, e serve a poterlo provare senza una VM.
 * qemu/scripts/test-mappa.sh e' la documentazione eseguibile di questo
 * confine: se un giorno questo file cominciasse a chiamare Windows, quella
 * riga di compilazione smetterebbe di funzionare. */
#ifndef WINQ_MAPPA_H
#define WINQ_MAPPA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAPPA_TOCCHI_MAX   32

/* Il caso peggiore NON e' il BEGIN+UPDATE del joystick, che sono due azioni:
 * e' mappa_rilascia_tutto con ogni dito giu', cioe' 32 tocchi piu' il
 * joystick. Dimensionare su due qui vorrebbe dire troncare i rilasci proprio
 * nel caso pensato per evitare le dita incollate. */
#define MAPPA_AZIONI_MAX   (MAPPA_TOCCHI_MAX + 1)

/* Identificativi dei diti sintetici, alti di proposito: quelli che Windows
 * mette nei messaggi del puntatore sono interi piccoli, quindi non collidono
 * mai dentro winq_slot_per_pointer, che confronta per identificativo. */
#define MAPPA_ID_JOYSTICK  0x4D000000u
#define MAPPA_ID_TOCCO(i) (0x4D000001u + (uint32_t)(i))

typedef enum {
    MAPPA_AZ_BEGIN,
    MAPPA_AZ_UPDATE,
    MAPPA_AZ_END
} MappaTipoAzione;

typedef struct {
    MappaTipoAzione tipo;
    uint32_t        id;
    int32_t         x_pm, y_pm;   /* permille; ignorati quando tipo e' END */
} MappaAzione;

typedef struct Mappa Mappa;

Mappa *mappa_crea(void);
void   mappa_distruggi(Mappa *m);

/* Le proporzioni dell'area cliente, in pixel. SOLO NUMERI: il modulo non
 * guarda nessuna finestra, glieli passa il chiamante.
 *
 * Servono perche' il raggio del joystick e' in permille del LATO CORTO (spec
 * sezione 4), mentre le coordinate sono in permille di larghezza e altezza.
 * Senza queste due misure il modulo non puo' convertire fra le due unita' e lo
 * stesso raggio darebbe uno spostamento in PIXEL diverso sui due assi: su
 * un'area 1600x900 con raggio 120, "destra" sposterebbe 192 px e "su" 108 --
 * uno stick ellittico, cioe' esattamente il guasto che la normalizzazione
 * delle diagonali serve a evitare, rifatto in pixel. Lo stesso vale nel modo
 * impara, dove senza queste misure lo stesso gesto visivo darebbe raggi quasi
 * doppi a seconda della direzione in cui lo si fa.
 *
 * Da chiamare quando la mappa si carica e a OGNI cambio dell'area cliente.
 * Finche' non sono state impostate (o se una delle due e' <= 0, per esempio a
 * finestra ridotta a icona) il modulo si comporta come su un'area QUADRATA,
 * cioe' come prima che questa funzione esistesse: e' l'unica risposta sensata
 * quando non c'e' niente da cui dedurle, e non e' una divisione per zero.
 *
 * Non sono un dato del FILE: mappa_carica le conserva attraverso il
 * caricamento, o cambiare profilo riporterebbe di nascosto lo stick ellittico
 * fino al primo ridimensionamento. */
void mappa_imposta_proporzioni(Mappa *m, int larghezza_px, int altezza_px);

/* Conversione fra pixel di un lato dell'area cliente e permille, nei due versi.
 *
 * Stanno QUI, e non nel chiamante che ha la finestra, per due ragioni: sono
 * solo numeri, e la spec (sezione 13) pretende che pixel->permille->pixel torni
 * al punto di partenza entro un pixel -- una proprieta' che si prova solo dove
 * si puo' compilare senza VM.
 *
 * Arrotondano ENTRAMBI al piu' vicino invece di troncare: con due troncamenti
 * di fila su un'area da 2880 px, 2879 diventava 999 permille e tornava 2877,
 * due pixel di scarto, e il modo impara scriveva un punto diverso da quello
 * cliccato.
 *
 * mappa_pm_a_px SATURA a lato_px-1: 1000 permille e' un valore legale del
 * formato, ma pm * lato / 1000 con pm = 1000 darebbe lato, cioe' un pixel FUORI
 * dall'area cliente, e il tocco verrebbe rifiutato piu' a valle -- "tocco
 * SPAZIO 500 1000" non scattava mai, in silenzio.
 *
 * Con lato_px <= 0 (finestra ridotta a icona) rispondono zero: non c'e' nessun
 * pixel da indicare, e dividere per zero sarebbe peggio. */
int mappa_pm_a_px(int pm, int lato_px);
int mappa_px_a_pm(int px, int lato_px);

/* Legge il file. RIFIUTA TUTTO se una riga e' malformata: niente mappe a meta'.
 *
 * Su errore ritorna false, scrive il motivo in errore (terminato) e il numero
 * di riga in riga. IN CASO DI ERRORE LA MAPPA RESTA VUOTA e non torna quella
 * di prima: una mappa vecchia che sopravvive a un caricamento fallito e' il
 * modo in cui si crede di aver cambiato profilo e invece si sta giocando col
 * precedente. Le proporzioni impostate con mappa_imposta_proporzioni NON si
 * perdono: sono una proprieta' della finestra, non del file.
 *
 * Il joystick si valida COME INSIEME e non un campo alla volta: cx-r, cx+r,
 * cy-r e cy+r devono stare tutti in 0..1000. "joystick WASD 200 750 250" ha i
 * tre campi in intervallo ma manda il dito a -50 permille quando si preme A:
 * il record viene scartato piu' a valle e la sola direzione "sinistra" non
 * funziona, senza che niente lo dica.
 *
 * La verifica e' PER ASSE e usa le proporzioni gia' impostate, con le stesse
 * conversioni che il modulo applichera' a runtime: il raggio e' in permille del
 * lato corto, cx e cy in permille dei rispettivi assi. Confrontarli
 * direttamente rifiuta profili validi -- su 1600x900 "joystick WASD 150 500
 * 200" manda il dito a 37 permille, ben dentro, e veniva respinto con un
 * messaggio che non era vero. Senza proporzioni vale il caso quadrato.
 *
 * I quattro tasti del joystick devono essere tutti DIVERSI: "joystick WWWW"
 * verrebbe accettato con tre indici su quattro morti. */
bool mappa_carica(Mappa *m, const char *percorso,
                  char *errore, size_t errore_n, int *riga);

/* Traduce una pressione o un rilascio.
 *
 * vk           virtual-key di Windows
 * ripetizione  true se e' ripetizione automatica (bit 30 di LPARAM). Windows
 *              manda WM_KEYDOWN a raffica mentre il tasto e' tenuto premuto, e
 *              senza scartarle un tocco mantenuto diventerebbe una raffica di
 *              BEGIN.
 *
 * Ritorna quante azioni ha scritto in az. ZERO significa "questo tasto non e'
 * mappato": il chiamante lo consegna al guest come tasto, com'e' oggi. */
int mappa_tasto(Mappa *m, int vk, bool premuto, bool ripetizione,
                MappaAzione *az, int az_max);

/* True se questo tasto appartiene alla mappa, anche quando mappa_tasto non ha
 * prodotto azioni. Serve al chiamante per NON consegnarlo al guest: senza,
 * tenere premuto W produrrebbe azioni la prima volta e la lettera "w" a ogni
 * ripetizione successiva.
 *
 * SI CONSULTA SOLO SU UNA PRESSIONE, mai su un rilascio, e la ragione e' un
 * tasto incollato nel guest: se la mappa si ACCENDE mentre W e' gia' tenuto
 * premuto (fine di un modo impara, scelta di un profilo dal menu), il guest ha
 * gia' ricevuto KEY_W DOWN come tasto normale. Al rilascio mappa_tasto ritorna
 * zero -- per la mappa niente e' cambiato, quel tasto non era premuto -- e un
 * chiamante che qui leggesse "e' mio" sopprimerebbe il KEYUP: il guest
 * resterebbe con W premuto per sempre, con l'autoripetizione di Android sopra
 * e nessuna via d'uscita. La soppressione serve unicamente a scartare la
 * ripetizione automatica, che e' sempre una PRESSIONE. Un UP consegnato senza
 * il suo DOWN, nel caso legittimo, e' invece innocuo. */
bool mappa_tasto_e_mio(const Mappa *m, int vk);

/* Rilascia ogni dito sintetico ancora giu'. Ritorna quante END ha scritto.
 * Da chiamare quando la mappa si spegne, quando la finestra perde il fuoco e
 * prima di cambiare profilo: senza, il guest resta con un dito incollato. */
int mappa_rilascia_tutto(Mappa *m, MappaAzione *az, int az_max);

/* --- modo impara ---
 *
 * Il chiamante converte il clic in permille e lo passa qui. Il salvataggio su
 * file e' una funzione separata perche' cosi' il modulo resta provabile senza
 * toccare il disco: le prove chiamano le prime e leggono il risultato con
 * mappa_tasto. */
typedef enum {
    MAPPA_IMPARA_TOCCO,
    MAPPA_IMPARA_JOYSTICK
} MappaImparaTipo;

/* Ritorna false, SENZA entrare in modo impara, quando tipo e' MAPPA_IMPARA_JOYSTICK
 * e uno fra W, A, S, D e' gia' un tocco. I quattro tasti del joystick sono
 * SEMPRE W, A, S, D: il conflitto si conosce gia' qui, prima di qualunque clic,
 * e non serve aspettare mappa_impara_punto per scoprirlo. Senza questo
 * controllo il modo impara potrebbe fissare il joystick su un tasto gia'
 * assegnato a un tocco, mappa_salva scriverebbe quel file, e mappa_carica lo
 * rifiuterebbe subito dopo: lo stesso modulo che produce un file che non sa
 * rileggere. Per MAPPA_IMPARA_TOCCO ritorna sempre true: li' il tasto non e'
 * ancora noto, e il controllo resta dov'era, in mappa_impara_tasto. */
bool mappa_impara_inizia(Mappa *m, MappaImparaTipo tipo);

/* Ritorna true se c'era davvero un modo impara da annullare. */
bool mappa_impara_annulla(Mappa *m);

/* Il tasto da assegnare. Ignorata per il joystick, che usa sempre WASD.
 * Ritorna false se il tasto non e' fra quelli riconosciuti. */
bool mappa_impara_tasto(Mappa *m, int vk);

/* L'esito di un clic del modo impara. TRE stati e non un bool, perche' con due
 * "non completo" e "impossibile" si confondono: quando i tocchi erano gia'
 * MAPPA_TOCCHI_MAX il modulo CHIUDEVA il modo impara e ritornava false, il
 * chiamante leggeva false come "serve un altro clic" e restava con il proprio
 * flag acceso. Da li' in poi i due stati divergevano -- ogni clic cadeva nel
 * vuoto, la mappa restava spenta, nessuna riga di registro -- e l'unica uscita
 * era una scorciatoia che l'utente non ha motivo di provare. */
typedef enum {
    MAPPA_ESITO_ANCORA,    /* manca ancora qualcosa: il modo impara e' APERTO */
    MAPPA_ESITO_FATTO,     /* assegnazione completa, modo impara CHIUSO */
    MAPPA_ESITO_FALLITO    /* impossibile: modo impara CHIUSO, niente scritto */
} MappaImparaEsito;

/* Un clic del modo impara, in permille.
 *
 * MAPPA_ESITO_FATTO quando l'assegnazione e' completa e il modo impara si e'
 * chiuso: per il tocco al primo punto, per il joystick al secondo -- centro,
 * poi un punto sul bordo che ne fissa il raggio.
 *
 * MAPPA_ESITO_ANCORA quando il modo impara resta aperto: il primo clic del
 * joystick, oppure un clic del tocco arrivato PRIMA del tasto da assegnare
 * (mappa_impara_tasto non e' ancora stata chiamata con successo). Il chiamante
 * sa quale dei due e' -- conosce il tipo -- e deve scriverlo nel registro: un
 * clic che non produce niente e non dice niente sembra un guasto.
 *
 * MAPPA_ESITO_FALLITO quando l'assegnazione non si puo' fare e il modo impara
 * si e' chiuso da se': niente modo impara aperto; oppure gia' MAPPA_TOCCHI_MAX
 * tocchi (solo per il tocco); oppure, per il joystick, un raggio che verrebbe
 * ZERO -- centro esattamente su un bordo, o due clic nello stesso punto. Il
 * chiamante deve chiudere anche il PROPRIO stato e scriverlo nel registro.
 *
 * Un tasto gia' assegnato viene SOSTITUITO e non aggiunto una seconda volta:
 * due righe per lo stesso tasto sono un errore di lettura, e il modo impara non
 * deve poter scrivere un file che poi si rifiuta di leggere. Per la stessa
 * ragione il raggio del joystick viene RIDOTTO quanto basta a tenere il cerchio
 * dentro 0..1000 su entrambi gli assi: mappa_carica rifiuta un joystick che
 * esce dall'area, e questo modo non deve poterne produrre uno.
 *
 * La riduzione a ZERO non si scrive: uno stick di raggio zero ha BEGIN e UPDATE
 * nello stesso punto, si rilegge senza un errore e non produce nessuna
 * direzione. Un fallimento che il chiamante annuncia e' meglio di una mappa che
 * si carica e non fa niente. */
MappaImparaEsito mappa_impara_punto(Mappa *m, int x_pm, int y_pm);

/* Il raggio dell'ULTIMO modo impara del joystick che si e' concluso: quello che
 * il gesto ha MISURATO e quello finito nella mappa. Ritorna false se in questa
 * mappa non se n'e' concluso nessuno (compreso dopo un mappa_carica, che azzera
 * tutto, e dopo un mappa_impara_inizia, che ricomincia).
 *
 * ESISTE PERCHE' LA RIDUZIONE E' ALTRIMENTI INVISIBILE. Il chiamante confronta
 * i due numeri e, se differiscono, lo scrive nel registro accanto a "mappa
 * salvata": senza, il caso frequente -- lo stick di un gioco sta in un ANGOLO,
 * e li' il massimo consentito e' piccolo -- da' uno stick che funziona ma
 * "tira" a una distanza diversa da quella misurata, e non c'e' niente da cui
 * capirlo. Sui due clic che avrebbero prodotto un raggio zero mappa_impara_punto
 * ritorna MAPPA_ESITO_FALLITO, e questi due numeri dicono al chiamante perche':
 * usato vale zero. */
bool mappa_impara_raggio(const Mappa *m, int *misurato, int *usato);

/* Riscrive il file dalla mappa in memoria, CONSERVANDO il blocco di commenti in
 * TESTA al file esistente (le righe che iniziano con '#' prima della prima riga
 * di dati) e riemettendolo in cima al file nuovo. Senza, il primo "impara un
 * tasto" cancellava in silenzio le sei righe che documentano il formato in
 * runtime/keymaps/generico.txt, e qualunque annotazione dell'utente.
 *
 * Se il file non esiste, o non ha commenti in testa, si scrive l'intestazione
 * fissa. I commenti in MEZZO e in CODA non si conservano, e nemmeno una riga di
 * commento piu' lunga di quella che il lettore accetta, che chiude la testa
 * invece di entrarci mutilata: limiti dichiarati nella spec, sezione 14.
 *
 * Ritorna false anche quando la SCRITTURA fallisce, non solo l'apertura: il
 * file viene troncato subito, quindi un disco pieno lascia una mappa mutilata,
 * e i commenti dell'utente che c'erano dentro non si ricostruiscono da quella
 * in memoria. Un "salvata" scritto nel registro sopra un file a meta' e' il
 * modo in cui la perdita si scopre al prossimo avvio. */
bool mappa_salva(Mappa *m, const char *percorso, char *errore, size_t errore_n);

#endif /* WINQ_MAPPA_H */
