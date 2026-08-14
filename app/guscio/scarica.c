/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* scarica.c -- la rete e le impronte.
 *
 * Vedi scarica.h per il confine che questo file rispetta: qui c'e' SOLO cio'
 * che deve toccare Windows (WinHTTP per la rete, BCrypt per l'impronta,
 * GetDiskFreeSpaceExA per lo spazio). Ogni decisione su cosa manca, quale
 * voce del manifesto scegliere, quanto spazio serve resta in varianti.c, che
 * si prova senza rete: mescolarla qui dentro romperebbe quel confine.
 *
 * STESSO PRINCIPIO DI dati.c, non registro.c: un errore si comunica col
 * valore di ritorno e con la stringa errore fornita dal chiamante, mai con
 * una riga scritta da qualche parte -- questo modulo non conosce registro.c.
 *
 * PERCHE' WCHAR_T QUI DENTRO, quando tutto il resto del guscio usa le API
 * ...A (vedi il commento su file_nome_trasportabile in guscio.h): WinHTTP
 * non ha varianti ANSI. WinHttpOpenRequest, WinHttpConnect, WinHttpCrackUrl
 * esistono SOLO a stringa larga. La conversione (con ampia(), sotto) resta
 * confinata a cio' che tocca WinHTTP -- host, verbo, intestazioni, percorso
 * della richiesta -- mentre i percorsi su disco (destinazione, il suo
 * ".parziale", la cartella per lo spazio libero) restano stringhe strette
 * come ovunque nel resto del guscio, perche' fopen e le *A di Win32 le
 * accettano cosi'.
 *
 * TRE COSE CHE SEMBRANO DETTAGLI:
 *
 * 1) I RIMANDI VERSO UN ALTRO HOST. Gli URL di SourceForge finiscono in
 *    /download e rimandano a un mirror su un host diverso da quello della
 *    richiesta iniziale. WinHttpSetOption con WINHTTP_OPTION_REDIRECT_POLICY
 *    a WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS lo rende ESPLICITO invece di
 *    fidarsi del default: senza, un cambio di politica di Windows su una
 *    macchina futura scaricherebbe la pagina di rimando invece
 *    dell'immagine, e il sintomo sarebbe un file che comincia per '<' (HTML)
 *    invece che per 'P' 'K' (zip) -- scoperto solo dallo sha256 che non
 *    torna, molto dopo aver speso la banda.
 *
 * 2) LA RIPRESA CHE PUO' FALLIRE IN UN MODO SPECIFICO. Un download da
 *    1,17 GB su una rete domestica cade, quindi si riprende con
 *    "Range: bytes=<gia>-" aggiungendo in coda al ".parziale". Ma il server
 *    puo' IGNORARE quell'intestazione e rispondere 200 con l'immagine
 *    intera invece di 206 col solo resto: in quel caso continuare ad
 *    aggiungere in coda raddoppierebbe il file. Qui lo si riconosce dal
 *    codice di stato e si ricomincia da capo (troncando il ".parziale"),
 *    invece di fidarsi che 200 e 206 si equivalgano.
 *
 * 3) RINOMINA SOLO DOPO L'IMPRONTA, CANCELLA SE SBAGLIATA. Il file scende
 *    sempre su "<destinazione>.parziale" e passa al nome vero solo dopo che
 *    lo sha256 (calcolato a blocchi con BCrypt, mai l'intero file in
 *    memoria: sono 1,17 GB) corrisponde a quello atteso. Se non corrisponde
 *    il parziale si cancella e lo si dice: un mirror corrotto non deve
 *    lasciare un file che alla ripresa successiva sembra un download da
 *    continuare invece che da rifare. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include "scarica.h"

/* --- conversione stretta -> larga, confinata a cio' che tocca WinHTTP --- */

/* Converte s (codepage ANSI di questa macchina, come ovunque nel guscio) in
 * dest, capace di max wchar_t compreso il terminatore. false se s non ci
 * sta: chi chiama tratta una richiesta cosi' come "URL too long", non
 * come un URL diverso troncato in silenzio. */
static bool ampia(const char *s, wchar_t *dest, int max)
{
    return MultiByteToWideChar(CP_ACP, 0, s, -1, dest, max) > 0;
}

/* --- una richiesta HTTP GET, con o senza ripresa --------------------- */

typedef struct {
    HINTERNET sessione;
    HINTERNET connessione;
    HINTERNET richiesta;
} ScRichiesta;

