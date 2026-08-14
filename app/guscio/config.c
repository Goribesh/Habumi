/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* config.c -- il file di configurazione.
 *
 * PERCHE' NON FALLISCE MAI. Un file di configurazione che impedisce l'avvio e'
 * un file che nessuno modifica: al primo errore di battitura l'utente torna
 * allo script e non ci riprova. Si parte dai default, si applica cio' che si
 * riesce a leggere, e ogni riga ignorata finisce nel registro CON LA RAGIONE --
 * "porta_adb=5555 ignorata: 5554-5585 e' l'intervallo che adb esplora per
 * scoprire gli emulatori", non "valore non valido". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guscio.h"

void config_default(Config *c)
{
    c->vcpu = 6;
    c->memoria = 6144;
    c->larghezza = 1280;
    c->altezza = 800;
    c->riprove = 3;
    c->porta_adb = 15555;
    /* Una porta sola sopra quella di adb, e comunque lontana da 5554..5585: e'
     * la porta del canale degli appunti condivisi, che il guscio ascolta su
     * 127.0.0.1 e l'app del guest chiama come 10.0.2.2. */
    c->porta_appunti = 15556;
    strcpy(c->gl, "wgl");
    /* Senza attesa, e il perche' sta sul campo in guscio.h:
     * 257 us per commutazione con l'attesa contro 26-29 senza, misurati. */
    c->gl_attesa_flush = false;
    /* Acceso : il kernel del guest ha virtio-snd builtin, quindi
     * /dev/snd esiste e l'HAL lo apre. Prima era spento perche' il dispositivo
     * non aveva nessuno che lo aprisse. */
    c->audio = true;
    c->scala_guest = false;
    c->hz = 0;
    c->densita = 0;
    c->variante = VAR_VANILLA;
}

/* Toglie spazi in testa e in coda, in posto. */
static char *pota(char *s)
{
    char *fine;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    fine = s + strlen(s);
    while (fine > s && (fine[-1] == ' ' || fine[-1] == '\t' ||
                        fine[-1] == '\r' || fine[-1] == '\n')) {
        fine--;
    }
    *fine = '\0';
    return s;
}

/* Esito della sola interpretazione (parse + controllo dell'intervallo), senza
 * scrivere nel registro: intero() e intero_o_zero() vogliono messaggi diversi
 * per lo stesso guasto (il secondo deve nominare lo zero speciale), quindi il
 * messaggio resta a ciascun chiamante. Cio' che serve condiviso -- strtol, il
 * controllo del resto, il confronto con l'intervallo -- vive qui una volta
 * sola: prima intero_o_zero copiava queste tre righe, e un domani che
 * cambiasse solo intero() (per esempio un controllo di overflow) non si
 * sarebbe propagato all'altra senza che nessuno se ne accorgesse. */
typedef enum {
    INTERO_OK,
    INTERO_NON_NUMERICO,
    INTERO_FUORI_INTERVALLO
} EsitoIntero;

static EsitoIntero interpreta_intero(const char *v, int min, int max, long *n)
{
    char *resto = NULL;

    *n = strtol(v, &resto, 10);
    if (resto == v || (resto && *resto)) {
        return INTERO_NON_NUMERICO;
    }
    if (*n < min || *n > max) {
        return INTERO_FUORI_INTERVALLO;
    }
    return INTERO_OK;
}

static bool intero(const char *v, int min, int max, int *dove,
                   const char *chiave)
{
    long n;

    switch (interpreta_intero(v, min, max, &n)) {
    case INTERO_NON_NUMERICO:
        registro_riga(REG_GUSCIO, "config: %s=%s ignored, not a "
                                  "number. Staying at %d.", chiave, v, *dove);
        return false;
    case INTERO_FUORI_INTERVALLO:
        registro_riga(REG_GUSCIO, "config: %s=%ld ignored, outside "
                                  "the range %d..%d. Staying at %d.",
                      chiave, n, min, max, *dove);
        return false;
    case INTERO_OK:
    default:
        *dove = (int)n;
        return true;
    }
}

/* Come intero(), ma per chiavi con un valore speciale (0) fuori dall'intervallo
 * pieno: hz (0 = automatico, o 24..240) e densita (0 = non toccare, o
 * 120..640) hanno la stessa forma. Delega a interpreta_intero() la parte che
 * condivide con intero() (parse e controllo dell'intervallo): la sola cosa
 * propria di questa funzione e' trattare lo zero come un valore a se', e dirlo
 * nel messaggio di rifiuto. */
