/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* winq-rotella.h -- da delta di Windows a scatti di rotella.
 *
 * PERCHE' UN FILE A PARTE e non una funzione dentro winq-input.c: quel file
 * include windows.h e mezzo QEMU, quindi non si compila da solo, quindi la sua
 * logica non si prova senza avviare una VM -- e una VM costa due minuti per
 * ogni tentativo. Questo file non include niente, quindi si prova in un
 * secondo. E' la stessa ragione per cui esiste winq-coord.c.
 */
#ifndef WINQ_ROTELLA_H
#define WINQ_ROTELLA_H

/* Converte il delta di WM_MOUSEWHEEL in scatti interi con segno, tenendo in
 * *resto quello che non basta a farne uno.
 *
 * PERCHE' SERVE UN RESTO: i touchpad di precisione mandano delta MINORI di 120
 * (WHEEL_DELTA), spesso 8 o 12 per movimento del dito. Scartarli farebbe
 * sembrare la rotella rotta proprio sul trackpad del Surface, cioe' sulla
 * macchina di sviluppo.
 *
 * CONTRATTO: resto non deve essere NULL, e deve essere lo stesso fra chiamate
 * successive (un solo mouse, un solo accumulo). Se e' NULL si torna 0 senza
 * scrivere niente. */
int winq_rotella_scatti(int delta, int *resto);

#endif /* WINQ_ROTELLA_H */
