#!/bin/bash
# qemu/scripts/innesta-winq.sh
# Copia i nostri sorgenti dentro l'albero di QEMU e applica la patch di build.
#
# Perche' non si sviluppa direttamente dentro l'albero di QEMU: quello e' scaricato
# e ricreabile, i nostri sorgenti no. Stanno nel repo, e uno script li innesta —
# stessa disciplina di research/patches.
#
# L'albero di QEMU e' estratto da un tarball, non un repo git: "git apply" non
# richiede pero' un repository, solo che i percorsi nel patch corrispondano ai
# file sotto la directory corrente. Verificato qui: funziona.
#
# -c core.autocrlf=false su OGNI "git apply" qui sotto, scoperto mentre si
# preparava la patch qemu-force-ctx0-selettivo: questa macchina ha
# core.autocrlf=true a livello SYSTEM (C:/Program Files/Git/etc/gitconfig,
# il default dell'installer di Git per Windows), che si applica anche fuori
# da un repo, e "git apply" lo rispetta -- non riscrive solo le righe toccate
# dalla patch, RICOSTRUISCE l'intero file e lo riscrive con CRLF su ogni riga.
# Verificato: un'applicazione pulita di una patch su una copia LF di
# virtio-gpu-virgl.c produce un file di dimensione maggiore di esattamente
# una riga per riga aggiunta (ogni "\n" diventa "\r\n"), qui e sotto
# $SRC/msys64/home/... indifferentemente -- non e' un mount particolare, e'
# la config di git. I file di questo albero sono LF (estratti da un
# tarball); senza l'override, la PRIMA applicazione vera su un albero
# fresco corrompe silenziosamente i terminatori di riga di ogni file
# toccato (compila comunque: clang non si lamenta di un CR di troppo prima
# di un LF), e i controlli idempotenti sotto smettono di essere affidabili
# perche' confrontano contro un file che nessuna patch ha mai scritto con
# quei terminatori. -c e' un override PER COMANDO, non tocca il file di
# configurazione di nessuno.
#
# "git" non c'e' nell'ambiente MSYS2 CLANGARM64 (verificato: solo git-clang-format,
# non git stesso). Copia e patch girano quindi con qualunque bash le esegua
# (compresa Git Bash, che porta sempre il proprio git); solo la compilazione vera
# e propria, che richiede clang/ninja della cross-toolchain, si rilancia
# esplicitamente dentro MSYS2 CLANGARM64, cosi' lo script funziona a prescindere
# da quale shell l'ha invocato.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"

# DOVE STA L'ALBERO DI QEMU, e perche' "$HOME/qemu-11.0.3" da solo non bastava.
#
# Questo script vuole git, quindi va lanciato da Git Bash -- lo dice il commento
# in testa, e lo dice qemu/HOST-WINDOW.md. Ma l'albero vive nella home di
# MSYS2, perche' e' li' che build-qemu.sh lo estrae e lo compila, e le due home
# NON sono la stessa cartella:
#
#     Git Bash    $HOME = /c/Users/<utente>
#     MSYS2       $HOME = /c/msys64/home/<utente>
#
# Con il solo $HOME lo script si fermava quindi con "albero QEMU assente"
# ESEGUENDOLO ESATTAMENTE COME LA DOCUMENTAZIONE DICE DI ESEGUIRLO. Funzionava
# solo lanciandolo da dentro MSYS2, che e' il caso che la sua stessa intestazione
# dichiara di non voler richiedere.
#
# Si provano i posti plausibili in ordine, e si DICE quale e' stato scelto: un
# innesto che sceglie in silenzio fra due alberi e' un innesto che un giorno
# compila quello sbagliato e non lo fa sapere a nessuno.
#
# QEMU_SRC, se c'e', NON e' un candidato: e' un ordine. Se punta a una cartella
# che non esiste si fallisce li', invece di ripiegare su un altro albero -- una
# richiesta esplicita che non si puo' onorare deve fermarsi, o si finirebbe a
# compilare un albero diverso da quello chiesto credendo di aver ubbidito.
UTENTE="$(id -un)"
if [ -n "${QEMU_SRC:-}" ]; then
    if [ ! -d "$QEMU_SRC" ]; then
        echo "FERMO: QEMU_SRC punta a $QEMU_SRC, che non esiste."
        echo "Togliere QEMU_SRC per far cercare gli alberi soliti."
        exit 1
    fi
    echo "=== albero QEMU: $QEMU_SRC (imposto da QEMU_SRC)"
    SRC="$QEMU_SRC"