static bool intero_o_zero(const char *v, int min, int max, int *dove,
                          const char *chiave, const char *zero_significa)
{
    long n;
    EsitoIntero esito = interpreta_intero(v, min, max, &n);

    if (esito == INTERO_NON_NUMERICO) {
        registro_riga(REG_GUSCIO, "config: %s=%s ignored, not a "
                                  "number. Staying at %d.", chiave, v, *dove);
        return false;
    }
    /* Lo zero e' un valore valido a se', qualunque cosa dica l'intervallo
     * pieno (che parte da un minimo positivo): va riconosciuto PRIMA di
     * guardare INTERO_FUORI_INTERVALLO, o un min > 0 lo rifiuterebbe. */
    if (n == 0) {
        *dove = 0;
        return true;
    }
    if (esito == INTERO_FUORI_INTERVALLO) {
        registro_riga(REG_GUSCIO, "config: %s=%ld ignored, outside "
                                  "either 0 (%s) or the range %d..%d. "
                                  "Staying at %d.", chiave, n, zero_significa, min,
                      max, *dove);
        return false;
    }
    *dove = (int)n;
    return true;
}

void config_carica(Config *c, const char *percorso)
{
    FILE *f = fopen(percorso, "rb");
    char riga[512];
    int numero = 0;
    /* PORTA_APPUNTI NON SI APPLICA DENTRO IL CICLO, e non e' una complicazione
     * gratuita: uno dei suoi tre rifiuti e' "uguale a porta_adb", e dentro il
     * ciclo il confronto userebbe il porta_adb del momento -- cioe' l'esito
     * dipenderebbe dall'ORDINE delle due righe nel file. Con
     *     porta_appunti=16000
     *     porta_adb=16000
     * il controllo immediato avrebbe accettato entrambe (a quel punto porta_adb
     * vale ancora 15555) e le due porte sarebbero finite uguali senza che
     * nessuno lo dicesse: il server degli appunti non si sarebbe messo in
     * ascolto, e la ragione sarebbe stata un WSAEADDRINUSE che non nomina la
     * causa. Si tiene da parte il valore letto e si decide dopo il ciclo, quando
     * porta_adb e' definitivo. -1 = la chiave non c'era. */
    int appunti_letto = -1;

    if (!f) {
        registro_riga(REG_GUSCIO, "config: %s is not there, using the "
                                  "defaults.", percorso);
        return;
    }

    while (fgets(riga, sizeof(riga), f)) {
        char *chiave, *valore, *uguale;

        numero++;
        chiave = pota(riga);
        if (!*chiave || *chiave == '#') {
            continue;
        }
        uguale = strchr(chiave, '=');
        if (!uguale) {
            registro_riga(REG_GUSCIO, "config: line %d ignored, missing "
                                      "the equals sign: %s", numero, chiave);
            continue;
        }
        *uguale = '\0';
        chiave = pota(chiave);
        valore = pota(uguale + 1);

        if (!strcmp(chiave, "vcpu")) {
            intero(valore, 1, 64, &c->vcpu, "vcpu");
        } else if (!strcmp(chiave, "memory")) {
            intero(valore, 1024, 65536, &c->memoria, "memoria");
        } else if (!strcmp(chiave, "width")) {
            intero(valore, 320, 8192, &c->larghezza, "larghezza");
        } else if (!strcmp(chiave, "height")) {
            intero(valore, 320, 8192, &c->altezza, "altezza");
        } else if (!strcmp(chiave, "retries")) {
            intero(valore, 1, 10, &c->riprove, "riprove");
        } else if (!strcmp(chiave, "adb_port")) {
            int p = c->porta_adb;

            if (intero(valore, 1024, 65535, &p, "porta_adb")) {
                /* MISURATO: adb esplora 5554-5585 in coppie per scoprire gli
                 * emulatori. Con la nostra porta in quell'intervallo adb la
                 * prende per un emulatore, inventa un'entrata "emulator-5554" e
                 * lascia OFFLINE sia quella sia la nostra -- con adbd che nel
                 * guest gira regolarmente. Il guasto e' intermittente perche'
                 * dipende dai tempi: ha funzionato per ore e poi ha smesso. */
                if (p >= 5554 && p <= 5585) {
                    registro_riga(REG_GUSCIO, "config: adb_port=%d "
                                  "ignored -- 5554-5585 is the range adb "
                                  "scans to discover emulators, and it "
                                  "would leave everything offline. Staying at %d.",
                                  p, c->porta_adb);
                } else {
                    c->porta_adb = p;
                }
            }
        } else if (!strcmp(chiave, "clipboard_port")) {
            int p = c->porta_appunti;

            /* Qui si controlla SOLO l'intervallo generale: gli altri due
             * rifiuti stanno dopo il ciclo, vedi la dichiarazione di
             * appunti_letto per il perche'. */
            if (intero(valore, 1024, 65535, &p, "porta_appunti")) {
                appunti_letto = p;
            }
        } else if (!strcmp(chiave, "gl")) {
            if (!strcmp(valore, "wgl") || !strcmp(valore, "angle")) {
                strcpy(c->gl, valore);
            } else {
                registro_riga(REG_GUSCIO, "config: gl=%s ignored, the "
                              "values are wgl or angle. Staying at %s.",
                              valore, c->gl);
            }
        } else if (!strcmp(chiave, "audio")) {
            if (!strcmp(valore, "on")) {
                c->audio = true;
            } else if (!strcmp(valore, "off")) {
                c->audio = false;
            } else {
                registro_riga(REG_GUSCIO, "config: audio=%s ignored, the "
                              "values are on or off. Staying at %s.",
                              valore, c->audio ? "on" : "off");
            }
        } else if (!strcmp(chiave, "gl_flush_wait")) {
            if (!strcmp(valore, "yes")) {
                c->gl_attesa_flush = true;
            } else if (!strcmp(valore, "no")) {
                c->gl_attesa_flush = false;
            } else {
                registro_riga(REG_GUSCIO, "config: gl_flush_wait=%s "
                              "ignored, the values are yes or no. Staying at %s.",
                              valore, c->gl_attesa_flush ? "yes" : "no");
            }
        } else if (!strcmp(chiave, "guest_scale")) {
            if (!strcmp(valore, "yes")) {
                c->scala_guest = true;
            } else if (!strcmp(valore, "no")) {
                c->scala_guest = false;
            } else {
                registro_riga(REG_GUSCIO, "config: guest_scale=%s "
                              "ignored, the values are yes or no. Staying at %s.",
                              valore, c->scala_guest ? "yes" : "no");
            }
        } else if (!strcmp(chiave, "variant")) {
            VarNome v;
            if (var_da_testo(valore, &v)) {
                c->variante = v;
            } else {
                /* Non e' fatale: si avvia la vanilla e lo si dice. Fermare
                 * l'emulatore per una parola sbagliata in un file di
                 * configurazione sarebbe peggio del difetto. */
                registro_riga(REG_GUSCIO,
                              "config: unknown variant \"%s\", "
                              "using vanilla", valore);
            }
        } else if (!strcmp(chiave, "hz")) {
            intero_o_zero(valore, 24, 240, &c->hz, "hz", "automatico");
        } else if (!strcmp(chiave, "density")) {
            intero_o_zero(valore, 120, 640, &c->densita, "densita",
                          "non tocca la densita' del guest");
        } else {
            registro_riga(REG_GUSCIO, "config: unknown key '%s' "
                                      "on line %d, ignored.", chiave, numero);
        }
    }
    fclose(f);

    /* --- i due rifiuti di porta_appunti che dipendono dal resto del file --- */
    if (appunti_letto >= 0) {
        if (appunti_letto >= 5554 && appunti_letto <= 5585) {
            /* Lo stesso intervallo vietato a porta_adb, e per lo stesso motivo
             * MISURATO: adb lo esplora in coppie per scoprire gli emulatori.
             * Non e' la nostra porta di adb, ma e' comunque una porta che adb
             * andra' a sondare, e una connessione di adb sul canale degli
             * appunti manderebbe byte che non sono trame -- il server
             * chiuderebbe e riaprirebbe senza che nulla dica perche'. */
            registro_riga(REG_GUSCIO, "config: clipboard_port=%d "
                          "ignored -- 5554-5585 is the range adb scans "
                          "to discover emulators (see adb_port). "
                          "Staying at %d.", appunti_letto, c->porta_appunti);
        } else if (appunti_letto == c->porta_adb) {
            registro_riga(REG_GUSCIO, "config: clipboard_port=%d "
                          "ignored, it is the same as adb_port: two servers "
                          "cannot listen on one port. "
                          "Staying at %d.", appunti_letto, c->porta_appunti);
        } else {
            c->porta_appunti = appunti_letto;
        }
    }
    /* Ci si arriva in un modo solo: dando a porta_adb il valore che porta_appunti
     * ha per default, senza scrivere porta_appunti. Nessun valore e' stato
     * rifiutato -- sono validi entrambi -- ma il server degli appunti non
     * riuscira' a legarsi a una porta che QEMU sta gia' usando, e senza questa
     * riga la sola traccia sarebbe un errore di bind che non nomina la causa. */
    if (c->porta_appunti == c->porta_adb) {
        registro_riga(REG_GUSCIO, "config: adb_port and clipboard_port "
                      "are the same (%d): the clipboard server will not be "
                      "able to listen. Give clipboard_port a different "
                      "value.", c->porta_appunti);
    }
}
