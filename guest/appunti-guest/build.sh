#!/bin/bash
# guest/appunti-guest/build.sh -- costruisce HabumiClipboard.apk.
#
# Da MSYS2 o Git Bash, con percorso assoluto:
#   bash <radice>/guest/appunti-guest/build.sh
#
# NIENTE GRADLE. La catena e' quella nuda dell'SDK:
#   aapt2 link   il manifesto diventa un APK senza codice
#   javac        i .java diventano .class (JDK 25 con --release 11)
#   d8           i .class diventano classes.dex
#   aapt add     classes.dex entra nell'APK -- d8 non sa aggiungere a uno zip
#   zipalign     allineamento a 4, prima della firma
#   apksigner    firma di piattaforma
#
# TRE COSE COSTATE TEMPO, scritte perche' non si ripaghino:
#
# 1. java sul PATH e' una JRE 8. d8 e apksigner sono compilati per una JVM
#    moderna e falliscono con UnsupportedClassVersionError. Per quei due passi
#    si impone JAVA_HOME al JDK 25; javac lo si chiama per percorso assoluto.
# 2. javac 25 non accetta piu' -bootclasspath. Al suo posto --release 11 con
#    android.jar sul classpath: le classi java.* vengono dalla piattaforma,
#    quelle android.* dal jar.
# 3. La costante Manifest.permission.READ_CLIPBOARD_IN_BACKGROUND non esiste
#    nell'android.jar pubblico (il permesso e' signature|role e resta fuori
#    dall'SDK). Nel manifesto si scrive la stringa a mano, e nel codice non
#    serve mai nominarla.
set -euo pipefail

CARTELLA="$(cd "$(dirname "$0")" && pwd)"
# I passi Java sono programmi nativi di Windows e non sanno cosa sia /c/... .
# pwd -W da' lo stesso percorso in stile Windows con le barre in avanti, che
# vanno bene sia per Windows sia per le virgolette di bash.
CARTELLA_W="$(cd "$(dirname "$0")" && pwd -W)"

# SDK: si passa in ANDROID_SDK, oppure si cerca dove lo mette Android Studio.
SDK="${ANDROID_SDK:-$(cygpath -m "${LOCALAPPDATA:-$HOME}" 2>/dev/null || echo "$HOME")/Android/Sdk}"
BT="$SDK/build-tools/36.1.0"
ANDROID_JAR="$SDK/platforms/android-34/android.jar"
JDK="${JDK25:-C:/Program Files/Java/jdk-25.0.2}"

CHIAVI="$CARTELLA/chiavi"
CHIAVI_W="$CARTELLA_W/chiavi"
COSTR="$CARTELLA/build"
COSTR_W="$CARTELLA_W/build"
APK="$COSTR/HabumiClipboard.apk"
APK_W="$COSTR_W/HabumiClipboard.apk"

# L'impronta della testkey di piattaforma di AOSP, la stessa con cui e' firmata
# framework-res.apk dentro system.img. Se la firma non da' questa, l'app non
# ottiene READ_CLIPBOARD_IN_BACKGROUND e l'intera funzione e' inutile: per
# questo il controllo sta dentro la build e non nelle istruzioni a parte.
IMPRONTA_ATTESA=c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8

echo "== 0. controllo degli attrezzi"
for f in "$BT/aapt2.exe" "$BT/aapt.exe" "$BT/d8.bat" "$BT/zipalign.exe" \
         "$BT/apksigner.bat" "$ANDROID_JAR" "$JDK/bin/javac.exe"; do
    [ -f "$f" ] || { echo "manca: $f"; exit 1; }
done

if [ ! -f "$CHIAVI/platform.pk8" ] || [ ! -f "$CHIAVI/platform.x509.pem" ]; then
    echo "MANCANO LE CHIAVI DI FIRMA."
    echo "Servono questi due file:"
    echo "    $CHIAVI/platform.pk8"
    echo "    $CHIAVI/platform.x509.pem"
    echo
    echo "Sono la testkey di piattaforma di AOSP, che si prende da"
    echo "build/target/product/security/ dell'albero AOSP. La cartella chiavi/"
    echo "e' in .gitignore: una chiave privata non si committa, nemmeno quando"
    echo "e' pubblica a monte."
    exit 1
fi

echo "== 1. pulizia"
rm -rf "$COSTR"
mkdir -p "$COSTR/classes"

echo "== 2. aapt2 link: il manifesto diventa un APK senza codice"
"$BT/aapt2.exe" link \
    --manifest "$CARTELLA_W/AndroidManifest.xml" \
    -I "$ANDROID_JAR" \
    -o "$COSTR_W/base.apk"

echo "== 3. javac (JDK 25, --release 11)"
# find e non un elenco a mano: aggiungere una classe non deve richiedere di
# ricordarsi di toccare anche questo script.
SORGENTI=$(cd "$CARTELLA" && find src -name '*.java' | sed "s|^|$CARTELLA_W/|")
"$JDK/bin/javac.exe" \
    --release 11 \
    -Xlint:all \
    -classpath "$ANDROID_JAR" \
    -d "$COSTR_W/classes" \
    $SORGENTI

echo "== 4. d8: i .class diventano classes.dex"
CLASSI=$(cd "$COSTR" && find classes -name '*.class' | sed "s|^|$COSTR_W/|")
JAVA_HOME="$JDK" "$BT/d8.bat" \
    --release \
    --min-api 33 \
    --lib "$ANDROID_JAR" \
    --output "$COSTR_W" \
    $CLASSI

echo "== 5. aapt add: classes.dex entra nell'APK"
# Il cd non e' un dettaglio: aapt add usa il percorso cosi' com'e' dato come
# nome della voce nello zip, e una voce build/classes.dex non verrebbe mai
# caricata dal runtime.
(cd "$COSTR" && "$BT/aapt.exe" add -f "$COSTR_W/base.apk" classes.dex >/dev/null)

echo "== 6. zipalign"
"$BT/zipalign.exe" -p -f 4 "$COSTR_W/base.apk" "$COSTR_W/allineato.apk"

echo "== 7. apksigner: firma di piattaforma"
JAVA_HOME="$JDK" "$BT/apksigner.bat" sign \
    --key "$CHIAVI_W/platform.pk8" \
    --cert "$CHIAVI_W/platform.x509.pem" \
    --min-sdk-version 33 \
    --out "$APK_W" \
    "$COSTR_W/allineato.apk"

echo "== 8. verifica della firma"
USCITA=$(JAVA_HOME="$JDK" "$BT/apksigner.bat" verify --print-certs "$APK_W")
echo "$USCITA"
if ! echo "$USCITA" | grep -qi "$IMPRONTA_ATTESA"; then
    echo
    echo "IMPRONTA SBAGLIATA. Attesa: $IMPRONTA_ATTESA"
    echo "L'APK non e' firmato con la chiave di piattaforma dell'immagine:"
    echo "READ_CLIPBOARD_IN_BACKGROUND non verra' concesso e l'app leggera'"
    echo "appunti vuoti senza dire perche'."
    exit 1
fi

echo
echo "FATTO: $APK"