static void sc_chiudi_richiesta(ScRichiesta *r)
{
    if (r->richiesta) {
        WinHttpCloseHandle(r->richiesta);
    }
    if (r->connessione) {
        WinHttpCloseHandle(r->connessione);
    }
    if (r->sessione) {
        WinHttpCloseHandle(r->sessione);
    }
    memset(r, 0, sizeof(*r));
}

/* Legge un'intestazione numerica (qui solo Content-Length) come stringa di
 * sole cifre e la converte a mano: WinHttpQueryHeaders con
 * WINHTTP_QUERY_FLAG_NUMBER la darebbe in un DWORD, che satura a 4 GB -- un
 * limite falso per un progetto che gia' scarica immagini vicine a quella
 * soglia. false se l'intestazione manca o non e' fatta di sole cifre. */
static bool sc_content_length(HINTERNET richiesta, uint64_t *fuori)
{
    wchar_t buf[32];
    DWORD buf_n = sizeof(buf);
    size_t i, n;

    if (!WinHttpQueryHeaders(richiesta, WINHTTP_QUERY_CONTENT_LENGTH,
                             WINHTTP_HEADER_NAME_BY_INDEX, buf, &buf_n,
                             WINHTTP_NO_HEADER_INDEX)) {
        return false;
    }
    n = wcslen(buf);
    if (n == 0 || n >= 20) {
        return false;
    }
    *fuori = 0;
    for (i = 0; i < n; i++) {
        if (buf[i] < L'0' || buf[i] > L'9') {
            return false;
        }
        *fuori = (*fuori * 10) + (uint64_t)(buf[i] - L'0');
    }
    return true;
}

/* Apre una GET verso url e lascia la risposta pronta da leggere: *status ha
 * il codice HTTP, e se lunghezza non e' NULL, *lunghezza e *lunghezza_nota
 * dicono il Content-Length dichiarato (false se il server non lo manda).
 *
 * range_da >= 0 aggiunge "Range: bytes=<range_da>-": e' la ripresa. -1
 * niente intestazione Range: e' lo scaricamento da capo, e anche il caso di
 * scarica_testo, che non riprende mai un testo a meta'.
 *
 * WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS e' impostato SEMPRE, anche quando
 * chi chiama non si aspetta un rimando: vedi il punto 1 del commento in
 * testa al file.
 *
 * Su successo chi chiama legge il corpo da r->richiesta con
 * WinHttpQueryDataAvailable / WinHttpReadData, e chiude SEMPRE con
 * sc_chiudi_richiesta, successo o fallimento. */