fi

CANDIDATI=("$HOME/qemu-11.0.3" "/c/msys64/home/$UTENTE/qemu-11.0.3")

SRC="${SRC:-}"
if [ -z "$SRC" ]; then
    for c in "${CANDIDATI[@]}"; do
        if [ -d "$c" ]; then
            SRC="$c"
            break
        fi
    done
    if [ -z "$SRC" ]; then
        echo "FERMO: albero QEMU non trovato. Cercato, in ordine, in:"
        for c in "${CANDIDATI[@]}"; do
            echo "    $c"
        done
        echo "Se sta altrove:  QEMU_SRC=<percorso> bash $0"
        exit 1
    fi
    echo "=== albero QEMU: $SRC"
fi

command -v git >/dev/null 2>&1 || { echo "FERMO: serve git per applicare la patch (usare Git Bash)"; exit 1; }

# I NOSTRI SORGENTI SI COPIANO TUTTI, non per elenco.
#
# Qui c'era una lista scritta a mano con l'invito "i file dei task successivi si
# aggiungono qui quando esistono". Non e' stato fatto: winq-rotella.c e
# winq-rotella.h esistono nel repo, ui/meson.build li pretende (la patch di build
# li elenca), e la lista non li copiava. Invisibile sull'albero innestato, dove il
# file c'e' da una copia a mano di allora; fatale su un albero vergine, dove
# configure si ferma con "File winq-rotella.c does not exist" -- trovato il
# provando per la prima volta la ricostruzione da tarball.
#
# La glob winq* esclude da se' i test (test-winq-*.c) e i loro .exe, che nel
# prodotto non entrano. Un elenco si dimentica; una glob no, ed e' la stessa
# ragione per cui piu' sotto i marcatori si contano invece di essere scritti.
COPIATI=0
for f in "$HERE"/ui-winq/winq*.c "$HERE"/ui-winq/winq*.h; do
    [ -e "$f" ] || continue
    cp "$f" "$SRC/ui/"
    COPIATI=$((COPIATI + 1))
done
echo "=== $COPIATI sorgenti winq copiati in ui/"

# E CIO' CHE meson PRETENDE DEVE ESSERCI, verificato prima di arrivare a
# configure: l'errore di meson arriva dopo minuti di risoluzione delle
# dipendenze e nomina un file per volta, quindi con tre file mancanti si
# scoprono in tre giri. Qui si dicono tutti insieme, subito.
ATTESI=$(grep -oE "winq-[a-z0-9-]+\.c" "$HERE/patches/qemu-aggiungi-winq.patch" | sort -u)
ASSENTI=""
for f in $ATTESI; do
    [ -f "$SRC/ui/$f" ] || ASSENTI="$ASSENTI $f"
done
if [ -n "$ASSENTI" ]; then
    echo "FERMO: ui/meson.build pretende file che non sono in $SRC/ui:$ASSENTI"
    echo "       Stanno in qemu/ui-winq/ del repo? Se si', la glob qui sopra non"
    echo "       li prende (nome fuori schema). Se no, il sorgente non e'"
    echo "       versionato e va recuperato prima di compilare."
    exit 1
fi

cd "$SRC"
GIT="git -c core.autocrlf=false"

