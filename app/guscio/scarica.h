/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* scarica.h -- la rete e le impronte. SOTTILE DI PROPOSITO: qui sta solo cio'
 * che deve toccare Windows, mentre ogni decisione sta in varianti.c, che si
 * prova senza rete.
 *
 * WinHTTP e BCrypt sono dentro Windows: nessuna dipendenza nuova, HTTPS e
 * verifica del certificato inclusi. */
#ifndef SCARICA_H
#define SCARICA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Chiamata mentre il file scende. Ritornare false ANNULLA lo scaricamento. */
typedef bool (*ScaricaAvanzamento)(uint64_t fatti, uint64_t totali, void *dato);

bool scarica_testo(const char *url, char *fuori, size_t fuori_n,
                   char *errore, size_t errore_n);

/* Scarica su "<destinazione>.parziale" e RINOMINA solo dopo che lo sha256
 * corrisponde: un file a meta' non deve mai poter sembrare un'immagine buona.
 * Se il parziale esiste gia', riprende con Range invece di ricominciare. */
bool scarica_file(const char *url, const char *destinazione,
                  const char *sha256_atteso, uint64_t byte_attesi,
                  ScaricaAvanzamento avanti, void *dato,
                  char *errore, size_t errore_n);

bool scarica_sha256_file(const char *percorso, char *fuori65,
                         char *errore, size_t errore_n);

bool scarica_spazio_libero(const char *cartella, uint64_t *byte);
#endif
