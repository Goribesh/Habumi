/* SPDX-License-Identifier: GPL-2.0-only */
/* Ordered Android controls. All state belongs to the shell's UI thread. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "guscio.h"

#define COMANDI_CAPACITA 16
typedef struct { ComandoUi tipo; int valore, porta; } RichiestaUi;
static RichiestaUi coda[COMANDI_CAPACITA], attuale;
static int testa, quanti, fase, larghezza, altezza, rotazione;
static AdbComando *cliente;
static bool attiva, annullando;
static ULONGLONG iniziata;
static const char *nomi[] = {"power", "volume up", "volume down", "home",
                            "back", "recent apps", "rotation", "density"};
static const char *tasti[] = {"KEYCODE_POWER", "KEYCODE_VOLUME_UP", "KEYCODE_VOLUME_DOWN",
                             "KEYCODE_HOME", "KEYCODE_BACK", "KEYCODE_APP_SWITCH"};

bool comandi_in_corso(void) { return attiva || quanti || annullando; }

void comandi_pulizia(void)
{
    if (annullando) { comandi_annulla(); }
}

bool comandi_annulla(void)
{
    quanti = 0;
    testa = 0;
    annullando = true;
    if (!adb_comando_chiudi(cliente)) { return false; }
    cliente = NULL;
    attiva = annullando = false;
    return true;
}

bool comandi_accoda(const Config *c, ComandoUi tipo, int valore)
{
    if (!c || tipo < COMANDO_POWER || tipo > COMANDO_DENSITA ||
        (tipo == COMANDO_DENSITA && (valore < 120 || valore > 640))) { return false; }
    if (annullando || quanti == COMANDI_CAPACITA) {
        registro_riga(REG_GUSCIO, "controls: %s refused; queue is full or still stopping", nomi[tipo]);
        return false;
    }
    coda[(testa + quanti) % COMANDI_CAPACITA] = (RichiestaUi){tipo, valore, c->porta_adb};
    quanti++;
    return true;
}

static void fallisci(const char *motivo)
{
    registro_riga(REG_GUSCIO, "controls: %s failed (%s); pending controls discarded", nomi[attuale.tipo], motivo);
    comandi_annulla();
}

static bool lancia(void)
{
    Config config = {0};
    char comando[128];
    config.porta_adb = attuale.porta;
    if (attuale.tipo <= COMANDO_RECENTI) {
        snprintf(comando, sizeof(comando), "input keyevent %s", tasti[attuale.tipo]);
    } else if (attuale.tipo == COMANDO_DENSITA) {
        snprintf(comando, sizeof(comando), "wm density %d", attuale.valore);
    } else {
        switch (fase) {
        case 0: strcpy(comando, "wm size"); break;
        case 1: strcpy(comando, "settings get system user_rotation"); break;
        case 2: strcpy(comando, "settings put system accelerometer_rotation 0"); break;
        default: snprintf(comando, sizeof(comando), "settings put system user_rotation %d", rotazione ? 0 : 1); break;
        }
    }
    cliente = adb_comando_avvia(&config, comando);
    return cliente != NULL;
}

static bool leggi_rotazione(const char *testo, int *valore)
{
    while (*testo && strchr(" \r\n\t", *testo)) { testo++; }
    char *fine;
    if (!strncmp(testo, "null", 4)) { *valore = 0; fine = (char *)testo + 4; }
    else {
        long n = strtol(testo, &fine, 10);
        if (fine == testo || n < 0 || n > 3) { return false; }
        *valore = (int)n;
    }
    while (*fine && strchr(" \r\n\t", *fine)) { fine++; }
    return !*fine;
}

void comandi_passo(void)
{
    if (annullando) { comandi_annulla(); return; }
    if (!attiva) {
        if (!quanti) { return; }
        attuale = coda[testa]; testa = (testa + 1) % COMANDI_CAPACITA; quanti--;
        attiva = true; fase = 0; iniziata = GetTickCount64();
        if (!lancia()) { fallisci("ADB client could not start"); }
        return;
    }
    if (GetTickCount64() - iniziata >= 20000) { fallisci("20 s deadline"); return; }
    char uscita[256] = {0};
    bool troncata = false;
    AdbSpegniStato stato = adb_comando_passo(cliente, uscita, sizeof(uscita), &troncata);
    if (stato == ADB_SPEGNI_ATTESA) { return; }
    if (stato != ADB_SPEGNI_OK || troncata) { fallisci("ADB error or incomplete output"); return; }
    if (!adb_comando_chiudi(cliente)) { annullando = true; quanti = 0; return; }
    cliente = NULL;
    if (attuale.tipo == COMANDO_RUOTA) {
        if (fase == 0) {
            const char *p = strchr(uscita, ':');
            char *fine;
            long w = p ? strtol(p + 1, &fine, 10) : 0;
            if (!p || fine == p + 1 || *fine != 'x' || w < 320 || w > 8192) {
                fallisci("invalid guest resolution"); return;
            }
            const char *inizio_h = fine + 1;
            long h = strtol(inizio_h, &fine, 10);
            if (fine == inizio_h || h < 320 || h > 8192 ||
                (*fine && !strchr(" \t\r\n", *fine))) { fallisci("invalid guest resolution"); return; }
            larghezza = (int)w; altezza = (int)h;
        } else if (fase == 1) {
            if (!leggi_rotazione(uscita, &rotazione)) { fallisci("invalid rotation state"); return; }
            int w, h;
            dpi_ruota(larghezza, altezza, rotazione != 0, &w, &h);
            /* Preserve the existing framebuffer-before-Android-settings order. */
            if (!tubo_risoluzione(w, h)) {
                registro_riga(REG_GUSCIO, "rotation: command pipe did not deliver the framebuffer; applying Android rotation fallback");
            }
        }
        if (fase < 3) {
            fase++;
            if (!lancia()) { fallisci("ADB client could not start"); }
            return;
        }
        registro_riga(REG_GUSCIO, "rotation: applied user_rotation=%d", rotazione ? 0 : 1);
    } else if (attuale.tipo == COMANDO_DENSITA) {
        registro_riga(REG_GUSCIO, "density: applied %d (asynchronous)", attuale.valore);
    } else {
        registro_riga(REG_GUSCIO, "controls: %s delivered", nomi[attuale.tipo]);
    }
    attiva = false;
    /* Dispatch at most one next command; no unbounded work in a UI callback. */
    if (quanti) { comandi_passo(); }
}
