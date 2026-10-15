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

void winq_rotation_source_uv(unsigned rotation, double u, double v,
                            double *su, double *sv)
{
    double x, y;

    if (rotation > 3 || !su || !sv) {
        return;
    }
    switch (rotation) {
    case 0: x = u;     y = v;     break;
    case 1: x = v;     y = 1 - u; break;
    case 2: x = 1 - u; y = 1 - v; break;
    default:x = 1 - v; y = u;     break;
    }
    *su = x;
    *sv = y;
}

bool winq_guest_rect(const WinqGeom *g, double *x, double *y,
                     double *w, double *h)
{
    if (!g || g->rotation > 3 || g->win_w <= 0 || g->win_h <= 0 ||
        g->guest_w <= 0 || g->guest_h <= 0) {
        return false;
    }

    /* Scala in virgola mobile: con l'aritmetica intera si perderebbe fino a un
     * pixel per ogni fattore, e su un tocco si vede. */
    int display_w = (g->rotation & 1) ? g->guest_h : g->guest_w;
    int display_h = (g->rotation & 1) ? g->guest_w : g->guest_h;
    double sx = (double)g->win_w / display_w;
    double sy = (double)g->win_h / display_h;
    double s = sx < sy ? sx : sy;

    *w = display_w * s;
    *h = display_h * s;
    *x = (g->win_w - *w) / 2.0;
    *y = (g->win_h - *h) / 2.0;
    return true;
}

bool winq_win_to_guest(const WinqGeom *g, int wx, int wy, int *gx, int *gy)
{
    double banda_x, banda_y, usata_w, usata_h;
    double u, v, su, sv;
    int display_w, display_h;

    if (!winq_guest_rect(g, &banda_x, &banda_y, &usata_w, &usata_h)) {
        return false;
    }

    display_w = (g->rotation & 1) ? g->guest_h : g->guest_w;
    display_h = (g->rotation & 1) ? g->guest_w : g->guest_h;

    /* La scala si ricava dal rettangolo invece di ricalcolarla: cosi' non
     * esistono due espressioni della stessa cosa da tenere d'accordo. */
    double s = usata_w / ((g->rotation & 1) ? g->guest_h : g->guest_w);

    double rx = (wx - banda_x) / s;
    double ry = (wy - banda_y) / s;

    if (rx < 0 || ry < 0 || rx >= display_w || ry >= display_h) {
        return false;
    }

    /* Rotate pixel centres, rather than their upper-left edges: an inverted
     * edge would otherwise select the neighbouring pixel (or width/height). */
    u = ((int)rx + 0.5) / display_w;
    v = ((int)ry + 0.5) / display_h;
    winq_rotation_source_uv(g->rotation, u, v, &su, &sv);
    /* La conversione inversa puo' portare il bordo esatto a width/height.
     * Clampa quel caso solo dopo aver escluso bande e punti esterni. */
    int px = (int)(su * g->guest_w);
    int py = (int)(sv * g->guest_h);
    if (px >= g->guest_w) px = g->guest_w - 1;
    if (py >= g->guest_h) py = g->guest_h - 1;
    *gx = px;
    *gy = py;
    return true;
}
