#!/bin/bash
# app/scripts/confeziona-release.sh -- confeziona l'archivio da distribuire.
#
# In MSYS2 CLANGARM64, con percorso assoluto (stesso schema di build-guscio.sh
# e test-guscio.sh, per la stessa ragione: quella shell parte dalla propria
# home, non da qui):
#   MSYSTEM=CLANGARM64 /c/msys64/usr/bin/bash.exe -lc "bash <radice-del-progetto>/app/scripts/confeziona-release.sh"
#
# Legge app/release/contenuto.txt (l'elenco misurato) e non decide da se' cosa
# entra nel pacchetto. Cio' che l'elenco
# nomina va COSTRUITO, non raccolto: il guscio e l'initramfs si ricompilano
# qui, cosi' la release e' riproducibile e non un assemblaggio a mano che
# nessuno puo' rifare identico (un Habumi.exe raccolto a caso
# potrebbe essere quello di ieri, con un bug gia' corretto nei sorgenti). Il
# resto dell'elenco -- le DLL di runtime/bin, i dati di QEMU, le immagini che
# il progetto non scarica -- sta gia' sul disco per conto proprio e si copia.
#
# Si ferma nominando il file quando l'elenco chiede qualcosa che non c'e':
# stessa disciplina del FERMO in build-android-initramfs.sh. Un pacchetto
# monco si scopre a valle, da qualcun altro -- meglio non produrlo affatto.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

# --- la versione: UN posto solo, e nessun altro -----------------------------
# Finisce nel nome dell'archivio (sotto). Chi rilascia una versione nuova
# cambia questa riga e nessun'altra.
# 0.x e non 1.0.0, ed e' una scelta: il prodotto funziona ed e' provato, ma non
# e' mai stato installato da nessuno tranne chi lo sviluppa, gira su una macchina
# sola, e porta limiti dichiarati -- niente firma del codice, nessun aggiornamento
# automatico, le licenze delle DLL di MSYS2 da completare. Uno 0.x invita a
# segnalare i difetti; un 1.0 promette una stabilita' che nessuno ha ancora
# potuto smentire.
#
# 0.2.0 e non 0.1.1, perche' cambia cosa c'e' dentro e non solo
# come sta insieme:
#   - si spedisce la NOSTRA Mesa 26.2.0 (runtime/bin/opengl32.dll), che in
#     0.1.0 non c'era affatto: quel pacchetto girava sull'OpenGL di sistema;
#   - virglrenderer porta la correzione della perdita di contesto (patch 0020),
#     quella dei pezzi di grafica che sparivano;
#   - il guscio ha la chiave gl_flush_wait, e il difetto per cui ogni cambio di
#     contesto aspettava la GPU non c'e' piu';
#   - escono dal pacchetto le quattro copie di sviluppo di qemu-nostro.exe che
#     0.1.0 spediva per sbaglio, 464 MB.
#
# 0.2.1: l'interfaccia del guscio e' passata all'inglese. Sedici stringhe, tutte
# quelle visibili -- README.md lo dichiarava gia' e non era vero. Cambia solo
# Habumi.exe, ma cambia cio' che l'utente legge, quindi merita un numero.
# NON e' mai stata pubblicata: superata nel giro di due ore dalla 0.2.2.
#
# 0.2.2, e questa nasce dai primi rapporti su hardware non nostro. Due Surface
# Pro 12" non avviavano, e in un caso la colpa era interamente qui:
#   - il guardiano dell'avvio contava le righe seriali a una scadenza FISSA di
#     sedici secondi. Su una macchina piu' lenta il kernel ne produceva 119 e
#     stava ancora salendo: sano, e lo uccidevamo noi dichiarando per giunta
#     una corsa sui vCPU che non c'entrava. Ora giudica il PROGRESSO, e
#     inchiodato vuol dire otto secondi di silenzio;
#   - i tre tentativi rigiocavano la stessa identica configurazione. Ora
#     DEGRADANO: a zero righe il successivo dimezza i vCPU (6, 3, 1), che e'
#     esattamente cio' che serviva a quelle due macchine;
#   - il bottone della variante era grigio finche' Android non era pronto, cioe'
#     proprio quando serve per provare l'altra immagine. Ora si accende quando
#     l'avvio fallisce.
# In piu': README.md e KNOWN-ISSUES.md dicevano che la macchina di sviluppo e'
# un Snapdragon X Elite. E' un X Plus X1P64100 a 10 core, ed era pubblico.
VERSIONE="0.2.2"
# Il nome del PRODOTTO, non della cartella del progetto: finisce nel nome
# dell'archivio e in quello della cartella di uscita qui sotto.
NOME="Habumi"