# COME SI CAPISCE SE UNA PATCH E' GIA' DENTRO, e perche' non piu' con
# "git apply --reverse --check".
#
# Quel controllo chiede "questa patch e' invertibile?", che NON e' la domanda.
# La domanda e' "questa modifica e' nell'albero?". Le due coincidono solo se
# nessun'altra patch ha toccato il contesto attorno -- e qui cinque delle sei
# toccano lo stesso hw/display/virtio-gpu-virgl.c, quindi si calpestano il
# contesto a vicenda. Misurato sull'albero innestato e
# funzionante:
#
#   patch                       patch -R (=dentro)   git apply -R
#   aggiungi-winq               OK                   NO
#   diagnostica-gpu-tempo       NO                   NO
#   diagnostica-risorse         NO                   NO
#   diagnostica-virtio-snd      NO                   NO
#   fasi-submit3d               OK                   OK
#   force-ctx0-selettivo        NO                   NO
#
# Quattro su sei non sono ne' invertibili ne' applicabili: sono dentro, e
# nessuna prova basata sulle patch lo sa dire. Con set -e la PRIMA di queste
# fermava lo script, che quindi non arrivava piu' a compilare: e' il guasto per
# cui questa parte e' stata riscritta.
#
# Il marcatore e' una stringa che esiste SOLO dopo quella patch. E' una prova
# piu' debole in teoria -- non dice che la patch e' applicata TUTTA -- e piu'
# forte in pratica, perche' risponde alla domanda giusta e non si rompe quando
# una patch vicina cambia il contesto.
#
# La tabella qui sopra e' di PRIMA della rigenerazione : le patch di
# oggi si applicano tutte in ordine su un albero vergine (vedi il blocco in fondo
# a questo script), quindi su un albero vergine anche git apply --reverse
# tornerebbe a funzionare. Il marcatore resta comunque la prova giusta, perche'
# l'albero su cui questo script gira di solito NON e' vergine: e' innestato, e
# la' le patch continuano a calpestarsi il contesto a vicenda.
applicata() {   # $1 = marcatore, $2 = file relativo all'albero
    grep -qF "$1" "$SRC/$2"
}

innesta() {     # $1 = nome del file patch, $2 = marcatore, $3 = file, $4 = etichetta
    if applicata "$2" "$3"; then
        echo "=== $4: gia' dentro"
        return 0
    fi
    if ! $GIT apply "$HERE/patches/$1"; then
        echo "FERMO: $1 non si applica, e il suo marcatore non c'e' in $3."
        echo "       Questo albero non e' ne' vergine ne' innestato: e' a meta',"
        echo "       ed e' lo stato in cui nessuna patch sa piu' dove mettersi."
        echo "       La via e' ripartire dal tarball -- da oggi l'insieme delle"
        echo "       patch lo ricostruisce, vedi il blocco in fondo a questo"
        echo "       script -- oppure duplicare un albero innestato che compila."
        exit 1
    fi
    echo "=== $4: applicata"
}

innesta qemu-aggiungi-winq.patch "{ 'name': 'winq' }" qapi/ui.json \
        "patch di build"

# PRIMA di diagnostica-gpu-tempo, e non per gusto: e' questa che introduce il
# blocco WINQ_GPU_SENZA_BQL che il contesto di quella si aspetta. Era
# dimenticata del tutto -- stesso buco documentato sotto per la diagnostica
# delle risorse -- e vale piu' delle altre: porta le callback asincrone delle
# fence (VIRGL_RENDERER_ASYNC_FENCE_CB, THREAD_SYNC, WINQ_FENCE_SINCRONE), che
# valgono 572 ms/s di ciclo principale misurati. Un innesto senza di lei
# tornerebbe al ritiro sincrono in silenzio.
innesta qemu-diagnostica-virtio-snd.patch WINQ_FENCE_SINCRONE hw/display/virtio-gpu-virgl.c \
        "diagnostica virtio-snd e fence asincrone"

innesta qemu-diagnostica-gpu-tempo.patch WINQ_CTX_FORCE0 hw/display/virtio-gpu-virgl.c \
        "diagnostica del tempo GPU"

# Correzione funzionale, non diagnostica: NON sta insieme ai contatori nello
# stesso file di patch (vincolo esplicito). virgl_renderer_force_ctx_0() era
# chiamata incondizionatamente prima di ogni comando; ora e' selettiva per
# tipo, con un elenco di esclusione la cui sicurezza sta nel default
# (chiamare), non nella lista -- vedi il commento sopra winq_ctx0_serve() in
# hw/display/virtio-gpu-virgl.c.
innesta qemu-force-ctx0-selettivo.patch winq_ctx0_serve hw/display/virtio-gpu-virgl.c \
        "chiamata selettiva a force_ctx_0"

# MANCAVA, e il buco era reale: questa patch era applicata a mano nell'albero
# ma non elencata qui, quindi un innesto su un albero pulito non l'avrebbe mai
# messa e la riga winq-res/s sarebbe sparita senza che nessuno se ne accorgesse
# -- si sarebbe letto "nessun ricambio di risorse" da una diagnostica assente.
# Trovato mentre si aggiungeva la patch delle fasi, che dipende
# da questa (tocca il getenv dentro winq_res_conta): l'ordine qui sotto non e'
# estetico, e' un vincolo.
innesta qemu-diagnostica-risorse.patch WINQ_RES_MAX hw/display/virtio-gpu-virgl.c \
        "diagnostica delle risorse"