static bool sc_apri_richiesta(const char *url, int64_t range_da,
                              ScRichiesta *r, DWORD *status,
                              uint64_t *lunghezza, bool *lunghezza_nota,
                              char *errore, size_t errore_n)
{
    wchar_t url_l[2048];
    wchar_t schema[16];
    wchar_t host[256];
    wchar_t percorso[1024];
    wchar_t extra[1024];
    wchar_t oggetto[2048];
    URL_COMPONENTS uc;
    bool sicuro;
    DWORD politica = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    DWORD codice_n;

    memset(r, 0, sizeof(*r));
    if (lunghezza) {
        *lunghezza = 0;
    }
    if (lunghezza_nota) {
        *lunghezza_nota = false;
    }

    if (!ampia(url, url_l, (int)(sizeof(url_l) / sizeof(wchar_t)))) {
        snprintf(errore, errore_n, "URL too long or not convertible: %s",
                url);
        return false;
    }

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszScheme = schema;
    uc.dwSchemeLength = (DWORD)(sizeof(schema) / sizeof(wchar_t));
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(wchar_t));
    uc.lpszUrlPath = percorso;
    uc.dwUrlPathLength = (DWORD)(sizeof(percorso) / sizeof(wchar_t));
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = (DWORD)(sizeof(extra) / sizeof(wchar_t));

    if (!WinHttpCrackUrl(url_l, 0, 0, &uc)) {
        snprintf(errore, errore_n, "malformed URL: %s", url);
        return false;
    }
    sicuro = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    if (wcslen(percorso) + wcslen(extra) >=
        sizeof(oggetto) / sizeof(wchar_t)) {
        snprintf(errore, errore_n,
                "the request path is too long for %s", url);
        return false;
    }
    wcscpy(oggetto, percorso);
    wcscat(oggetto, extra);

    r->sessione = WinHttpOpen(L"Habumi/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                              0);
    if (!r->sessione) {
        snprintf(errore, errore_n, "WinHttpOpen failed (%lu)",
                (unsigned long)GetLastError());
        return false;
    }

    /* LE QUATTRO SCADENZE, ESPLICITE E NON I DEFAULT, perche' uno dei default
     * e' ILLIMITATO: WinHTTP risolve il nome dell'host senza alcuna scadenza
     * (il default documentato e' 0, cioe' "waits forever"), e la sola
     * connessione ne ha 60 s per se'. Con la rete appena caduta -- il cavo
     * staccato, il Wi-Fi che se ne va, il DNS irraggiungibile -- un thread
     * fermo dentro questa funzione poteva quindi non tornare MAI.
     *
     * COSA SI ROMPE SENZA QUESTA CHIAMATA, e non e' solo un download lento:
     * chi chiude il guscio mentre lo scaricamento e' in corso lo aspetta a
     * tempo (variante_ferma in main.c), e quell'attesa ha un bilancio. Con la
     * risoluzione illimitata il bilancio non esisteva: era una speranza. Da
     * qui invece e' un conto -- 5+10+30 s e' il tetto di WinHttpSendRequest
     * (risolve, connette, invia, e ogni fase parte solo se la precedente e'
     * riuscita), piu' 30 s per WinHttpReceiveResponse, piu' 30 s per la
     * lettura in volo: 105 s, il numero su cui main.c dimensiona la propria
     * attesa d'uscita.
     *
     * I 30 s di RICEZIONE non sono per l'intero scaricamento ma per ogni
     * singola operazione: 64 KB che non arrivano in mezzo minuto sono una
     * connessione morta, non una lenta, e 1,17 GB continuano a scendere
     * finche' scendono.
     *
     * SULLA SESSIONE e non sulla richiesta: la scadenza di connessione serve
     * prima che una richiesta esista. Il fallimento e' fatale apposta --
     * proseguire vorrebbe dire tornare alla risoluzione illimitata, cioe'
     * esattamente al difetto che questa chiamata chiude. */
    if (!WinHttpSetTimeouts(r->sessione, 5000, 10000, 30000, 30000)) {
        snprintf(errore, errore_n,
                "cannot set the network timeouts (%lu)",
                (unsigned long)GetLastError());
        sc_chiudi_richiesta(r);
        return false;
    }

    r->connessione = WinHttpConnect(r->sessione, host, uc.nPort, 0);
    if (!r->connessione) {
        snprintf(errore, errore_n, "connection to %s failed (%lu)", url,
                (unsigned long)GetLastError());
        sc_chiudi_richiesta(r);
        return false;
    }

    r->richiesta = WinHttpOpenRequest(r->connessione, L"GET", oggetto, NULL,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      sicuro ? WINHTTP_FLAG_SECURE : 0);
    if (!r->richiesta) {
        snprintf(errore, errore_n, "request to %s not created (%lu)", url,
                (unsigned long)GetLastError());
        sc_chiudi_richiesta(r);
        return false;
    }

    /* Vedi il punto 1 del commento in testa al file: esplicito, non il
     * default, perche' un rimando di SourceForge cambia host. Il fallimento
     * di WinHttpSetOption qui non e' fatale quanto quello di tutto il resto:
     * al peggio si torna al comportamento di default di Windows, che gia'
     * segue la maggior parte dei rimandi -- ma si prova comunque sempre,
     * incondizionatamente. */
    WinHttpSetOption(r->richiesta, WINHTTP_OPTION_REDIRECT_POLICY, &politica,
                     sizeof(politica));

    if (range_da >= 0) {
        char intest_s[64];
        wchar_t intest_l[64];

        if (snprintf(intest_s, sizeof(intest_s), "Range: bytes=%llu-",
                    (unsigned long long)range_da) >= (int)sizeof(intest_s)) {
            snprintf(errore, errore_n, "Range header too long");
            sc_chiudi_richiesta(r);
            return false;
        }
        if (!ampia(intest_s, intest_l,
                  (int)(sizeof(intest_l) / sizeof(wchar_t)))) {
            snprintf(errore, errore_n,
                    "Range header not convertible");
            sc_chiudi_richiesta(r);
            return false;
        }
        if (!WinHttpAddRequestHeaders(r->richiesta, intest_l,
                                      (DWORD)wcslen(intest_l),
                                      WINHTTP_ADDREQ_FLAG_ADD)) {
            snprintf(errore, errore_n,
                    "cannot add the Range header (%lu)",
                    (unsigned long)GetLastError());
            sc_chiudi_richiesta(r);
            return false;
        }
    }

    if (!WinHttpSendRequest(r->richiesta, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        snprintf(errore, errore_n, "request to %s not sent (%lu)",
                url, (unsigned long)GetLastError());
        sc_chiudi_richiesta(r);
        return false;
    }
    if (!WinHttpReceiveResponse(r->richiesta, NULL)) {
        snprintf(errore, errore_n, "no answer from %s (%lu)", url,
                (unsigned long)GetLastError());
        sc_chiudi_richiesta(r);
        return false;
    }

    codice_n = sizeof(*status);
    if (!WinHttpQueryHeaders(r->richiesta,
                             WINHTTP_QUERY_STATUS_CODE |
                                     WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, status, &codice_n,
                             WINHTTP_NO_HEADER_INDEX)) {
        snprintf(errore, errore_n, "status code missing from %s (%lu)", url,
                (unsigned long)GetLastError());
        sc_chiudi_richiesta(r);
        return false;
    }

    if (lunghezza) {
        uint64_t cl;
        bool nota = sc_content_length(r->richiesta, &cl);

        *lunghezza = nota ? cl : 0;
        if (lunghezza_nota) {
            *lunghezza_nota = nota;
        }
    }

    return true;
}

/* --- scarica_testo ----------------------------------------------------- */

bool scarica_testo(const char *url, char *fuori, size_t fuori_n,
                   char *errore, size_t errore_n)
{
    ScRichiesta r;
    DWORD status = 0;
    size_t usati = 0;

    errore[0] = '\0';
    if (fuori_n == 0) {
        snprintf(errore, errore_n, "empty output buffer");
        return false;
    }
    fuori[0] = '\0';
    if (url == NULL || url[0] == '\0') {
        snprintf(errore, errore_n, "empty URL");
        return false;
    }

    if (!sc_apri_richiesta(url, -1, &r, &status, NULL, NULL, errore,
                           errore_n)) {
        return false;
    }
    if (status != 200) {
        snprintf(errore, errore_n, "HTTP %lu from %s (expected 200)",
                (unsigned long)status, url);
        sc_chiudi_richiesta(&r);
        return false;
    }

    for (;;) {
        DWORD disponibili = 0;
        DWORD letti = 0;
        size_t capienza;

        if (!WinHttpQueryDataAvailable(r.richiesta, &disponibili)) {
            snprintf(errore, errore_n, "read from %s interrupted (%lu)", url,
                    (unsigned long)GetLastError());
            sc_chiudi_richiesta(&r);
            return false;
        }
        if (disponibili == 0) {
            break;
        }
        capienza = fuori_n - 1 - usati;
        if (capienza == 0) {
            snprintf(errore, errore_n,
                    "answer from %s larger than the %zu-byte buffer", url,
                    fuori_n);
            sc_chiudi_richiesta(&r);
            return false;
        }
        if ((size_t)disponibili > capienza) {
            disponibili = (DWORD)capienza;
        }
        if (!WinHttpReadData(r.richiesta, fuori + usati, disponibili,
                             &letti) ||
            letti == 0) {
            snprintf(errore, errore_n, "read from %s interrupted (%lu)", url,
                    (unsigned long)GetLastError());
            sc_chiudi_richiesta(&r);
            return false;
        }
        usati += letti;
    }
    fuori[usati] = '\0';
    sc_chiudi_richiesta(&r);
    return true;
}

/* --- scarica_spazio_libero ---------------------------------------------- */

bool scarica_spazio_libero(const char *cartella, uint64_t *byte)
{
    ULARGE_INTEGER liberi;

    if (cartella == NULL || byte == NULL) {
        return false;
    }
    if (!GetDiskFreeSpaceExA(cartella, &liberi, NULL, NULL)) {
        return false;
    }
    *byte = liberi.QuadPart;
    return true;
}

/* --- scarica_sha256_file ------------------------------------------------- */

bool scarica_sha256_file(const char *percorso, char *fuori65, char *errore,
                         size_t errore_n)
{
    static const char cifre[] = "0123456789abcdef";
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    NTSTATUS st;
    DWORD cb_oggetto = 0, cb_hash = 0, cb_scritti = 0;
    unsigned char oggetto_hash[1024];
    unsigned char digest[32];
    unsigned char blocco[65536];
    FILE *f;
    size_t letti, i;
    bool ok = false;

    errore[0] = '\0';
    fuori65[0] = '\0';

    /* Letto a blocchi apposta: sono 1,17 GB, e non stanno in memoria tutti
     * insieme -- ne' dovrebbero, quando l'unica cosa che serve e' farli
     * scorrere una volta sola dentro BCrypt. */
    f = fopen(percorso, "rb");
    if (f == NULL) {
        snprintf(errore, errore_n,
                "cannot open %s to hash it",
                percorso);
        return false;
    }

    st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (st != 0) {
        snprintf(errore, errore_n,
                "BCryptOpenAlgorithmProvider failed (0x%lx)",
                (unsigned long)st);
        fclose(f);
        return false;
    }

    st = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cb_oggetto,
                           sizeof(cb_oggetto), &cb_scritti, 0);
    if (st != 0 || cb_oggetto == 0 || cb_oggetto > sizeof(oggetto_hash)) {
        snprintf(errore, errore_n,
                "unexpected hash object size (%lu)",
                (unsigned long)cb_oggetto);
        goto fine;
    }

    st = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&cb_hash,
                           sizeof(cb_hash), &cb_scritti, 0);
    if (st != 0 || cb_hash != sizeof(digest)) {
        snprintf(errore, errore_n, "unexpected hash length (%lu)",
                (unsigned long)cb_hash);
        goto fine;
    }

    st = BCryptCreateHash(alg, &hash, oggetto_hash, cb_oggetto, NULL, 0, 0);
    if (st != 0) {
        snprintf(errore, errore_n, "BCryptCreateHash failed (0x%lx)",
                (unsigned long)st);
        goto fine;
    }

    for (;;) {
        letti = fread(blocco, 1, sizeof(blocco), f);
        if (letti > 0) {
            st = BCryptHashData(hash, blocco, (ULONG)letti, 0);
            if (st != 0) {
                snprintf(errore, errore_n, "BCryptHashData failed (0x%lx)",
                        (unsigned long)st);
                goto fine;
            }
        }
        if (letti < sizeof(blocco)) {
            if (ferror(f)) {
                snprintf(errore, errore_n, "read of %s interrupted",
                        percorso);
                goto fine;
            }
            break;
        }
    }

    st = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    if (st != 0) {
        snprintf(errore, errore_n, "BCryptFinishHash failed (0x%lx)",
                (unsigned long)st);
        goto fine;
    }

    for (i = 0; i < sizeof(digest); i++) {
        fuori65[i * 2] = cifre[digest[i] >> 4];
        fuori65[i * 2 + 1] = cifre[digest[i] & 0x0F];
    }
    fuori65[sizeof(digest) * 2] = '\0';
    ok = true;

