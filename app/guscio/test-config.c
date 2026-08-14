/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-config.c -- si compila da solo. Scrive file temporanei nella cartella
 * corrente e li rimuove. */
#include <stdio.h>
#include <string.h>
#include "guscio.h"

static int totali = 0;
static int fallimenti = 0;

#define CHECK(expr) do { \
    totali++; \
    if (!(expr)) { \
        fallimenti++; \
        printf("FALLITO %s:%d  %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static void scrivi(const char *nome, const char *contenuto)
{
    FILE *f = fopen(nome, "wb");
    if (f) {
        fputs(contenuto, f);
        fclose(f);
    }
}

static void i_default_sono_quelli_della_spec(void)
{
    Config c;

    config_default(&c);
    CHECK(c.vcpu == 6);
    CHECK(c.memoria == 6144);
    CHECK(c.larghezza == 1280);
    CHECK(c.altezza == 800);
    CHECK(c.riprove == 3);
    CHECK(c.porta_adb == 15555);
    CHECK(c.porta_appunti == 15556);
    CHECK(strcmp(c.gl, "wgl") == 0);
    /* l'audio e' acceso di default: il kernel del guest ha
     * virtio-snd builtin, quindi /dev/snd esiste e la catena fino a Windows e'
     * provata. Un emulatore muto sorprende chi apre un video. */
    CHECK(c.audio == true);
}

static void un_file_mancante_lascia_i_default(void)
{
    Config c;

    config_default(&c);
    config_carica(&c, "questo-file-non-esiste.txt");
    CHECK(c.vcpu == 6);
    CHECK(c.porta_adb == 15555);
}

static void un_file_parziale_completa_col_default(void)
{
    Config c;

    scrivi("prova-parziale.txt", "vcpu=8\n");
    config_default(&c);
    config_carica(&c, "prova-parziale.txt");
    CHECK(c.vcpu == 8);
    CHECK(c.memoria == 6144);
    remove("prova-parziale.txt");
}

static void commenti_spazi_e_righe_vuote_non_disturbano(void)
{
    Config c;

    scrivi("prova-commenti.txt",
           "# un commento\n"
           "\n"
           "  memory = 8192  \n"
           "gl=angle\n"
           "audio=on\n");
    config_default(&c);
    config_carica(&c, "prova-commenti.txt");
    CHECK(c.memoria == 8192);
    CHECK(strcmp(c.gl, "angle") == 0);
    CHECK(c.audio == true);
    remove("prova-commenti.txt");
}

static void un_valore_fuori_intervallo_resta_al_default(void)
{
    Config c;

    scrivi("prova-fuori.txt", "vcpu=999\nmemory=-5\nwidth=0\n");
    config_default(&c);
    config_carica(&c, "prova-fuori.txt");
    CHECK(c.vcpu == 6);
    CHECK(c.memoria == 6144);
    CHECK(c.larghezza == 1280);
    remove("prova-fuori.txt");
}

static void la_porta_nell_intervallo_degli_emulatori_e_rifiutata(void)
{
    Config c;

    /* adb esplora 5554-5585 per scoprire gli emulatori e prenderebbe la nostra
     * porta per uno di essi, lasciando tutto offline. Il difetto e' misurato e
     * intermittente, quindi la configurazione non deve permetterlo. */
    scrivi("prova-porta.txt", "adb_port=5555\n");
    config_default(&c);
    config_carica(&c, "prova-porta.txt");
    CHECK(c.porta_adb == 15555);
    remove("prova-porta.txt");

    /* I DUE CONFINI ESATTI, non solo il centro dell'intervallo: e' la lezione
     * di processo che questo stesso progetto ha imparato con 5555 (un valore
     * centrale, che avrebbe passato la prova anche con un confine sbagliato
     * di uno in "p >= 5554 && p <= 5585"). Una prova che non tocca i confini
     * non e' una prova di un confine. */
    scrivi("prova-porta-min.txt", "adb_port=5554\n");
    config_default(&c);
    config_carica(&c, "prova-porta-min.txt");
    CHECK(c.porta_adb == 15555);
    remove("prova-porta-min.txt");

    scrivi("prova-porta-max.txt", "adb_port=5585\n");
    config_default(&c);
    config_carica(&c, "prova-porta-max.txt");
    CHECK(c.porta_adb == 15555);
    remove("prova-porta-max.txt");

    scrivi("prova-porta2.txt", "adb_port=15600\n");
    config_default(&c);
    config_carica(&c, "prova-porta2.txt");
    CHECK(c.porta_adb == 15600);
    remove("prova-porta2.txt");
}

/* porta_appunti ha TRE modi di essere rifiutata, e sono tre prove distinte
 * perche' hanno tre cause diverse: l'intervallo generale, l'intervallo che adb
 * esplora, e la collisione con porta_adb. Un rifiuto non ferma l'avvio: il
 * valore si ignora e resta il default. */
static void la_porta_degli_appunti_ha_tre_rifiuti(void)
{
    Config c;

    /* Un valore buono passa: senza questo caso le prove qui sotto passerebbero
     * anche con una chiave che non viene letta affatto. */
    scrivi("prova-ap-buona.txt", "clipboard_port=16000\n");
    config_default(&c);
    config_carica(&c, "prova-ap-buona.txt");
    CHECK(c.porta_appunti == 16000);
    remove("prova-ap-buona.txt");

    /* RIFIUTO 1: fuori da 1024..65535. I confini esatti, non un valore
     * centrale: e' la lezione gia' imparata su adb_port=5555. */
    scrivi("prova-ap-min.txt", "clipboard_port=1024\n");
    config_default(&c);
    config_carica(&c, "prova-ap-min.txt");
    CHECK(c.porta_appunti == 1024);
    remove("prova-ap-min.txt");

    scrivi("prova-ap-max.txt", "clipboard_port=65535\n");
    config_default(&c);
    config_carica(&c, "prova-ap-max.txt");
    CHECK(c.porta_appunti == 65535);
    remove("prova-ap-max.txt");

    scrivi("prova-ap-sotto.txt", "clipboard_port=1023\n");
    config_default(&c);
    config_carica(&c, "prova-ap-sotto.txt");
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-sotto.txt");

    scrivi("prova-ap-sopra.txt", "clipboard_port=65536\n");
    config_default(&c);
    config_carica(&c, "prova-ap-sopra.txt");
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-sopra.txt");

    /* RIFIUTO 2: dentro 5554..5585, che adb esplora in coppie per scoprire gli
     * emulatori. Non e' la porta di adb, ma e' una porta che adb andra'
     * comunque a sondare, e una connessione di adb su questo canale manderebbe
     * byte che non sono trame. I due confini esatti piu' un valore in mezzo. */
    scrivi("prova-ap-adbmin.txt", "clipboard_port=5554\n");
    config_default(&c);
    config_carica(&c, "prova-ap-adbmin.txt");
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-adbmin.txt");

    scrivi("prova-ap-adbmed.txt", "clipboard_port=5555\n");
    config_default(&c);
    config_carica(&c, "prova-ap-adbmed.txt");
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-adbmed.txt");

    scrivi("prova-ap-adbmax.txt", "clipboard_port=5585\n");
    config_default(&c);
    config_carica(&c, "prova-ap-adbmax.txt");
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-adbmax.txt");

    /* RIFIUTO 3: uguale a porta_adb. Due server sulla stessa porta non possono
     * mettersi in ascolto entrambi. */
    scrivi("prova-ap-uguale.txt", "adb_port=16000\nclipboard_port=16000\n");
    config_default(&c);
    config_carica(&c, "prova-ap-uguale.txt");
    CHECK(c.porta_adb == 16000);
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-uguale.txt");

    /* LO STESSO CASO CON LE RIGHE SCAMBIATE, ed e' la prova che conta di piu' di
     * questa terna: se il confronto avvenisse mentre si legge la riga, qui
     * porta_adb varrebbe ancora il suo default e la collisione passerebbe
     * inosservata -- due porte uguali, e come sola traccia un errore di bind che
     * non nomina la causa. */
    scrivi("prova-ap-uguale2.txt", "clipboard_port=16000\nadb_port=16000\n");
    config_default(&c);
    config_carica(&c, "prova-ap-uguale2.txt");
    CHECK(c.porta_adb == 16000);
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-uguale2.txt");

    /* E il caso simmetrico che NON e' una collisione: porta_adb spostata su un
     * valore diverso lascia passare il default degli appunti. */
    scrivi("prova-ap-diversa.txt", "adb_port=16000\n");
    config_default(&c);
    config_carica(&c, "prova-ap-diversa.txt");
    CHECK(c.porta_appunti == 15556);
    remove("prova-ap-diversa.txt");
}

static void un_gl_sconosciuto_resta_al_default(void)
{
    Config c;

    scrivi("prova-gl.txt", "gl=vulkan\n");
    config_default(&c);
    config_carica(&c, "prova-gl.txt");
    CHECK(strcmp(c.gl, "wgl") == 0);
    remove("prova-gl.txt");
}

static void la_scala_guest_e_no_di_default_e_vuole_si_o_no(void)
{
    Config c;

    /* Il default (del codice) e' "no" perche' con "si" il guest disegnerebbe
     * 4x i pixel su questa macchina. Il costo E' misurato (tabella nel
     * commento della chiave in runtime/bin/config.txt): la mediana GPU passa
     * da 4 ms a 1280x800 a 7 ms a
     * 2560x1600@60. Un default che costa senza che nessuno l'abbia chiesto e'
     * peggio di una chiave in meno, anche conoscendone il prezzo. */
    config_default(&c);
    CHECK(c.scala_guest == false);

    scrivi("prova-scala-si.txt", "guest_scale=yes\n");
    config_default(&c);
    config_carica(&c, "prova-scala-si.txt");
    CHECK(c.scala_guest == true);
    remove("prova-scala-si.txt");

    scrivi("prova-scala-no.txt", "guest_scale=no\n");
    config_default(&c);
    config_carica(&c, "prova-scala-no.txt");
    CHECK(c.scala_guest == false);
    remove("prova-scala-no.txt");

    /* Un valore illeggibile NON accende la chiave: accenderla per errore
     * quadruplicherebbe i pixel che il guest disegna, con un costo che nessuno
     * ha misurato. La ragione finisce nel registro, come per ogni altra chiave. */
    scrivi("prova-scala-forse.txt", "guest_scale=forse\n");
    config_default(&c);
    config_carica(&c, "prova-scala-forse.txt");
    CHECK(c.scala_guest == false);
    remove("prova-scala-forse.txt");
}

/* hz: 0 = automatico (winq legge GetDeviceCaps(VREFRESH), il comportamento di
 * oggi), oppure 24..240. I DUE CONFINI ESATTI, non solo un valore centrale:
 * stessa lezione di adb_port=5555 (un valore centrale avrebbe passato la
 * prova anche con un confine sbagliato di uno). */
static void la_hz_e_zero_di_default_e_vuole_0_o_24_240(void)
{
    Config c;

    config_default(&c);
    CHECK(c.hz == 0);

    scrivi("prova-hz-zero.txt", "hz=0\n");
    /* Seminato a un valore diverso da 0 apposta: se intero_o_zero rifiutasse lo
     * zero invece di accettarlo, c.hz resterebbe 120 e questo CHECK fallirebbe
     * per davvero. Senza questo seme il campo varrebbe gia' 0 dal config_default
     * di sopra, e la prova passerebbe anche con lo zero rifiutato. */
    c.hz = 120;
    config_carica(&c, "prova-hz-zero.txt");
    CHECK(c.hz == 0);
    remove("prova-hz-zero.txt");

    scrivi("prova-hz-120.txt", "hz=120\n");
    config_default(&c);
    config_carica(&c, "prova-hz-120.txt");
    CHECK(c.hz == 120);
    remove("prova-hz-120.txt");

    scrivi("prova-hz-min.txt", "hz=24\n");
    config_default(&c);
    config_carica(&c, "prova-hz-min.txt");
    CHECK(c.hz == 24);
    remove("prova-hz-min.txt");

    scrivi("prova-hz-max.txt", "hz=240\n");
    config_default(&c);
    config_carica(&c, "prova-hz-max.txt");
    CHECK(c.hz == 240);
    remove("prova-hz-max.txt");

    scrivi("prova-hz-sotto.txt", "hz=23\n");
    config_default(&c);
    config_carica(&c, "prova-hz-sotto.txt");
    CHECK(c.hz == 0);
    remove("prova-hz-sotto.txt");

    scrivi("prova-hz-sopra.txt", "hz=241\n");
    config_default(&c);
    config_carica(&c, "prova-hz-sopra.txt");
    CHECK(c.hz == 0);
    remove("prova-hz-sopra.txt");
}

/* densita: 0 = non toccare (comportamento di oggi), oppure 120..640. Stessi
 * confini esatti della prova sopra, per la stessa ragione. */
static void la_densita_e_zero_di_default_e_vuole_0_o_120_640(void)
{
    Config c;

    config_default(&c);
    CHECK(c.densita == 0);

    scrivi("prova-densita-zero.txt", "density=0\n");
    /* Stesso seme della prova gemella su hz, e per la stessa ragione: senza
     * un valore diverso da 0 prima del carico, un intero_o_zero che rifiutasse
     * lo zero lascerebbe c.densita a 0 (dal config_default di sopra) e il
     * CHECK passerebbe comunque. */
    c.densita = 300;
    config_carica(&c, "prova-densita-zero.txt");
    CHECK(c.densita == 0);
    remove("prova-densita-zero.txt");

    scrivi("prova-densita-426.txt", "density=426\n");
    config_default(&c);
    config_carica(&c, "prova-densita-426.txt");
    CHECK(c.densita == 426);
    remove("prova-densita-426.txt");

    scrivi("prova-densita-min.txt", "density=120\n");
    config_default(&c);
    config_carica(&c, "prova-densita-min.txt");
    CHECK(c.densita == 120);
    remove("prova-densita-min.txt");

    scrivi("prova-densita-max.txt", "density=640\n");
    config_default(&c);
    config_carica(&c, "prova-densita-max.txt");
    CHECK(c.densita == 640);
    remove("prova-densita-max.txt");

    scrivi("prova-densita-sotto.txt", "density=119\n");
    config_default(&c);
    config_carica(&c, "prova-densita-sotto.txt");
    CHECK(c.densita == 0);
    remove("prova-densita-sotto.txt");

    scrivi("prova-densita-sopra.txt", "density=641\n");
    config_default(&c);
    config_carica(&c, "prova-densita-sopra.txt");
    CHECK(c.densita == 0);
    remove("prova-densita-sopra.txt");
}

/* variante: vanilla di default. Una parola sconosciuta o la chiave assente
 * non fermano l'avvio: si resta a vanilla, e config_carica lo scrive nel
 * registro (vedi il commento li' per il perche' -- fermarsi per una parola
 * sbagliata in un file di configurazione sarebbe peggio del difetto). */
static void la_variante_e_vanilla_di_default_e_vuole_vanilla_o_gapps(void)
{
    Config c;

    config_default(&c);
    CHECK(c.variante == VAR_VANILLA);

    scrivi("prova-variante-gapps.txt", "variant=gapps\n");
    config_default(&c);
    config_carica(&c, "prova-variante-gapps.txt");
    CHECK(c.variante == VAR_GAPPS);
    remove("prova-variante-gapps.txt");

    /* Seminata a GAPPS apposta, come gia' si fa per hz e densita' qui sopra:
     * senza questo seme il default di config_default nasconderebbe un
     * var_da_testo("vanilla", ...) che non funzionasse. */
    scrivi("prova-variante-vanilla.txt", "variant=vanilla\n");
    config_default(&c);
    c.variante = VAR_GAPPS;
    config_carica(&c, "prova-variante-vanilla.txt");
    CHECK(c.variante == VAR_VANILLA);
    remove("prova-variante-vanilla.txt");

    /* Una parola sconosciuta non e' fatale: si resta al valore di serie. */
    scrivi("prova-variante-sconosciuta.txt", "variant=pippo\n");
    config_default(&c);
    config_carica(&c, "prova-variante-sconosciuta.txt");
    CHECK(c.variante == VAR_VANILLA);
    remove("prova-variante-sconosciuta.txt");

    /* La chiave assente lascia il default, come ogni altra chiave. */
    config_default(&c);
    config_carica(&c, "questo-file-non-esiste.txt");
    CHECK(c.variante == VAR_VANILLA);
}

int main(void)
{
    registro_apri();
    i_default_sono_quelli_della_spec();
    un_file_mancante_lascia_i_default();
    un_file_parziale_completa_col_default();
    commenti_spazi_e_righe_vuote_non_disturbano();
    un_valore_fuori_intervallo_resta_al_default();
    la_porta_nell_intervallo_degli_emulatori_e_rifiutata();
    la_porta_degli_appunti_ha_tre_rifiuti();
    un_gl_sconosciuto_resta_al_default();
    la_scala_guest_e_no_di_default_e_vuole_si_o_no();
    la_hz_e_zero_di_default_e_vuole_0_o_24_240();
    la_densita_e_zero_di_default_e_vuole_0_o_120_640();
    la_variante_e_vanilla_di_default_e_vuole_vanilla_o_gapps();
    registro_chiudi();

    printf("test-config: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
