/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* file.c -- cosa si copia nel guest, e come.
 *
 * PERCHE' UN MODULO SUO: il trascinamento e' un fatto della finestra, ma "quale
 * nome questo canale trasporta" e "che comando si costruisce" sono decisioni che
 * si sbagliano in silenzio, e in un file separato si provano senza aprire una
 * finestra ne' accendere una VM. Chi orchestra e' rilascio.c; questo modulo non
 * sa che esistono i dialoghi, i thread e gli APK.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

/* La barra finale NON e' cosmetica: con "push X /sdcard/Download" e la cartella
 * assente adb crea un FILE di nome Download, e il file "arriva" con un nome che
 * nessuna app trovera'. Con la barra, adb sa che il bersaglio e' una cartella.
 * La prova negativa in test-file.c protegge questa riga. */
#define FILE_BERSAGLIO "/sdcard/Download/"

bool file_nome_trasportabile(const wchar_t *nome)
{
    BOOL sostituito = FALSE;
    int n;

    if (!nome) {
        return false;
    }
    /* WC_NO_BEST_FIT_CHARS e' la meta' che conta: senza, Windows "aiuta"
     * sostituendo per somiglianza (una lettera accentata diventa la sua base, un
     * ideogramma diventa '?') e la sostituzione NON verrebbe segnalata. Il file
     * arriverebbe nel guest con un nome DIVERSO da quello che l'utente vede, che
     * e' peggio di non arrivare: nessun errore, e un file che non si ritrova.
     *
     * Si passa NULL come destinazione perche' qui non serve la conversione, solo
     * sapere se perderebbe qualcosa: con cbMultiByte a 0, WideCharToMultiByte
     * misura invece di convertire. */
    n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, nome, -1,
                            NULL, 0, NULL, &sostituito);
    return n > 0 && !sostituito;
}

int file_comando_push(char *dest, int max, const char *percorso)
{
    int n;

    if (!dest || max <= 0) {
        return 0;
    }
    dest[0] = '\0';
    if (!percorso || !percorso[0]) {
        return 0;
    }
    n = snprintf(dest, (size_t)max, "push \"%s\" \"%s\"", percorso,
                 FILE_BERSAGLIO);
    if (n < 0 || n >= max) {
        /* Un comando troncato non e' un comando mancante: e' un comando
         * DIVERSO, e copierebbe altrove o niente. */
        dest[0] = '\0';
        return 0;
    }
    return n;
}
