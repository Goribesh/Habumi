/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* dati.c -- creare il /data vuoto quando manca.
 *
 * PERCHE' UN MODULO SUO: file.c parla di cosa si copia nel guest via adb (vedi
 * il commento in testa a quel file), non di estrarre un archivio locale con
 * tar.exe. Mescolare le due cose in un file che ha gia' un titolo diverso
 * confonderebbe il confine invece di renderlo chiaro.
 *
 * PERCHE' SERVE. Nessuno script del progetto genera data.img: l'initramfs fa
 * un mount secco su quel file e si ferma se fallisce, e finora si creava a
 * mano dalla WSL con mke2fs -- impossibile da pretendere da chi installa il
 * prodotto. Un ext4 vuoto da 4 GiB compresso pesa 4,2 MB (MISURATO, non
 * stimato: runtime/empty-data.zip), quindi si spedisce dentro il prodotto e
 * si estrae qui alla prima occasione in cui manca.
 *
 * ZIP E NON GZIP: Windows non ha un'API per decomprimere gzip, mentre
 * tar.exe -- che Windows 11 spedisce di serie -- legge gli zip, ed e' gia'
 * una dipendenza dichiarata per estrarre l'immagine di sistema. Una
 * dipendenza invece di due. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

int dati_comando_estrai(char *dest, int max, const char *archivio,
                        const char *cartella_temp)
{
    int n;

    if (!dest || max <= 0) {
        return 0;
    }
    dest[0] = '\0';
    if (!archivio || !archivio[0] || !cartella_temp || !cartella_temp[0]) {
        return 0;
    }
    n = snprintf(dest, (size_t)max, "tar.exe -xf \"%s\" -C \"%s\"",
                archivio, cartella_temp);
    if (n < 0 || n >= max) {
        /* Un comando troncato non e' un comando mancante: e' un comando
         * DIVERSO, ed estrarrebbe altrove o niente. */
        dest[0] = '\0';
        return 0;
    }
    return n;
}

int dati_cartella_temp(char *dest, int max, const char *percorso_dati)
{
    const char *sep_win;
    const char *sep_unix;
    const char *sep;
    size_t dir_len;
    int n;

    if (!dest || max <= 0) {
        return 0;
    }
    dest[0] = '\0';
    if (!percorso_dati || !percorso_dati[0]) {
        return 0;
    }

    /* L'ultimo separatore, che sia '\' o '/': la tabella di varianti.c scrive
     * i percorsi con '/' (var_percorsi), il resto del guscio con '\', e
     * questa funzione deve riconoscere entrambi gli stili invece di
     * spezzarsi su uno dei due. Nessun separatore vuol dire percorso_dati
     * gia' nella cartella corrente: dir_len resta 0 e la cartella temporanea
     * nasce li', che e' ancora "accanto" nel senso che conta qui. */
    sep_win = strrchr(percorso_dati, '\\');
    sep_unix = strrchr(percorso_dati, '/');
    sep = sep_win;
    if (sep_unix && (!sep || sep_unix > sep)) {
        sep = sep_unix;
    }
    dir_len = sep ? (size_t)(sep - percorso_dati) + 1 : 0;

    /* Nome fisso: nella cartella di percorso_dati vivono solo le immagini
     * delle varianti (system.img, data.img, system-gapps.img, data-gapps.img
     * -- la tabella intera e' in varianti.c), quindi non collide con nessuna
     * di esse ne' con l'altra variante che puo' condividere la stessa
     * cartella. */
    n = snprintf(dest, (size_t)max, "%.*sHabumiDatiTmp",
                (int)dir_len, percorso_dati);
    if (n < 0 || n >= max) {
        /* Stesso principio di dati_comando_estrai: un percorso troncato
         * estrarrebbe altrove, non e' un percorso mancante. */
        dest[0] = '\0';
        return 0;
    }
    return n;
}

/* Cancella cartella_temp e il suo unico contenuto possibile, "data.img".
 *
 * Va chiamata da OGNI uscita di dati_crea_se_manca successiva alla creazione
 * della cartella, successo o fallimento che sia: senza, un tentativo che
 * fallisce dopo l'estrazione (tar.exe che esce con errore a meta' copia,
 * MoveFileA che fallisce) abbandonerebbe fino a 4 GiB sul disco a ogni
 * riavvio che ci riprova. Meglio sforzo: se DeleteFileA o RemoveDirectoryA
 * falliscono (per esempio perche' non c'era niente da cancellare) non e' un
 * errore che deve cambiare l'esito gia' deciso dal chiamante. */