CONTENUTO="$ROOT/app/release/contenuto.txt"
README_SRC="$ROOT/app/release/README.txt"
LICENSES_SRC="$ROOT/app/release/LICENSES"

[ -f "$CONTENUTO" ] || { echo "FERMO: manca $CONTENUTO"; exit 1; }
[ -f "$README_SRC" ] || { echo "FERMO: manca $README_SRC"; exit 1; }
[ -d "$LICENSES_SRC" ] || { echo "FERMO: manca $LICENSES_SRC"; exit 1; }

# L'archivio esce a FIANCO dell'albero del progetto, non dentro: un archivio
# da centinaia di MB lasciato sotto ROOT verrebbe raccolto lui stesso dal
# prossimo confezionamento il giorno che qualcuno allargasse la ricerca oltre
# contenuto.txt (per esempio con un "trova tutto cio' che sta sotto runtime/").
DIST_DIR="$(cd "$ROOT/.." && pwd)/${NOME}-dist"
ARCHIVIO="$DIST_DIR/${NOME}-${VERSIONE}.zip"

echo "=== 1. costruzione del guscio (Habumi.exe)"
bash "$ROOT/app/scripts/build-guscio.sh"

echo "=== 2. costruzione dell'initramfs"
# build-android-initramfs.sh vuole un host aarch64 vero: controlla uname -m e
# pretende un busybox statico aarch64. In MSYS2 CLANGARM64 uname -m risponde
# comunque x86_64 -- CLANGARM64 e' solo la toolchain di compilazione, non
# l'architettura del sottosistema POSIX di base di questa installazione MSYS2
# -- quindi quello script si fermerebbe subito qui. Lo si lancia percio' nella
# WSL della macchina, che aarch64 lo e' davvero (vedi la sua intestazione).
# Nessuna variabile WINQ_* impostata: e' il caso di oggi (initramfs verificato
# con la coppia vendor+GSI, non con la coppia Waydroid che vorrebbe
# WINQ_VNDK_LITE=1), e questo copione riproduce quello, non decide altro.
WIN_ROOT="$(cygpath -w "$ROOT")"
WSL_DRIVE="$(printf '%s' "$WIN_ROOT" | cut -c1 | tr '[:upper:]' '[:lower:]')"
WSL_RESTO="$(printf '%s' "$WIN_ROOT" | cut -c3- | tr '\\' '/')"
WSL_ROOT="/mnt/${WSL_DRIVE}${WSL_RESTO}"
# MSYS_NO_PATHCONV: senza, questa stessa shell MSYS riscrive "/mnt/c/..." come
# se fosse un percorso MSYS relativo alla propria installazione (misurato:
# diventava "C:/msys64/mnt/c/..."), perche' "/mnt" non e' uno dei suoi punti
# di mount noti come lo e' "/c". wsl.exe e' un eseguibile nativo Windows, e la
# stringa che gli passiamo va interpretata DENTRO la WSL, non riscritta prima.
MSYS_NO_PATHCONV=1 wsl.exe -- bash "$WSL_ROOT/guest/scripts/build-android-initramfs.sh"

echo "=== 3. composizione dell'albero"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

