/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef IMMAGINE_STATO_H
#define IMMAGINE_STATO_H

#include <stddef.h>
#include <string.h>

#include "scarica.h"
#include "varianti.h"

/* Produce una copia della voce del manifesto con l'impronta dell'immagine
 * estratta. Nome e byte restano quelli dell'archivio ZIP; *fuori cambia solo
 * dopo che l'immagine e' stata letta e hashata con successo. */
static inline bool immagine_stato_voce(const VarVoce *archivio,
                                       const char *percorso_immagine,
                                       VarVoce *fuori,
                                       char *errore, size_t errore_n)
{
    VarVoce copia;
    char sha_immagine[VAR_SHA256_CIFRE + 1];

    if (!archivio || !percorso_immagine || !fuori || !errore || !errore_n) {
        return false;
    }
    if (!scarica_sha256_file(percorso_immagine, sha_immagine,
                             errore, errore_n)) {
        return false;
    }
    copia = *archivio;
    memcpy(copia.sha256, sha_immagine, sizeof(sha_immagine));
    *fuori = copia;
    return true;
}

#endif /* IMMAGINE_STATO_H */
