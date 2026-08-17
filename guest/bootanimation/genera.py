#!/usr/bin/env python3
# guest/bootanimation/genera.py -- genera bootanimation.zip per Habumi.
#
# PERCHE' UNO SCRIPT E NON SOLO UN FILE BINARIO NEL REPO. Un'animazione e' una
# settantina di immagini: committarle renderebbe illeggibile ogni diff e
# impossibile cambiare un parametro senza rifare tutto a mano. Il video e' la
# sorgente, lo zip e' l'artefatto -- come per il resto del progetto, dove le
# immagini si ricostruiscono e non si custodiscono.
#
# IL FORMATO, e le due trappole che ci stanno dentro:
#
#   1. LO ZIP DEVE ESSERE "STORED", non compresso. Android mappa i frame in
#      memoria e non li decomprime: uno zip DEFLATE produce un avvio con lo
#      schermo nero e nessun errore da nessuna parte.
#   2. desc.txt vuole i fine riga UNIX e va per PRIMO nello zip. La prima riga e'
#      "<larghezza> <altezza> <fps>", poi una riga per parte:
#         p <quante volte> <pausa in frame> <cartella>
#      con "quante volte" a 0 che significa "in eterno".
#
# LE DUE SCELTE CHE COSTANO, e i numeri per cui sono state fatte:
#
#   3. I FRAME SONO JPEG, NON PNG. Lo zip finisce nell'initramfs, che sta TUTTO
#      in RAM, e i JPEG non si comprimono: cio' che pesa qui pesa in memoria per
#      tutta la vita della VM. PNG lossless a 1280x720 fa 684 KB per frame, cioe'
#      49 MB di zip, e non e' un prezzo pagabile per un'animazione d'avvio.
#      Il decodificatore e' ImageDecoder di AOSP, che il JPEG lo prende; se un
#      giorno NON lo prendesse il sintomo e' schermo nero muto, e il ripiego e'
#      PNG a 640x360 (~12 MB), non PNG a piena risoluzione.
#      Per lo stesso motivo il JPEG resta BASELINE: il progressivo lo scrive PIL
#      solo se glielo si chiede, e non si chiede a un decodificatore d'avvio.
#   3b. QUALITA' 85 IN 4:2:0, e il perche' e' una misura sui 72 frame VERI (non
#      su un campione: un campione di tre frame sottostima di quasi la meta',
#      perche' i frame scuri comprimono molto meglio della media). Zip completo:
#         4:4:4  q95 17,6  q90 12,0  q85 9,6  q80 8,0  q75 7,0  MB
#         4:2:0  q95 14,4  q90 10,0  q85 8,0  q80 6,8  q75 6,0  MB
#      q85 in 4:2:0 e' il ginocchio: sotto si risparmia poco e si sporca molto,
#      e cio' che si sporca sono i gradienti scuri e i bagliori, cioe' esattamente
#      di cosa e' fatto questo video. Il sottocampionamento e' scritto esplicito
#      perche' il default di PIL non e' garantito fra le versioni, e cambiarlo
#      sposta il peso dello zip di due megabyte senza toccare una riga di codice.
#   4. LA CODA SFUMA SUL PRIMO FRAME. Il video non e' ciclico e bootanimation lo
#      ripete in eterno: MISURATO, fra ultimo e primo frame la differenza media
#      di luminanza e' 41,9, contro 7,5 fra due frame consecutivi a meta' video.
#      Cinque volte e mezzo, cioe' uno stacco ben visibile ogni sei secondi per
#      i venti-trenta secondi dell'avvio. Gli ultimi CODA frame vengono mescolati
#      verso il frame 0 con peso crescente, e l'anello si chiude senza aggiungere
#      un solo frame -- quindi senza aggiungere un byte di RAM.
#
# PERCHE' IL MESCOLAMENTO AVVIENE SUI PNG. ffmpeg estrae in PNG lossless in una
# cartella temporanea e il JPEG si scrive UNA volta sola, alla fine: mescolare
# JPEG gia' compressi vorrebbe dire ricomprimere quei frame due volte, e la coda
# sfumata e' proprio la parte con i gradienti piu' delicati.
#
# Uso:  python guest/bootanimation/genera.py [percorso-di-uscita] [video]
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