N_ELENCO=0
while IFS= read -r riga || [ -n "$riga" ]; do
    # Un \r di coda (capita se l'albero di lavoro locale ha gia' convertito
    # questo file, anche se il blob nel commit resta LF) romperebbe il
    # confronto col nome del file: tolto qui, non nel file sorgente.
    riga="${riga%$'\r'}"
    if [ -z "$riga" ]; then
        continue
    fi
    if [ "${riga:0:1}" = "#" ]; then
        continue
    fi
    ORIGINE="$ROOT/$riga"
    if [ ! -f "$ORIGINE" ]; then
        echo "FERMO: manca $ORIGINE (elencato in contenuto.txt)"
        exit 1
    fi
    # La struttura si conserva tale e quale (vedi il controllo piu' sotto sul
    # perche'): DEST ripete lo stesso percorso relativo di ORIGINE.
    DEST="$STAGE/$riga"
    mkdir -p "$(dirname "$DEST")"
    cp "$ORIGINE" "$DEST"
    N_ELENCO=$((N_ELENCO + 1))
done < "$CONTENUTO"
echo "    $N_ELENCO file copiati da contenuto.txt"

# README.txt vuole CRLF nel pacchetto, non LF: lo apre un utente Windows col
# Blocco note, che senza CR mostra tutto il testo su una riga sola. Nel
# repository il file e' LF perche' .gitattributes impone eol=lf a TUTTO senza
# eccezioni -- e va bene cosi', e' l'unico file dell'intero pacchetto che vuole
# l'eccezione, quindi la conversione si fa qui, in uscita, non nel sorgente.
#
# Il primo sed toglie un CR se per caso c'e' gia': core.autocrlf locale puo'
# aver gia' convertito la copia nell'albero di lavoro anche se il blob nel
# commit resta LF (vedi rl-vincoli.md sulla stessa cosa). Senza questo passo,
# ripartire da un file gia' CRLF raddoppierebbe il CR invece di normalizzarlo.
sed 's/\r$//' "$README_SRC" | sed 's/$/\r/' > "$STAGE/README.txt"
cp -r "$LICENSES_SRC" "$STAGE/LICENSES"

# Start.cmd va in RADICE del pacchetto, non nella struttura di contenuto.txt:
# quell'elenco copia conservando i percorsi relativi, e questo file nel
# repository sta sotto app/release/. Si copia percio' esplicitamente, come
# README.txt e LICENSES/.
#
# E in CRLF, che non e' cosmetica: .gitattributes impone eol=lf a TUTTO il
# repository, e un .cmd con fine riga Unix puo' far sbagliare l'interprete dei
# comandi di Windows sulle righe che finiscono con un argomento. Stesso doppio
# sed del README, e per la stessa ragione: il primo toglie un CR se la copia
# nell'albero di lavoro ce l'ha gia' per via di core.autocrlf, senno' il secondo
# lo raddoppierebbe.
sed 's/\r$//' "$ROOT/app/release/Start.cmd" | sed 's/$/\r/' > "$STAGE/Start.cmd"

echo "=== 4. controllo della struttura"
# Il prodotto calcola la propria radice dall'eseguibile (due livelli sopra
# runtime\bin\, guscio_radice_calcola in app/guscio/adb.c) e la VERIFICA
# prima di usarla: senza una radice riconosciuta non parte, ed e' esattamente
# questo il bloccante gia' pagato una volta (il doppio clic che non faceva
# NIENTE). Se l'albero composto qui avesse una struttura diversa da quella
# dell'albero sorgente -- un bug di questo stesso copione, non dell'elenco --
# quella verifica fallirebbe per OGNI utente che scompatta il pacchetto.
# Meglio scoprirlo adesso, con un nome di file davanti, che dopo la
# distribuzione.
if [ ! -f "$STAGE/runtime/bin/Habumi.exe" ]; then
    echo "FERMO: l'albero composto non ha runtime/bin/Habumi.exe"
    exit 1
fi
if [ ! -d "$STAGE/guest/images" ]; then
    echo "FERMO: l'albero composto non ha la cartella guest/images"
    exit 1