# Le fasi dentro il gestore di SUBMIT_3D, e i getenv caldi memorizzati.
# DIPENDE dalla patch delle risorse qui sopra (tocca il suo getenv) e da
# quella di force_ctx_0 (cronometra la chiamata che quella rende selettiva):
# va per ultima. Vedi il commento sopra l'enum WINQ_FASE_* in
# hw/display/virtio-gpu-virgl.c per cosa misura e perche' le fasi si annidano.
innesta qemu-fasi-submit3d.patch WINQ_FASE_CODA hw/display/virtio-gpu-virgl.c \
        "fasi di SUBMIT_3D"

# WINQ_SUBMIT_SENZA_CTX0: esisteva SOLO nell'albero, in nessuna patch. Trovata il
# rigenerando l'insieme: 47 righe -- l'interruttore piu' il suo
# commento -- che ricostruendo da vergine mancavano. Se quell'albero si fosse
# perso, quel codice era perso con lui, e nessuno se ne sarebbe accorto fino a
# quando qualcuno avesse riletto l'esperimento dell'11/08 nel commento che non
# c'era piu'. Va per ultima perche' e' stata catturata come differenza rispetto
# alle sei precedenti.
innesta qemu-submit-senza-ctx0.patch WINQ_SUBMIT_SENZA_CTX0 hw/display/virtio-gpu-virgl.c \
        "interruttore WINQ_SUBMIT_SENZA_CTX0"

# Diagnostica del ciclo WHPX, nata dall'issue #1: ogni 10 s una riga per vCPU
# con uscite, tempo dentro/fuori dall'hypervisor e pagine MMIO piu' battute.
# Tocca un file suo (target/arm/whpx/whpx-all.c), quindi non calpesta il
# contesto di nessuna delle patch qui sopra. Si spegne con WINQ_WHPX_STAT=0.
innesta qemu-diagnostica-whpx.patch winq_whpx_stat target/arm/whpx/whpx-all.c \
        "diagnostica del ciclo WHPX"

# La riparazione dell'issue #1: le feature sintetiche Hyper-V concesse di
# default alla partizione. Sul Surface Pro 12 (X1P-42) la sola concessione
# porta il boot da 81-130 minuti a 2 -- il perche' per esteso sta nel
# commento della patch. WINQ_HV_SINTETICI=0 la spegne a runtime.
innesta qemu-hv-sintetici.patch winq_hv_sintetici target/arm/whpx/whpx-all.c \
        "feature sintetiche Hyper-V di default"

# CANCELLO PRIMA DI COMPILARE: tutti e otto i marcatori, o non si compila.
#
# Serve perche' il guasto che questo script ha avuto per due giorni era SILENZIOSO
# nell'altra direzione: una patch dimenticata nell'elenco (e' successo due volte
# -- diagnostica-risorse, diagnostica-virtio-snd ) produce un
# binario che compila, gira, e ha una diagnostica in meno o il ritiro fence
# sincrono. Nessun errore, solo numeri diversi che poi si attribuiscono a
# qualcos'altro. Un elenco si dimentica; un cancello no.
MANCANTI=""
QUANTI=0
# Il conteggio si conta, non si scrive: la riga diceva "sei su sei" il giorno in
# cui i marcatori sono diventati sette, e una diagnostica che mente sul proprio
# numero e' peggio che non averla.
verifica_marcatore() {   # $1 = marcatore, $2 = file, $3 = etichetta
    QUANTI=$((QUANTI + 1))
    applicata "$1" "$2" || MANCANTI="$MANCANTI
    - $3 (marcatore '$1' assente in $2)"
}
verifica_marcatore "{ 'name': 'winq' }" qapi/ui.json                      "patch di build"
verifica_marcatore WINQ_FENCE_SINCRONE hw/display/virtio-gpu-virgl.c      "virtio-snd e fence asincrone"
verifica_marcatore WINQ_CTX_FORCE0     hw/display/virtio-gpu-virgl.c      "diagnostica del tempo GPU"
verifica_marcatore winq_ctx0_serve     hw/display/virtio-gpu-virgl.c      "force_ctx_0 selettiva"
verifica_marcatore WINQ_RES_MAX        hw/display/virtio-gpu-virgl.c      "diagnostica delle risorse"
verifica_marcatore WINQ_FASE_CODA      hw/display/virtio-gpu-virgl.c      "fasi di SUBMIT_3D"
verifica_marcatore WINQ_SUBMIT_SENZA_CTX0 hw/display/virtio-gpu-virgl.c   "interruttore WINQ_SUBMIT_SENZA_CTX0"
verifica_marcatore winq_whpx_stat      target/arm/whpx/whpx-all.c         "diagnostica del ciclo WHPX"
verifica_marcatore winq_hv_sintetici   target/arm/whpx/whpx-all.c         "feature sintetiche Hyper-V"
if [ -n "$MANCANTI" ]; then
    echo "FERMO: l'albero non ha tutte le nostre modifiche. Manca:$MANCANTI"
    echo "    Non si compila: uscirebbe un binario che gira e misura un'altra cosa."
    exit 1
