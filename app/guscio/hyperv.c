/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* hyperv.c -- c'e' l'hypervisor di Windows?
 *
 * IL PROBLEMA CHE RISOLVE. QEMU gira con -accel whpx. Se sulla macchina la
 * funzionalita' "Piattaforma hypervisor Windows" non e' attiva, QEMU esce
 * SUBITO, tre volte di fila, e l'avvio fallisce. Chi ha appena scompattato il
 * pacchetto si trova davanti a un errore di QEMU e deve INDOVINARE che il
 * rimedio e' una funzionalita' di Windows: non c'e' nessuna riga che la nomini.
 *
 * PERCHE' NON SI CHIEDE A QEMU. Lanciarlo con una macchina vuota e leggere
 * l'errore proverebbe esattamente cio' che poi accadra', ed e' una virtu' vera.
 * Costa pero' un processo a ogni avvio, e soprattutto DEDUCE DA UNA STRINGA cio'
 * che qui si chiede a una funzione documentata: il testo dei messaggi di QEMU
 * cambia fra le versioni, il codice della capability no.
 *
 * PERCHE' NON Get-WindowsOptionalFeature: misurato, pretende privilegi
 * elevati. Un prodotto che promette di non chiedere l'amministratore non puo'
 * usarla nemmeno per controllare.
 *
 * PERCHE' LA DLL SI CARICA A RUNTIME E NON SI COLLEGA. Su una macchina dove la
 * funzionalita' non e' installata WinHvPlatform.dll PUO' NON ESISTERE, e un
 * collegamento statico impedirebbe al programma di partire del tutto -- cioe'
 * proprio nel caso che questo file esiste per spiegare. Il caricamento a runtime
 * trasforma quell'assenza in una risposta invece che in un crash.
 *
 * MISURATO su questa macchina, con una sonda buttata via:
 *     LoadLibraryA(WinHvPlatform.dll) = 00007FFADF9C0000
 *     GetProcAddress(WHvGetCapability) = 00007FFADF9E6D80
 *     hr = 0x00000000  scritti = 4  presente = 1
 * Verificato anche che WinHvEmulation.dll NON c'e' mentre WinHvPlatform.dll si':
 * cercare la seconda darebbe un falso negativo su un computer che funziona. */
#include <stdio.h>
#include <stdlib.h>   /* getenv, per la scorciatoia di prova */
#include <string.h>   /* strcmp, idem */
#include <windows.h>
#include "guscio.h"

/* I tipi di WinHvPlatform.h non si includono: quell'intestazione puo' mancare
 * nel toolchain, e qui serve UN campo di UNA struttura.
 * WHvCapabilityCodeHypervisorPresent vale 0. */
#define HV_CAP_PRESENTE 0x00000000u

typedef HRESULT (WINAPI *HvGetCap)(UINT32, void *, UINT32, UINT32 *);

/* La regola intera, in una riga, e separata dal rilevamento apposta: e' l'unica
 * parte provabile senza una macchina senza hypervisor, ed e' quella dove un
 * errore farebbe danno. Vedi test-hyperv.c. */
bool hyperv_deve_fermare(HypervEsito e)
{
    return e == HYPERV_NO;
}

HypervEsito hyperv_presente(void)
{
    HMODULE dll;
    HvGetCap get_cap;
    UINT32 presente = 0;
    UINT32 scritti = 0;
    HRESULT hr;

    /* UNA SCORCIATOIA PER PROVARE, e sta nel prodotto di proposito.
     *
     * Il percorso "hypervisor assente" non e' percorribile su questa macchina:
     * qui c'e'. Senza questa variabile l'unica alternativa per provare il
     * messaggio sarebbe spegnere la funzionalita' di Windows e riavviare, cioe'
     * rendere inutilizzabile il computer di sviluppo per verificare un testo.
     *
     * NON e' una porta di servizio: risponde solo a due valori esatti, non fa
     * niente piu' che restituire un esito, non tocca nessun file, e chi la trova
     * leggendo il codice legge anche perche' c'e'. Il caso peggiore in cui
     * qualcuno la imposti e' che l'emulatore si rifiuti di partire dicendo il
     * perche'.
     *
     * Resta nel prodotto e non dietro un #ifdef perche' serve proprio sul
     * binario che si spedisce: e' li' che si vuole verificare cosa vede
     * l'utente, non su una compilazione diversa. */
    {
        const char *finto = getenv("GUSCIO_HYPERV_FINTO");

        if (finto && !strcmp(finto, "no")) {
            return HYPERV_NO;
        }
        if (finto && !strcmp(finto, "nonso")) {
            return HYPERV_NON_SO;
        }
    }

    /* La DLL assente E' la risposta: la funzionalita' non e' installata. */
    dll = LoadLibraryA("WinHvPlatform.dll");
    if (!dll) {
        return HYPERV_NO;
    }

    get_cap = (HvGetCap)(void *)GetProcAddress(dll, "WHvGetCapability");
    if (!get_cap) {
        /* La DLL c'e' ma non ha la funzione: e' una situazione che non sappiamo
         * leggere, non una risposta negativa. Non si ferma nessuno per questo. */
        FreeLibrary(dll);
        return HYPERV_NON_SO;
    }

    hr = get_cap(HV_CAP_PRESENTE, &presente, (UINT32)sizeof(presente), &scritti);
    FreeLibrary(dll);

    /* Si controlla anche quanti byte ha scritto: una chiamata che riesce ma
     * riempie meno di quanto ci si aspetta lascerebbe "presente" al valore
     * iniziale, cioe' 0, e verrebbe letta come "hypervisor assente". Sarebbe un
     * falso negativo che ferma una macchina sana. */
    if (FAILED(hr) || scritti != sizeof(presente)) {
        return HYPERV_NON_SO;
    }
    return presente ? HYPERV_SI : HYPERV_NO;
}