from PIL import Image

# 12 fps e non i 24 del video: dimezza i frame, quindi dimezza la RAM, e su
# un'animazione fatta di bagliori la differenza non si vede.
FPS = 12
QUALITA = 85
# 2 = 4:2:0. Esplicito di proposito: vedi la nota 3b in testa al file.
SOTTOCAMPIONAMENTO = 2
CODA = 10

VIDEO = "Habumi_serpent_glowing_on_circuit_202608171755.mp4"


def estrai(video, dove):
    """Tira fuori i frame in PNG a FPS, alla risoluzione nativa del video.

    Nessuna scalatura: il guest gira a 2560x1600 e ogni pixel buttato qui e' un
    pixel che Android deve inventarsi in upscale."""
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        sys.exit("FERMO: manca ffmpeg nel PATH (serve a estrarre i frame dal video)")
    subprocess.run(
        [ffmpeg, "-v", "error", "-y", "-i", video,
         "-vf", "fps=%d" % FPS, "-f", "image2",
         os.path.join(dove, "grezzo%04d.png")],
        check=True)
    # ffmpeg numera da 1; l'ordine alfabetico e' quello giusto perche' il campo
    # e' a larghezza fissa.
    frame = sorted(f for f in os.listdir(dove) if f.startswith("grezzo"))
    if len(frame) < 2 * CODA:
        sys.exit("FERMO: estratti %d frame, troppo pochi per una coda di %d"
                 % (len(frame), CODA))
    return [os.path.join(dove, f) for f in frame]


def chiudi_anello(frame):
    """Mescola gli ultimi CODA frame verso il primo, con peso crescente.

    Il peso arriva a CODA/(CODA+1) e non a 1: l'ultimo frame resta distinto dal
    primo, altrimenti il ciclo mostrerebbe due volte di fila la stessa
    immagine."""
    primo = Image.open(frame[0]).convert("RGB")
    for j in range(CODA):
        i = len(frame) - CODA + j
        peso = (j + 1) / (CODA + 1)
        img = Image.open(frame[i]).convert("RGB")
        Image.blend(img, primo, peso).save(frame[i])


def genera(uscita, video):
    if not os.path.isfile(video):
        sys.exit("FERMO: manca il video %s" % video)
    os.makedirs(os.path.dirname(os.path.abspath(uscita)), exist_ok=True)

    temp = tempfile.mkdtemp(prefix="bootanim-")
    try:
        frame = estrai(video, temp)
        chiudi_anello(frame)

        larghezza, altezza = Image.open(frame[0]).size
        desc = "%d %d %d\np 0 0 part0\n" % (larghezza, altezza, FPS)

        # ZIP_STORED e non ZIP_DEFLATED: vedi la trappola 1 in testa al file.
        with zipfile.ZipFile(uscita, "w", zipfile.ZIP_STORED) as z:
            z.writestr("desc.txt", desc)
            for i, p in enumerate(frame):
                nome = os.path.join(temp, "frame%03d.jpg" % i)
                Image.open(p).convert("RGB").save(
                    nome, "JPEG", quality=QUALITA, optimize=True,
                    subsampling=SOTTOCAMPIONAMENTO, progressive=False)
                z.write(nome, "part0/frame%03d.jpg" % i)
    finally:
        shutil.rmtree(temp, ignore_errors=True)

    print("scritto %s (%d frame, %dx%d a %d fps, coda di %d, %.1f MB)"
          % (uscita, len(frame), larghezza, altezza, FPS, CODA,
             os.path.getsize(uscita) / 1048576))


if __name__ == "__main__":
    qui = os.path.dirname(os.path.abspath(__file__))
    genera(sys.argv[1] if len(sys.argv) > 1
           else os.path.join(qui, "bootanimation.zip"),
           sys.argv[2] if len(sys.argv) > 2 else os.path.join(qui, VIDEO))