static void dati_pulisci_temp(const char *cartella_temp, const char *estratto)
{
    DeleteFileA(estratto);
    RemoveDirectoryA(cartella_temp);
}

/* Quanti tentativi IN PIU' concede dati_sposta_con_riprova a uno spostamento
 * dopo il primo, prima di arrendersi, e quanto aspetta prima di ciascuno:
 * CRESCENTE, ognuno il doppio del precedente. Vedi il commento su
 * dati_sposta_con_riprova in guscio.h per la misura sul prodotto vivo che
 * giustifica sia il numero sia le pause. */
#define DATI_SPOSTA_RIPROVE 5
static const DWORD DATI_SPOSTA_ATTESE_MS[DATI_SPOSTA_RIPROVE] = {
    200, 400, 800, 1600, 3200
};

bool dati_sposta_errore_e_transitorio(DWORD errore)
{
    return errore == ERROR_ACCESS_DENIED || errore == ERROR_SHARING_VIOLATION;
}

bool dati_sposta_con_riprova(const char *origine, const char *destinazione,
                             DWORD flag_move, const char *fase,
                             DWORD *ultimo_errore, int *tentativi_fatti)
{
    int tentativo;

    for (tentativo = 1; ; tentativo++) {
        if (MoveFileExA(origine, destinazione, flag_move)) {
            if (tentativo > 1) {
                registro_riga(REG_GUSCIO, "%s: move succeeded on "
                              "attempt %d of %d", fase, tentativo,
                              DATI_SPOSTA_RIPROVE + 1);
            }
            *tentativi_fatti = tentativo;
            return true;
        }
        *ultimo_errore = GetLastError();
        *tentativi_fatti = tentativo;
        if (!dati_sposta_errore_e_transitorio(*ultimo_errore) ||
            tentativo > DATI_SPOSTA_RIPROVE) {
            return false;
        }
        registro_riga(REG_GUSCIO, "%s: move failed on attempt %d "
                      "of %d (%lu), retrying in %lu ms", fase, tentativo,
                      DATI_SPOSTA_RIPROVE + 1, (unsigned long)*ultimo_errore,
                      (unsigned long)DATI_SPOSTA_ATTESE_MS[tentativo - 1]);
        Sleep(DATI_SPOSTA_ATTESE_MS[tentativo - 1]);
    }
}

