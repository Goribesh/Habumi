/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-hyperv.c -- la DECISIONE, non il rilevamento.
 *
 * hyperv_presente() non si prova qui, ed e' una scelta: su questa macchina
 * l'hypervisor c'e', quindi risponderebbe sempre HYPERV_SI e la prova non
 * potrebbe fallire. Una prova che non puo' fallire non prova niente.
 *
 * Cio' che si prova e' hyperv_deve_fermare, che e' dove un errore farebbe
 * davvero danno: e' la funzione che decide se un computer si avvia o no. */
#include <stdio.h>
#include "guscio.h"

static int falliti;

#define CHECK(c) do { \
    if (!(c)) { printf("FALLITA riga %d: %s\n", __LINE__, #c); falliti++; } \
} while (0)

int main(void)
{
    int totali = 0;

    /* Solo NO ferma. */
    CHECK(hyperv_deve_fermare(HYPERV_NO) == true);   totali++;
    CHECK(hyperv_deve_fermare(HYPERV_SI) == false);  totali++;

    /* IL CASO CHE CONTA: "non sono riuscito a chiederlo" NON e' "non c'e'".
     * Se questa riga diventasse true, una versione di Windows che non
     * conosciamo, o un antivirus che blocca la LoadLibrary, impedirebbe l'avvio
     * a un computer che funziona benissimo -- e il messaggio direbbe alla
     * persona di accendere una funzionalita' che ha gia' acceso. */
    CHECK(hyperv_deve_fermare(HYPERV_NON_SO) == false); totali++;

    printf("test-hyperv: %d su %d passati\n", totali - falliti, totali);
    return falliti ? 1 : 0;
}