fine:
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    fclose(f);
    return ok;
}

/* --- helper di scarica_file, puri o quasi -------------------------------- */

/* Confronta due impronte sha256 senza distinguere maiuscole da minuscole.
 * NON e' condivisa con sha256_uguale di varianti.c: quel file resta PURO
 * (vedi il commento in testa a varianti.h) e questo tocca gia' Win32, quindi
 * non c'e' un header comune a cui affidare una funzione cosi' piccola senza
 * far dipendere l'uno dall'altro. */
static bool sc_sha_uguale(const char *a, const char *b)
{
    size_t i;

    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        char ca = a[i], cb = b[i];

        if (ca >= 'A' && ca <= 'F') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'F') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
    }
    return a[i] == b[i];
}

/* La cartella che contiene percorso, con la barra finale, o "." se percorso
 * non ne ha una: e' li' che vive lo spazio libero da controllare. Stessa
 * logica di dati_cartella_temp in dati.c (entrambi i separatori, per lo
 * stesso motivo: var_percorsi scrive '/', il resto del guscio '\'), non
 * condivisa per la stessa ragione di sc_sha_uguale qui sopra. */
static bool sc_cartella_di(const char *percorso, char *dest, size_t max)
{
    const char *sep_win = strrchr(percorso, '\\');
    const char *sep_unix = strrchr(percorso, '/');
    const char *sep = sep_win;
    size_t dir_len;

    if (sep_unix && (!sep || sep_unix > sep)) {
        sep = sep_unix;
    }
    if (!sep) {
        if (max < 2) {
            return false;
        }
        dest[0] = '.';
        dest[1] = '\0';
        return true;
    }
    dir_len = (size_t)(sep - percorso) + 1;
    if (dir_len >= max) {
        return false;
    }
    memcpy(dest, percorso, dir_len);
    dest[dir_len] = '\0';
    return true;
}

