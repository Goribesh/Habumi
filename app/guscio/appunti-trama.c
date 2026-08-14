/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* appunti-trama.c -- il formato di trama degli appunti e la guardia contro l'eco.
 *
 * Vedi appunti-trama.h per il perche' di questo modulo e per le proprieta' che
 * fa rispettare. Qui c'e' solo il come.
 *
 * QUATTRO INCLUDE E BASTA, e sono tutti della libreria standard. Non e' una
 * preferenza di stile: e' la ragione per cui questo file si compila da solo in
 * test-guscio.sh, e per cui l'ordine dei byte e il confine fra due trame si
 * verificano senza accendere un emulatore. Il primo #include di windows.h
 * scritto qui dentro spegne quella prova.
 *
 * NIENTE REGISTRO. Un errore qui si comunica col valore di ritorno, non con una
 * riga scritta da qualche parte: chi chiama sa in che contesto si trova -- il
 * thread del server, il thread della finestra -- e sa dove va detto. Chiamare
 * il registro da qui, oltretutto, tirerebbe dentro tre moduli e ucciderebbe la
 * purezza del file. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "appunti-trama.h"

size_t ap_trama_componi(const char *testo, size_t n, unsigned char *dest, size_t max)
{
    if (dest == NULL || (testo == NULL && n > 0)) {
        return 0;
    }
    if (n > (size_t)AP_TETTO_BYTE) {
        return 0;
    }

    /* Scritto cosi' e non "AP_TRAMA_INTESTAZIONE + n > max": quella somma
     * traboccherebbe in silenzio con un n vicino al massimo di size_t, e il
     * confronto direbbe di si' proprio nel caso in cui bisogna dire di no. Il
     * tetto sopra rende gia' impossibile quel valore, ma il controllo non deve
     * dipendere dall'ordine in cui i due if sono scritti. */
    if (max < AP_TRAMA_INTESTAZIONE || n > max - AP_TRAMA_INTESTAZIONE) {
        return 0;
    }

    /* La lunghezza si scrive un byte per volta, dal piu' significativo: e'
     * big-endian per definizione, indipendente da come questa macchina tiene gli
     * interi. Un memcpy dell'uint32_t sarebbe piu' corto e sarebbe sbagliato su
     * ARM64, senza che niente lo segnali fino alla prova con l'emulatore. */
    dest[0] = (unsigned char)((n >> 24) & 0xFFu);
    dest[1] = (unsigned char)((n >> 16) & 0xFFu);
    dest[2] = (unsigned char)((n >> 8) & 0xFFu);
    dest[3] = (unsigned char)(n & 0xFFu);

    if (n > 0) {
        memcpy(dest + AP_TRAMA_INTESTAZIONE, testo, n);
    }
    return AP_TRAMA_INTESTAZIONE + n;
}

int ap_trama_estrai(const unsigned char *buf, size_t n,
                    char *testo, size_t max_testo, size_t *lung, size_t *usati)
{
    uint32_t dichiarata;

    if (buf == NULL || n < AP_TRAMA_INTESTAZIONE) {
        return 0;
    }

    dichiarata = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                 ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];

    /* IL TETTO SI CONTROLLA PRIMA DI CONTARE I BYTE PRESENTI, e l'ordine e' la
     * sostanza di questa funzione. Chi controllasse prima "ne sono arrivati
     * abbastanza?" risponderebbe 0 a una lunghezza da quattro gigabyte, e il
     * chiamante si metterebbe ad accumulare per sempre in attesa di byte che
     * nessuno mandera'. L'ascoltatore sta su 127.0.0.1: quella lunghezza puo'
     * essere corrotta, o scelta apposta da qualunque cosa giri sulla macchina. */
    if (dichiarata > (uint32_t)AP_TETTO_BYTE) {
        return -1;
    }

    /* Buffer del chiamante piu' piccolo del testo annunciato: e' un errore di
     * chi chiama e non del filo, ma la conseguenza e' la stessa -- questa trama
     * non si puo' consegnare, e saltarla lascerebbe il flusso disallineato per
     * sempre. Si tratta come il tetto: -1, si chiude. Nel guscio non succede,
     * perche' i buffer sono dimensionati su AP_TETTO_BYTE. */
    if ((size_t)dichiarata > max_testo) {
        return -1;
    }

    /* Sottrazione e non somma, di nuovo per non far traboccare n. */
    if (n - AP_TRAMA_INTESTAZIONE < (size_t)dichiarata) {
        return 0;
    }

    if (dichiarata > 0) {
        memcpy(testo, buf + AP_TRAMA_INTESTAZIONE, (size_t)dichiarata);
    }
    if (lung != NULL) {
        *lung = (size_t)dichiarata;
    }
    if (usati != NULL) {
        *usati = AP_TRAMA_INTESTAZIONE + (size_t)dichiarata;
    }
    return 1;
}

void ap_eco_ricorda(ApEco *e, const char *testo, size_t n)
{
    if (e == NULL || (testo == NULL && n > 0)) {
        return;
    }

    /* Oltre il tetto NON si tronca e NON si dimentica: si lascia tutto com'era.
     *
     * Dal filo non puo' arrivare -- ap_trama_estrai chiude prima -- ma copiare
     * piu' di un mebibyte dentro un campo da un mebibyte e' il difetto che non
     * si vuole scoprire in produzione. Fra le tre uscite possibili, troncare
     * farebbe scambiare per eco un testo diverso che comincia uguale, e azzerare
     * dimenticherebbe l'ultimo valore buono riaprendo l'anello proprio mentre si
     * sta rifiutando qualcosa. Non fare niente e' l'unica che non peggiora lo
     * stato: quel testo semplicemente non verra' riconosciuto, e ap_trama_componi
     * si rifiutera' comunque di spedirlo. */
    if (n > (size_t)AP_TETTO_BYTE) {
        return;
    }

    if (n > 0) {
        memcpy(e->ultimo, testo, n);
    }
    e->lung = n;
}

bool ap_eco_uguale(const ApEco *e, const char *testo, size_t n)
{
    if (e == NULL || (testo == NULL && n > 0)) {
        return false;
    }

    /* La lunghezza per prima, e memcmp e non strcmp: un testo che comincia come
     * quello ricordato ma prosegue non e' l'eco, e non mandarlo perderebbe una
     * copia che l'utente ha fatto davvero. Il testo degli appunti puo' inoltre
     * contenere un NUL, e una funzione di stringa si fermerebbe li'. */
    if (e->lung != n) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    return memcmp(e->ultimo, testo, n) == 0;
}
