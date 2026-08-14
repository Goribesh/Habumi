/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* apk.c -- cos'e' un APK, e come si installa.
 *
 * PERCHE' UN MODULO SUO: il trascinamento e' un fatto della finestra, ma
 * "cos'e' un APK" e "che comando si costruisce" sono decisioni che si possono
 * sbagliare in silenzio, e in un file separato si provano senza aprire una
 * finestra ne' accendere una VM. finestra.c resta il posto dove si RICEVE il
 * messaggio; chi orchestra e' rilascio.c.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

bool apk_e_apk(const char *percorso)
{
    size_t n;
    const char *coda;

    if (!percorso) {
        return false;
    }
    n = strlen(percorso);
    /* Piu' di quattro caratteri, non almeno quattro: ".apk" da solo e'
     * un'estensione senza nome, non un file da installare. */
    if (n <= 4) {
        return false;
    }
    coda = percorso + n - 4;
    /* Il confronto e' insensibile alle maiuscole perche' Windows lo e': un
     * GIOCO.APK e' lo stesso file di gioco.apk. Scritto a mano invece di
     * stricmp per non dipendere dalla localizzazione, che su una lettera come
     * la i turca cambierebbe risposta. */
    return coda[0] == '.'
        && (coda[1] == 'a' || coda[1] == 'A')
        && (coda[2] == 'p' || coda[2] == 'P')
        && (coda[3] == 'k' || coda[3] == 'K');
}

int apk_comando(char *dest, int max, const char *percorso)
{
    int n;

    if (!dest || max <= 0 || !percorso) {
        return 0;
    }
    n = snprintf(dest, (size_t)max, "install -r \"%s\"", percorso);
    if (n < 0 || n >= max) {
        /* Un comando troncato non e' un comando mancante: e' un comando
         * DIVERSO, e installerebbe il file sbagliato o nessuno. */
        dest[0] = '\0';
        return 0;
    }
    return n;
}

/* IL THREAD E IL LUCCHETTO NON STANNO PIU' QUI. Se ne sono andati in rilascio.c
 *, quando il trascinamento ha imparato a copiare file oltre che a
 * installare app: un install e un push in parallelo sullo stesso dispositivo
 * sono lo stesso guasto di due install in parallelo, cioe' un errore confuso
 * invece di un errore chiaro, e con due lucchetti separati -- uno qui e uno
 * la' -- quel caso sarebbe stato raggiungibile. Un lucchetto solo, in un posto
 * solo, per entrambe le azioni.
 *
 * Qui restano le due decisioni pure: cos'e' un APK, e che comando si costruisce.
 * Si provano senza aprire una finestra ne' accendere una VM. */