fi
# Start.cmd e' l'unica cosa che dice a chi scompatta dove cliccare: senza, la
# radice mostra README.txt, LICENSES, guest e runtime, e l'eseguibile sta due
# livelli sotto. Un pacchetto che lo perde non e' rotto, e' MUTO -- che e'
# peggio, perche' non se ne accorge nessuno finche' non se ne lamenta qualcuno.
if [ ! -f "$STAGE/Start.cmd" ]; then
    echo "FERMO: l'albero composto non ha Start.cmd in radice"
    exit 1
fi

echo "=== 4b. cose che non devono uscire"
# Sull'albero COMPOSTO, prima di comprimerlo: un archivio gia' fatto lo si
# controlla solo se qualcuno si ricorda di farlo, e l'archivio e' il pezzo
# che finisce in mano ad altri. Elenco e ragioni in app/release/vietati.sh,
# gli stessi che usa esporta-repo-pubblico.sh.
# FACOLTATIVO, e non per pigrizia: vietati.sh NON viene pubblicato -- contiene
# l'elenco delle stringhe vietate, cioe' proprio quelle -- mentre QUESTO copione
# si'. Con un source obbligatorio, chiunque clonasse il repository e lanciasse
# il confezionamento si fermerebbe qui con "No such file or directory", perche'
# set -e non perdona. Il controllo serve a chi pubblica DA QUI; per un fork non
# ha senso, e la sua assenza non deve impedire di costruire l'archivio.
if [ -f "$ROOT/app/release/vietati.sh" ]; then
    . "$ROOT/app/release/vietati.sh"
    if ! controlla_vietati "$STAGE"; then
        echo "       L'archivio NON e' stato prodotto."
        exit 1
    fi
else
    echo "    (saltato: app/release/vietati.sh non c'e', ed e' normale in un fork)"
fi

N_TOTALE=$(find "$STAGE" -type f | wc -l)

echo "=== 5. archivio"
mkdir -p "$DIST_DIR"
rm -f "$ARCHIVIO"
# tar.exe di Windows (bsdtar) sa scrivere zip con --format zip: e' la stessa
# dipendenza che README.txt dichiara gia' per il prodotto stesso (lo usa per
# estrarre vendor.img e system.img al primo avvio), quindi confezionare la
# release non ne aggiunge una nuova. L'elenco esplicito delle voci di primo
# livello (invece di ".") evita che ogni nome finisca nello zip prefissato da
# "./": chi lo apre vede "runtime/" e "guest/" in radice, non "./runtime/" --
# ed e' proprio in radice che li vuole la verifica del prodotto descritta sopra.
VOCI=()
for voce in "$STAGE"/*; do
    VOCI+=("$(basename "$voce")")
done
( cd "$STAGE" && /c/Windows/System32/tar.exe --format zip -cf "$ARCHIVIO" "${VOCI[@]}" )

# Il conto si rilegge DALL'ARCHIVIO, non dall'albero composto. Stampare
# N_TOTALE e basta sarebbe dire "ho messo dentro 231 file" avendo contato
# quelli che volevo mettere: se tar ne perdesse uno, il numero stampato
# mentirebbe, e mentirebbe proprio nella riga che qualcuno userebbe per
# credere che il pacchetto sia completo. Le voci di cartella (che finiscono
# con "/") non sono file e non si contano.
N_ARCHIVIO=$(/c/Windows/System32/tar.exe -tf "$ARCHIVIO" | grep -vc '/$' || true)
if [ "$N_ARCHIVIO" != "$N_TOTALE" ]; then
    echo "FERMO: composti $N_TOTALE file ma nell'archivio ce ne sono $N_ARCHIVIO"
    exit 1
fi

echo "=== risultato"
echo "archivio: $ARCHIVIO"
ls -l "$ARCHIVIO"
echo "file    : $N_ARCHIVIO (riletti dall'archivio, non dall'albero)"
sha256sum "$ARCHIVIO"
