/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* archivio.c -- conservare il file del giro precedente invece di distruggerlo.
 *
 * PERCHE' ESISTE, e non e' prudenza generica. Due file del guscio nascono puliti
 * a ogni avvio, per due ragioni buone e documentate:
 *   - la seriale del guest (vm.c): cancellata in vm_apri, altrimenti si legge un
 *     "boot_completed" di due ore prima come se fosse quello appena fatto;
 *   - il registro (registro.c): aperto con "w", altrimenti si prende per nuovo un
 *     messaggio del penultimo avvio.
 * Insieme rendono impossibile indagare un guasto RARO.
 * system_server e' morto durante un install, e la riga del kernel che nominava il
 * pid giusto e' esistita: e' andata perduta al riavvio successivo, e di quel
 * crash restano solo le righe che qualcuno aveva copiato a mano. Con un difetto
 * che si presenta una volta ogni qualche ora, ogni occorrenza persa costa una
 * riproduzione intera.
 *
 * QUESTO MODULO NON CAMBIA QUELLE DUE RAGIONI: il file ATTIVO resta pulito a ogni
 * avvio. Cambia solo che il precedente viene spostato, non cancellato.
 *
 * PERCHE' UN MODULO SUO E NON DUE COPIE: i chiamanti sono due, vm.c e registro.c.
 * In questo guscio il percorso di adb.exe era stato costruito in due punti e le
 * due copie erano GIA' divergenti quando la revisione le ha trovate. Una sola
 * implementazione, come guscio_percorso_accanto.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

int archivio_nome(char *dest, int max, const char *cartella,
                  const char *prefisso, const SYSTEMTIME *ora)
{
    int n;

    if (!dest || max <= 0) {
        return 0;
    }
    dest[0] = '\0';
    if (!cartella || !prefisso || !ora) {
        return 0;
    }
    /* ANNO-MESE-GIORNO poi l'ora, con gli zeri davanti: cosi' l'ordine
     * ALFABETICO e' l'ordine cronologico, e potare i piu' vecchi diventa
     * "cancella i primi" invece di richiedere un confronto di date. Senza gli
     * zeri "9" verrebbe dopo "10" e la potatura butterebbe il file sbagliato. */
    n = snprintf(dest, (size_t)max, "%s\\%s-%04u%02u%02u-%02u%02u%02u.log",
                 cartella, prefisso, (unsigned)ora->wYear,
                 (unsigned)ora->wMonth, (unsigned)ora->wDay,
                 (unsigned)ora->wHour, (unsigned)ora->wMinute,
                 (unsigned)ora->wSecond);
    if (n < 0 || n >= max) {
        /* Un nome troncato non e' un nome mancante: e' un nome DIVERSO, e
         * archivierebbe altrove o sopra qualcos'altro. */
        dest[0] = '\0';
        return 0;
    }
    return n;
}

/* Cancella i piu' vecchi finche' non ne restano al massimo `quanti`.
 *
 * Non tiene una lista: scandisce, trova il nome alfabeticamente MINIMO (che per
 * la forma del nome e' il piu' vecchio) e lo cancella, ripetendo. Con tetti da
 * una decina di file costa meno di ordinare un array, e soprattutto non ha un
 * limite implicito sul numero di file che sa gestire -- un array dimensionato a
 * mano ce l'avrebbe, e sarebbe il tipo di limite che nessuno ricorda. */
static void pota(const char *cartella, const char *prefisso, int quanti)
{
    for (;;) {
        char modello[MAX_PATH];
        char minimo[MAX_PATH];
        WIN32_FIND_DATAA t;
        HANDLE h;
        int trovati = 0;

        if (snprintf(modello, sizeof(modello), "%s\\%s-*.log", cartella,
                     prefisso) >= (int)sizeof(modello)) {
            return;
        }
        minimo[0] = '\0';
        h = FindFirstFileA(modello, &t);
        if (h == INVALID_HANDLE_VALUE) {
            return;
        }
        do {
            if (t.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            trovati++;
            if (!minimo[0] || strcmp(t.cFileName, minimo) < 0) {
                snprintf(minimo, sizeof(minimo), "%s", t.cFileName);
            }
        } while (FindNextFileA(h, &t));
        FindClose(h);

        if (trovati <= quanti || !minimo[0]) {
            return;
        }
        {
            char vittima[MAX_PATH];

            if (snprintf(vittima, sizeof(vittima), "%s\\%s", cartella, minimo)
                >= (int)sizeof(vittima)) {
                return;
            }
            if (!DeleteFileA(vittima)) {
                /* Se non si riesce a cancellare, fermarsi: insistere girerebbe
                 * per sempre sullo stesso file. */
                return;
            }
        }
    }
}

bool archivio_ruota(const char *percorso, const char *cartella,
                    const char *prefisso, int quanti, const SYSTEMTIME *ora)
{
    char nome[MAX_PATH];
    int tentativo;

    if (!percorso || !cartella || !prefisso || !ora) {
        return false;
    }
    if (GetFileAttributesA(percorso) == INVALID_FILE_ATTRIBUTES) {
        /* Primo avvio in assoluto: non c'e' niente da conservare. Non e' un
         * errore, e non si crea una cartella vuota per niente. */
        return false;
    }
    if (quanti <= 0) {
        /* "Non voglio archivi": si torna al comportamento di prima di questo
         * modulo -- il file attivo si cancella e non nasce nessuna cartella.
         * Serve per poter tornare indietro senza togliere il codice. */
        return DeleteFileA(percorso) ? true : false;
    }

    if (!CreateDirectoryA(cartella, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        /* Non si e' potuta creare la cartella: NON si perde l'avvio per questo.
         * Si cancella come si faceva prima, cioe' si perde la prova ma non la
         * sessione. Chi chiama puo' accorgersene dal valore ritornato. */
        DeleteFileA(percorso);
        return false;
    }

    /* Il nome ha la precisione del secondo e due avvii dentro lo stesso secondo
     * sono possibili -- il guscio riprova l'avvio fino a tre volte quando il
     * kernel si inchioda. MoveFileA fallisce se la destinazione esiste, e
     * MOVEFILE_REPLACE_EXISTING distruggerebbe proprio la prova che stiamo
     * conservando: quindi si aggiunge un suffisso. */
    if (archivio_nome(nome, sizeof(nome), cartella, prefisso, ora) == 0) {
        DeleteFileA(percorso);
        return false;
    }
    for (tentativo = 1; tentativo <= 9; tentativo++) {
        if (MoveFileA(percorso, nome)) {
            pota(cartella, prefisso, quanti);
            return true;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS &&
            GetLastError() != ERROR_FILE_EXISTS) {
            break;
        }
        if (snprintf(nome, sizeof(nome),
                     "%s\\%s-%04u%02u%02u-%02u%02u%02u-%d.log", cartella,
                     prefisso, (unsigned)ora->wYear, (unsigned)ora->wMonth,
                     (unsigned)ora->wDay, (unsigned)ora->wHour,
                     (unsigned)ora->wMinute, (unsigned)ora->wSecond,
                     tentativo + 1) >= (int)sizeof(nome)) {
            break;
        }
    }
    /* Non si e' potuto spostare: si cancella, perche' un file attivo che
     * sopravvive all'avvio e' la trappola del "boot_completed di due ore prima"
     * che questi due file evitano per costruzione. Perdere la prova e' meno
     * grave che leggere dati vecchi credendoli nuovi. */
    DeleteFileA(percorso);
    return false;
}
