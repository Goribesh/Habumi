/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* appunti-trama.h -- la trama degli appunti condivisi, e la guardia contro l'eco.
 *
 * PERCHE' ESISTE COME MODULO A SE'. La funzione "copia e incolla fra Windows e
 * Android" e' fatta quasi tutta di cose che una prova non puo' toccare: un
 * ascoltatore Winsock, WM_CLIPBOARDUPDATE, un emulatore acceso. Le tre decisioni
 * che invece possono essere SBAGLIATE IN SILENZIO -- l'ordine dei byte della
 * lunghezza, il confine fra due trame dentro un flusso TCP, e il riconoscimento
 * del testo che ci e' appena arrivato dall'altro lato -- stanno tutte qui
 * dentro, dove si provano in millisecondi e senza accendere niente.
 *
 * NIENTE windows.h, NIENTE Winsock, NIENTE header del guscio: solo <stdint.h>,
 * <stdbool.h> e <stddef.h>. Non e' una promessa affidata a un commento, e'
 * verificata a ogni esecuzione: la riga di app/scripts/test-guscio.sh compila
 * test-appunti-trama.c insieme AL SOLO appunti-trama.c. Il giorno che questo
 * modulo cominciasse a chiamare il registro o un socket, quella prova non
 * compilerebbe piu'. E' la stessa proprieta' che winq-coda difende verso QEMU.
 *
 * IL FORMATO E' CONDIVISO CON L'APP DEL GUEST, che lo legge in Java con
 * DataInputStream.readInt() -- big-endian, cioe' l'ordine di rete. Su ARM64 il
 * nativo e' little-endian: comporre la lunghezza con un memcpy dell'intero
 * sarebbe compilato, sarebbe passato inosservato in lettura, e avrebbe prodotto
 * una trama da 16 MiB al posto di una da una riga. Per questo i quattro byte si
 * scrivono e si leggono a mano, un byte per volta.
 */
#ifndef APPUNTI_TRAMA_H
#define APPUNTI_TRAMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Il tetto.
 *
 * SI RIFIUTA, NON SI TRONCA. Un testo troncato a meta' e' peggio di un testo non
 * arrivato: chi incolla non vede il taglio e crede di avere copiato tutto. Un
 * rifiuto invece si nota subito -- gli appunti dell'altro lato restano quelli di
 * prima -- e lascia una riga nel registro.
 *
 * 1 MiB e' un dimensionamento, non una misura: e' ordini di grandezza sopra
 * qualunque testo che una persona copia davvero, e sotto la soglia in cui
 * tenerne una copia per lato diventa un problema. Sta scritto in un posto solo
 * perche' cambiarlo resti una riga. */
#define AP_TETTO_BYTE (1024 * 1024)

/* I byte di lunghezza che precedono il testo. Ha un nome perche' il "4" serve
 * anche a chi dimensiona i buffer di lettura in quella verifica, e un 4 sparso per il
 * codice e' un 4 che un giorno diventa 2 in un posto solo. */
#define AP_TRAMA_INTESTAZIONE 4

/* Compone: 4 byte big-endian di lunghezza + il testo UTF-8, senza terminatore.
 * Ritorna i byte scritti, oppure 0 se non ci stanno o se testo supera
 * AP_TETTO_BYTE.
 *
 * Uno 0 non scrive NIENTE in dest: non esiste una trama scritta a meta' che il
 * chiamante potrebbe spedire per sbaglio non guardando il valore di ritorno.
 * n == 0 e' legittimo e produce una trama di soli quattro byte a zero: se
 * mandare gli appunti vuoti abbia senso e' una decisione di prodotto, e sta nel
 * chiamante, non in una struttura di formato. */
size_t ap_trama_componi(const char *testo, size_t n, unsigned char *dest, size_t max);

/* Estrae una trama completa da un buffer che ne puo' contenere una parziale.
 *  1 = una trama estratta, *usati = quanti byte consumare
 *  0 = servono altri byte
 * -1 = lunghezza oltre il tetto: il chiamante DEVE chiudere la connessione
 *
 * Il tetto si controlla sui SOLI QUATTRO BYTE dell'intestazione, prima di
 * guardare quanti byte ci sono davvero: e' l'unico modo di non mettersi ad
 * aspettare i quattro gigabyte che una lunghezza corrotta -- o ostile, visto che
 * l'ascoltatore e' aperto su 127.0.0.1 -- puo' dichiarare.
 *
 * Con 0 e con -1 ne' *lung ne' *usati vengono toccati, e testo resta com'era: un
 * chiamante che leggesse quei valori senza guardare l'esito troverebbe cio' che
 * ci aveva messo, non spazzatura che sembra un risultato.
 *
 * Il testo estratto NON e' terminato da NUL: la lunghezza sta in *lung, e sui
 * primi n byte si lavora. max_testo piu' piccolo della lunghezza dichiarata da'
 * -1 come il tetto, perche' quel che non si puo' consegnare non si puo' nemmeno
 * saltare senza perdere l'allineamento del flusso. */
int ap_trama_estrai(const unsigned char *buf, size_t n,
                    char *testo, size_t max_testo, size_t *lung, size_t *usati);

/* LA GUARDIA CONTRO L'ECO. Ricorda l'ULTIMO valore ricevuto dall'altro lato: non
 * un flag, non un contatore, il valore. Senza, host e guest si rimbalzano lo
 * stesso testo per sempre.
 *
 * Perche' il valore e non un flag: scrivere negli appunti fa scattare la
 * notifica di cambio, ma NON e' l'unico modo in cui scatta, e non si sa quando.
 * Un flag "sto scrivendo io" alzato e abbassato sarebbe una scommessa sui tempi;
 * il confronto col valore e' vero indipendentemente da quando la notifica
 * arriva, e ha come solo effetto collaterale che ricopiare a mano lo stesso
 * identico testo non lo rimanda -- che e' invisibile, perche' l'altro lato ce
 * l'ha gia'.
 *
 * ATTENZIONE: ApEco pesa un AP_TETTO_BYTE pieno. Non e' una variabile locale --
 * lo stack di default su Windows e' 1 MiB e la pila salta prima della prima
 * riga. Statica, o allocata. */
typedef struct { char ultimo[AP_TETTO_BYTE]; size_t lung; } ApEco;
void ap_eco_ricorda(ApEco *e, const char *testo, size_t n);
bool ap_eco_uguale(const ApEco *e, const char *testo, size_t n);

#endif
