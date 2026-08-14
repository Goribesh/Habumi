/* winq-coord.c
 * Mappatura finestra -> guest, tenendo conto delle bande nere quando le
 * proporzioni non coincidono.
 *
 * I punti nelle bande si SCARTANO invece di saturarli al bordo: saturare
 * produrrebbe tocchi fantasma sul lato dello schermo, che nell'interfaccia di
 * Android aprono pannelli laterali senza che l'utente li abbia chiesti.
 *
 * Questa e' una PREVISIONE, non una misura: nessuno ha ancora visto il sintomo,
 * perche' con SDL il tocco non arrivava affatto al guest. Si scarta perche' e' la
 * scelta reversibile -- un tocco perso sul bordo si nota e si corregge, un
 * pannello che si apre da solo si attribuisce ad Android e si inseguono cause
 * sbagliate. Se una misura dice che saturare va bene, si cambia. */
#include "winq-coord.h"

bool winq_guest_rect(const WinqGeom *g, double *x, double *y,
                     double *w, double *h)
{
    if (g->win_w <= 0 || g->win_h <= 0 || g->guest_w <= 0 || g->guest_h <= 0) {
        return false;
    }

    /* Scala in virgola mobile: con l'aritmetica intera si perderebbe fino a un
     * pixel per ogni fattore, e su un tocco si vede. */
    double sx = (double)g->win_w / g->guest_w;
    double sy = (double)g->win_h / g->guest_h;
    double s = sx < sy ? sx : sy;

    *w = g->guest_w * s;
    *h = g->guest_h * s;
    *x = (g->win_w - *w) / 2.0;
    *y = (g->win_h - *h) / 2.0;
    return true;
}

bool winq_win_to_guest(const WinqGeom *g, int wx, int wy, int *gx, int *gy)
{
    double banda_x, banda_y, usata_w, usata_h;

    if (!winq_guest_rect(g, &banda_x, &banda_y, &usata_w, &usata_h)) {
        return false;
    }

    /* La scala si ricava dal rettangolo invece di ricalcolarla: cosi' non
     * esistono due espressioni della stessa cosa da tenere d'accordo. */
    double s = usata_w / g->guest_w;

    double rx = (wx - banda_x) / s;
    double ry = (wy - banda_y) / s;

    if (rx < 0 || ry < 0 || rx >= g->guest_w || ry >= g->guest_h) {
        return false;
    }

    *gx = (int)rx;
    *gy = (int)ry;
    return true;
}
