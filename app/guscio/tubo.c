/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* tubo.c -- il canale dal guscio a QEMU.
 *
 * Quattro comandi: la risoluzione (per la rotazione) e i tre della mappa dei
 * tasti (carica, spegni, impara). Tutti condividono la stessa apertura del
 * tubo e la stessa diagnosi di errore -- tubo_apri e tubo_scrivi, sotto --
 * perche' quella logica e' identica per ognuno e non si e' voluta scritta
 * quattro volte: la stessa lezione di "non crearne una terza copia" che vale
 * per guscio_percorso_accanto (vedi guscio.h) vale anche qui. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

#define TUBO_NOME "\\\\.\\pipe\\AndroidRuntimeGuscio.comandi"

/* Apre l'unica istanza del tubo dei comandi, diagnosticando la causa
 * GIUSTA se fallisce: ERROR_FILE_NOT_FOUND (2) vuol dire che il tubo non
 * esiste proprio -- QEMU e' quello vecchio, senza il tubo. ERROR_PIPE_BUSY
 * (231) vuol dire l'opposto: il tubo esiste, ma la sua unica istanza e'
 * occupata (per esempio perche' il lato QEMU non l'ha ancora riarmata dopo
 * il comando precedente) -- QEMU non c'entra affatto. Un messaggio che
 * nominasse sempre la stessa causa manderebbe chi indaga nella direzione
 * sbagliata, ed e' peggio di un messaggio generico. */
static HANDLE tubo_apri(void)
{
    HANDLE t = CreateFileA(TUBO_NOME, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           0, NULL);

    if (t == INVALID_HANDLE_VALUE) {
        DWORD errore = GetLastError();

        if (errore == ERROR_FILE_NOT_FOUND) {
            registro_riga(REG_GUSCIO, "the command pipe does not exist (2): "
                          "is this the old QEMU, without the pipe?");
        } else if (errore == ERROR_PIPE_BUSY) {
            registro_riga(REG_GUSCIO, "the command pipe is busy (231): "
                          "the QEMU-side instance has not re-armed yet, "
                          "so this is not the old QEMU");
        } else {
            registro_riga(REG_GUSCIO, "the command pipe is not answering, "
                          "Windows error %lu", errore);
        }
    }
    return t;
}

/* Apre, scrive n byte di comando e chiude. true solo se TUTTI i byte sono
 * arrivati alla pipe. La diagnosi del fallimento in apertura e' gia' nel
 * registro (tubo_apri, sopra): questa funzione non ne aggiunge una
 * seconda, coerente col fatto che ogni comando la condivide. */
static bool tubo_scrivi(const char *comando, int n)
{
    HANDLE t = tubo_apri();
    DWORD scritti = 0;

    if (t == INVALID_HANDLE_VALUE) {
        return false;
    }
    WriteFile(t, comando, (DWORD)n, &scritti, NULL);
    CloseHandle(t);
    return scritti == (DWORD)n;
}

bool tubo_risoluzione(int w, int h)
{
    char comando[64];
    int n = snprintf(comando, sizeof(comando), "risoluzione %d %d\n", w, h);

    return tubo_scrivi(comando, n);
}

bool tubo_mappa_carica(const char *percorso)
{
    char comando[MAX_PATH + 32];
    int n = snprintf(comando, sizeof(comando), "mappa carica %s\n", percorso);

    return tubo_scrivi(comando, n);
}

bool tubo_mappa_spegni(void)
{
    static const char comando[] = "mappa spegni\n";

    return tubo_scrivi(comando, (int)strlen(comando));
}

bool tubo_mappa_impara(const char *tipo, const char *percorso)
{
    char comando[MAX_PATH + 32];
    int n = snprintf(comando, sizeof(comando), "mappa impara %s %s\n", tipo,
                     percorso);

    return tubo_scrivi(comando, n);
}
