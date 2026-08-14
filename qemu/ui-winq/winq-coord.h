/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-coord.h
 * Mappatura pura finestra -> guest per il backend -display winq.
 *
 * Deliberatamente senza dipendenze da QEMU o da Windows: e' l'unico pezzo
 * di questo lavoro testabile in isolamento, senza VM e senza GPU, e deve
 * restare tale. */
#ifndef WINQ_COORD_H
#define WINQ_COORD_H
#include <stdbool.h>

typedef struct {
    int win_w, win_h;      /* dimensioni utili della finestra, in pixel */
    int guest_w, guest_h;  /* spazio logico del guest */
} WinqGeom;

/* Rettangolo occupato dall'immagine del guest dentro la finestra, in pixel:
 * origine (*x, *y) e dimensioni (*w, *h), con le bande nere che restano fuori.
 * Ritorna false se la geometria non e' ancora usabile (finestra minimizzata o
 * guest senza dimensioni), e in quel caso non tocca nulla.
 *
 * Esiste per una ragione precisa: il disegno (winq-present.c) e il tocco
 * (winq_win_to_guest, qui sotto) devono usare LO STESSO rettangolo, altrimenti
 * l'immagine e il punto toccato divergono di qualche pixel e il sintomo -- un
 * tocco che cade accanto a dove si e' premuto -- si attribuisce ad Android
 * invece che a due formule scritte due volte. Una formula sola, due chiamanti. */
bool winq_guest_rect(const WinqGeom *g, double *x, double *y,
                     double *w, double *h);

/* Converte un punto della finestra nello spazio del guest.
 * Ritorna true se il punto e' dentro l'immagine, false se cade nelle bande.
 * In caso di false, *gx e *gy non vengono toccati. */
bool winq_win_to_guest(const WinqGeom *g, int wx, int wy, int *gx, int *gy);

#endif