fi
echo "=== $QUANTI marcatori su $QUANTI presenti"

# L'INSIEME RICOSTRUISCE L'ALBERO, e da oggi e' verificato -- prima non era vero.
#
# Fino al queste patch erano state catturate in momenti diversi contro
# alberi via via piu' innestati, quindi il contesto di una conteneva righe
# aggiunte da un'altra. Misurato allora: nessuna delle 120 permutazioni le
# applicava tutte e sei su un albero vergine, e due modificavano lo stesso blocco
# WINQ_GPU_SENZA_BQL in modi incompatibili. L'albero era l'unica copia da cui i
# binari nascevano: si duplicava, non si ricostruiva.
#
# Rigenerate lo stesso giorno come storia LINEARE: applicate in quest'ordine con
# GNU patch a tolleranza (che ha il fuzz, git apply no) e ricatturate una per una
# come diff esatto. Adesso si applicano tutte e sette con "git apply", senza
# tolleranza, nell'ordine in cui compaiono qui sopra -- e quell'ordine e' un
# VINCOLO, non una preferenza: le patch si toccano il contesto a vicenda.
#
# ACCETTAZIONE, che non e' "si applicano" ma "ricostruiscono cio' che compila":
# partendo dal tarball, sei dei sette file toccati risultano IDENTICI all'albero
# innestato. Il settimo (audio/audio-mixeng-be.c) differisce per due righe di
# commento in cui la patch e' piu' aggiornata dell'albero (app/guscio/vm.c contro
# il vecchio phase3/guscio/vm.c): il ricostruito e' piu' corretto dell'originale.
#
# E' cosi' che si e' scoperto WINQ_SUBMIT_SENZA_CTX0, 47 righe che vivevano solo
# nell'albero. La rigenerazione non e' stata pulizia: ha trovato codice che il
# repo non aveva.
#
# COME SI RIFA', se un giorno una patch va ricatturata: script in
# scratchpad/rigenera.sh di quella sessione, o a mano -- albero vergine dal
# tarball, applicare in ordine con "patch -p1 --fuzz=3", e a ogni passo
# "diff -u --label a/<file> --label b/<file>" fra il prima e il dopo. La prova
# finale e' il confronto con l'albero che compila, terminatori normalizzati:
# l'unico file che nell'albero innestato ha i terminatori corrotti (CRLF) e'
# qapi/ui.json, danno storico della prima applicazione fatta senza
# -c core.autocrlf=false.

# SOLO_INNESTO=1 si ferma qui, senza compilare.
#
# Serve per l'unico caso in cui la compilazione non si puo' ancora fare: un
# albero appena estratto dal tarball non ha build/build.ninja, quindi va prima
# innestato (le patch toccano qapi/ui.json e ui/meson.build, che configure
# legge), POI configurato, e solo dopo compilato. Senza questa uscita, provare
# la ricostruzione da vergine finiva con un ninja che fallisce su una cartella
# che non esiste -- un errore che non significa niente e nasconde l'esito vero
# dell'innesto.
if [ -n "${SOLO_INNESTO:-}" ]; then
    echo "=== SOLO_INNESTO: mi fermo prima di compilare"
    exit 0
fi

# ninja/clang per aarch64-w64-windows-gnu vivono solo dentro MSYS2 CLANGARM64
# (vedi qemu/scripts/build-qemu.sh): ci si rientra esplicitamente, non si
# assume che la shell che ha lanciato questo script li abbia gia' in PATH.
JOBS="${JOBS:-10}"
MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc \
    "cd '$SRC' && ninja -C build qemu-system-aarch64.exe -j $JOBS"
echo "=== WINQ-COMPILATO"