bool dati_crea_se_manca(const char *archivio, const char *percorso_dati,
                        bool *creato, char *errore, size_t errore_n)
{
    char cartella_temp[MAX_PATH];
    char comando[2 * MAX_PATH + 32];
    char estratto[MAX_PATH];
    DWORD errore_dir;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD esito_processo;

    errore[0] = '\0';
    if (creato) {
        *creato = false;
    }

    /* Il caso comune, e quello di ogni riavvio dopo il primo: il file c'e'
     * gia' e non si tocca altro. */
    if (GetFileAttributesA(percorso_dati) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    /* SearchPathA invece di lanciare tar.exe alla cieca: cosi' l'ASSENZA si
     * distingue da ogni altro guasto e si puo' nominarla per nome, come
     * chiede il compito, invece di lasciare un errore di CreateProcessA che
     * non direbbe niente a chi non conosce Win32. */
    if (SearchPathA(NULL, "tar.exe", NULL, 0, NULL, NULL) == 0) {
        snprintf(errore, errore_n,
                "impossibile creare il /data: tar.exe non si trova. "
                "Windows 11 lo spedisce di serie in System32; su una "
                "macchina senza tar.exe %s non si puo' estrarre.", archivio);
        return false;
    }

    /* ACCANTO a percorso_dati, non sotto %TEMP%: vedi il commento su
     * dati_cartella_temp in guscio.h per il /data troncato e creduto buono
     * per sempre che questo evita quando la cartella del prodotto e %TEMP%
     * stanno su volumi diversi. */
    if (!dati_cartella_temp(cartella_temp, sizeof(cartella_temp),
                            percorso_dati)) {
        snprintf(errore, errore_n,
                "il percorso della cartella temporanea per %s e' troppo "
                "lungo", percorso_dati);
        return false;
    }

    /* tar.exe -xf non lascia scegliere il nome in uscita: l'archivio
     * contiene un solo file, "data.img", ed e' quello che serve spostare sul
     * percorso della variante. Costruito PRIMA di creare la cartella o
     * lanciare tar.exe, cosi' dati_pulisci_temp ha sempre un percorso valido
     * da provare a cancellare su ogni uscita successiva, riuscita o no. */
    if (snprintf(estratto, sizeof(estratto), "%s\\data.img", cartella_temp)
        >= (int)sizeof(estratto)) {
        snprintf(errore, errore_n,
                "il percorso del file estratto in %s e' troppo lungo",
                cartella_temp);
        return false;
    }

    /* Puo' esistere gia' da un tentativo precedente non riuscito (o dalla
     * pulizia sotto che non e' garantita, essendo a sua volta meglio
     * sforzo): ERROR_ALREADY_EXISTS va bene, tar.exe sovrascrive quello che
     * ci trova dentro. Ogni altro errore (permessi, percorso non valido) va
     * invece nominato QUI: senza questa distinzione il fallimento emerge solo
     * dopo, da un tar.exe che non trova la cartella e restituisce un codice
     * di uscita generico -- il registro finirebbe per parlare di "codice di
     * uscita" invece di dire la cosa vera, "non riesco a creare la cartella
     * temporanea". */
    if (!CreateDirectoryA(cartella_temp, NULL)) {
        errore_dir = GetLastError();
        if (errore_dir != ERROR_ALREADY_EXISTS) {
            snprintf(errore, errore_n,
                    "non riesco a creare la cartella temporanea %s (%lu)",
                    cartella_temp, errore_dir);
            return false;
        }
    }

    if (!dati_comando_estrai(comando, sizeof(comando), archivio,
                             cartella_temp)) {
        snprintf(errore, errore_n,
                "il comando di estrazione di %s non sta nel buffer",
                archivio);
        dati_pulisci_temp(cartella_temp, estratto);
        return false;
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    /* CREATE_NO_WINDOW: il guscio e' -mwindows, senza console propria, e
     * senza questo flag tar.exe ne aprirebbe una che lampeggia e sparisce. */
    if (!CreateProcessA(NULL, comando, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        snprintf(errore, errore_n,
                "impossibile avviare tar.exe per estrarre %s (%lu)",
                archivio, GetLastError());
        dati_pulisci_temp(cartella_temp, estratto);
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    esito_processo = 0;
    GetExitCodeProcess(pi.hProcess, &esito_processo);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (esito_processo != 0) {
        snprintf(errore, errore_n,
                "tar.exe e' uscito con codice %lu estraendo %s",
                esito_processo, archivio);
        dati_pulisci_temp(cartella_temp, estratto);
        return false;
    }

    {
        /* dati_sposta_con_riprova invece di una MoveFileA nuda: stesso rischio
         * di conflitto di condivisione transitorio (antivirus, indicizzatore)
         * dello spostamento dell'immagine di sistema in
         * variante_estrai_immagine (main.c), sullo stesso schema di riprova
         * -- qui il file e' il /data da 4 GiB invece dell'immagine da
         * 1,6-2,6 GB, ma il guasto misurato sul prodotto vivo (vedi il
         * commento su dati_sposta_con_riprova in guscio.h) non dipende da
         * quale file lo incontra. flag_move a 0, non
         * MOVEFILE_REPLACE_EXISTING: percorso_dati non deve esistere ancora,
         * per costruzione (questa funzione e' gia' uscita in testa se
         * GetFileAttributesA lo trova). */
        DWORD ultimo_errore = 0;
        int tentativi = 0;

        if (!dati_sposta_con_riprova(estratto, percorso_dati, 0, "/data",
                                     &ultimo_errore, &tentativi)) {
            snprintf(errore, errore_n,
                    "estratto %s ma non riesco a spostarlo su %s dopo %d "
                    "tentativi (%lu)", estratto, percorso_dati, tentativi,
                    (unsigned long)ultimo_errore);
            dati_pulisci_temp(cartella_temp, estratto);
            return false;
        }
    }
    /* Stesso volume per costruzione (dati_cartella_temp): questo spostamento
     * e' SEMPRE una rinomina atomica del filesystem, mai la copia-poi-cancella
     * fra volumi diversi che lascerebbe un data.img troncato in caso di
     * spegnimento a meta'. estratto non esiste piu' (e' stato spostato):
     * dati_pulisci_temp lo prova comunque, fallisce in silenzio su quello e
     * rimuove solo la cartella ormai vuota. */
    dati_pulisci_temp(cartella_temp, estratto);

    if (creato) {
        *creato = true;
    }
    return true;
}
