/* SPDX-License-Identifier: GPL-2.0-only */
/* Cadenza della rete di sicurezza: l'input conserva la sveglia immediata. */
#ifndef WINQ_RISPARMIO_H
#define WINQ_RISPARMIO_H

#include <stdint.h>

#define WINQ_RIPOSO_MS 30
#define WINQ_ATTIVITA_MS 250

static inline int winq_risparmio_periodo(int64_t ora, int64_t ultima,
                                       int attivo, int adattivo)
{
    if (!adattivo || ora < ultima || ora - ultima < WINQ_ATTIVITA_MS) {
        return attivo;
    }
    return WINQ_RIPOSO_MS;
}

#endif