/* La dimensione di percorso, 0 se non esiste (o non si legge: chi chiama
 * tratta i due casi allo stesso modo, "no partial file to resume"). */
static uint64_t sc_dimensione_file(const char *percorso)
{
    WIN32_FILE_ATTRIBUTE_DATA info;

    if (!GetFileAttributesExA(percorso, GetFileExInfoStandard, &info)) {
        return 0;
    }
    return ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
}

/* --- scarica_file -------------------------------------------------------- */

bool scarica_file(const char *url, const char *destinazione,
                  const char *sha256_atteso, uint64_t byte_attesi,
                  ScaricaAvanzamento avanti, void *dato, char *errore,
                  size_t errore_n)
{
    char parziale[MAX_PATH + 16];
    char cartella[MAX_PATH];
    char sha_calc[65];
    uint64_t gia;
    ScRichiesta r;
    DWORD status = 0;
    uint64_t content_length = 0;
    bool content_length_nota = false;
    bool modalita_riprendi;
    uint64_t atteso;

    errore[0] = '\0';
    if (url == NULL || url[0] == '\0' || destinazione == NULL ||
        destinazione[0] == '\0') {
        snprintf(errore, errore_n, "empty URL or destination");
        return false;
    }

    if (snprintf(parziale, sizeof(parziale), "%s.parziale", destinazione) >=
        (int)sizeof(parziale)) {
        snprintf(errore, errore_n, "the path of %s is too long",
                destinazione);
        return false;
    }

    /* Il file di destinazione c'e' gia' ed e' gia' quello giusto: non si
     * riscarica, si verifica l'impronta e si ritorna successo. Se invece non
     * corrisponde (o non si legge) non e' un errore da riportare qui: si
     * procede sotto come se mancasse, e MoveFileExA con
     * MOVEFILE_REPLACE_EXISTING alla fine la sostituira'. */
    if (GetFileAttributesA(destinazione) != INVALID_FILE_ATTRIBUTES) {
        if (scarica_sha256_file(destinazione, sha_calc, errore, errore_n) &&
            sc_sha_uguale(sha_calc, sha256_atteso)) {
            errore[0] = '\0';
            return true;
        }
        errore[0] = '\0';
    }

    gia = sc_dimensione_file(parziale);
    if (gia > byte_attesi) {
        /* Piu' grande del previsto: e' corrotto, non un parziale valido su
         * cui riprendere. Si ricomincia da zero invece di costruire un Range
         * che nessun server puo' soddisfare. */
        DeleteFileA(parziale);
        gia = 0;
    }

    if (gia < byte_attesi) {
        uint64_t serve = byte_attesi - gia;
        uint64_t liberi = 0;

        /* Lo spazio si controlla PRIMA di aprire una connessione, non a
         * meta' di un download da 1,17 GB. */
        if (!sc_cartella_di(destinazione, cartella, sizeof(cartella))) {
            snprintf(errore, errore_n, "path of %s too long",
                    destinazione);
            return false;
        }
        if (!scarica_spazio_libero(cartella, &liberi)) {
            snprintf(errore, errore_n,
                    "cannot read the free space on %s", cartella);
            return false;
        }
        if (liberi < serve) {
            snprintf(errore, errore_n,
                    "not enough space for %s: %llu bytes needed, "
                    "%llu of %s",
                    destinazione, (unsigned long long)serve,
                    (unsigned long long)liberi, cartella);
            return false;
        }

        if (!sc_apri_richiesta(url, gia > 0 ? (int64_t)gia : -1, &r, &status,
                               &content_length, &content_length_nota, errore,
                               errore_n)) {
            return false;
        }

        if (gia > 0 && status == 206) {
            /* Il server rispetta il Range: il resto arriva in coda. */
            modalita_riprendi = true;
            atteso = byte_attesi - gia;
        } else if (status == 200) {
            /* Il server manda tutto (Range assente, o presente ma
             * IGNORATO): vedi il punto 2 del commento in testa al file. Un
             * parziale gia' iniziato si tronca invece di aggiungere in coda,
             * o il file uscirebbe lungo il doppio. */
            modalita_riprendi = false;
            gia = 0;
            atteso = byte_attesi;
        } else {
            snprintf(errore, errore_n,
                    "HTTP %lu from %s (expected 200 or 206)",
                    (unsigned long)status, url);
            sc_chiudi_richiesta(&r);
            return false;
        }

        /* Un Content-Length che non combacia e' un errore da nominare PRIMA
         * di scaricare un gigabyte, non uno sha256 sbagliato da scoprire
         * dopo aver speso la banda. Manca del tutto -> stesso trattamento:
         * non si scarica alla cieca. */
        if (!content_length_nota) {
            snprintf(errore, errore_n,
                    "the server declares no Content-Length for %s: stopping "
                    "before downloading",
                    url);
            sc_chiudi_richiesta(&r);
            return false;
        }
        if (content_length != atteso) {
            snprintf(errore, errore_n,
                    "Content-Length %llu from %s does not match the %llu bytes "
                    "expected: stopping before downloading",
                    (unsigned long long)content_length, url,
                    (unsigned long long)atteso);
            sc_chiudi_richiesta(&r);
            return false;
        }

        {
            FILE *f = fopen(parziale, modalita_riprendi ? "ab" : "wb");
            uint64_t fatti = gia;
            unsigned char blocco[65536];
            const char *motivo = NULL;

            if (f == NULL) {
                snprintf(errore, errore_n, "cannot write to %s",
                        parziale);
                sc_chiudi_richiesta(&r);
                return false;
            }

            for (;;) {
                DWORD disponibili = 0;
                DWORD letti = 0;

                if (!WinHttpQueryDataAvailable(r.richiesta, &disponibili)) {
                    motivo = "read interrupted";
                    break;
                }
                if (disponibili == 0) {
                    break;
                }
                if (disponibili > sizeof(blocco)) {
                    disponibili = (DWORD)sizeof(blocco);
                }
                if (!WinHttpReadData(r.richiesta, blocco, disponibili,
                                     &letti) ||
                    letti == 0) {
                    motivo = "read interrupted";
                    break;
                }
                if (fwrite(blocco, 1, letti, f) != letti) {
                    motivo = "write failed";
                    break;
                }
                fatti += letti;
                if (avanti != NULL && !avanti(fatti, byte_attesi, dato)) {
                    motivo = "cancelled";
                    break;
                }
            }

            fclose(f);
            sc_chiudi_richiesta(&r);

            if (motivo != NULL) {
                /* Ne' "read interrupted" ne' "cancelled" cancellano il
                 * parziale: sono entrambi ripresi al prossimo giro, ed e'
                 * proprio questo il caso per cui esiste la ripresa. Una
                 * scrittura fallita (disco pieno a meta', permessi) lascia
                 * lo stesso il parziale intatto, per lo stesso motivo. */
                snprintf(errore, errore_n, "%s while downloading %s (%lu)", motivo,
                        url, (unsigned long)GetLastError());
                return false;
            }
        }
    }

    /* Verifica finale: sia dopo aver scaricato sia nel caso in cui gia era
     * gia' arrivato a byte_attesi (una rinomina precedente fallita, per
     * esempio) senza fare altra rete. */
    if (!scarica_sha256_file(parziale, sha_calc, errore, errore_n)) {
        /* errore gia' scritto da scarica_sha256_file; il parziale resta,
         * perche' non e' detto che l'impronta sia sbagliata -- puo' essere
         * un guasto di lettura transitorio. */
        return false;
    }
    if (!sc_sha_uguale(sha_calc, sha256_atteso)) {
        /* L'impronta NON corrisponde: si cancella. Un mirror corrotto non
         * deve lasciare spazzatura che alla ripresa successiva sembra un
         * download da continuare. */
        DeleteFileA(parziale);
        snprintf(errore, errore_n,
                "wrong hash for %s: expected %s, got %s -- "
                "partial file deleted",
                destinazione, sha256_atteso, sha_calc);
        return false;
    }

    if (!MoveFileExA(parziale, destinazione, MOVEFILE_REPLACE_EXISTING)) {
        snprintf(errore, errore_n,
                "downloaded and verified but I cannot rename %s onto %s "
                "(%lu)",
                parziale, destinazione, (unsigned long)GetLastError());
        return false;
    }
    return true;
}
