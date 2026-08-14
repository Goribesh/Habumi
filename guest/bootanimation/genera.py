#!/usr/bin/env python3
# guest/bootanimation/genera.py -- genera bootanimation.zip per GodziDroid.
#
# PERCHE' UNO SCRIPT E NON UN FILE BINARIO NEL REPO. Un'animazione e' una
# settantina di PNG: committarli renderebbe illeggibile ogni diff e impossibile
# cambiare un colore senza rifare tutto a mano. Lo script e' la sorgente, lo zip
# e' l'artefatto -- come per il resto del progetto, dove le immagini si
# ricostruiscono e non si custodiscono.
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
# Uso:  python guest/bootanimation/genera.py [percorso-di-uscita]
import os
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

LARGHEZZA = 1280
ALTEZZA = 800
FPS = 12
FRAME = 36

# La tavolozza: acciaio scuro e un ciano che fa da "acceso". Due colori soli,
# perche' un'animazione d'avvio si guarda per venti secondi e non deve gridare.
SFONDO_ALTO = (8, 11, 16)
SFONDO_BASSO = (16, 22, 30)
CORPO = (38, 48, 60)
CORPO_BORDO = (70, 88, 106)
ACCESO = (0, 224, 208)
ACCESO_FIOCO = (0, 110, 104)
TESTO = (196, 214, 226)


def font(dimensione, grassetto=True):
    """Il carattere di sistema, con ripiego se non c'e'.

    Non si mette un .ttf nel repo per una scritta sola: se manca, PIL disegna
    con il proprio bitmap e l'animazione resta valida -- brutta, ma valida, che
    e' meglio di uno script che non parte."""
    for nome in ("arialbd.ttf" if grassetto else "arial.ttf",
                 "seguisb.ttf", "segoeui.ttf", "consolab.ttf"):
        try:
            return ImageFont.truetype("C:/Windows/Fonts/" + nome, dimensione)
        except OSError:
            continue
    return ImageFont.load_default()


def sfondo():
    img = Image.new("RGB", (LARGHEZZA, ALTEZZA), SFONDO_ALTO)
    d = ImageDraw.Draw(img)
    for y in range(ALTEZZA):
        t = y / (ALTEZZA - 1)
        d.line([(0, y), (LARGHEZZA, y)],
               fill=tuple(int(a + (b - a) * t)
                          for a, b in zip(SFONDO_ALTO, SFONDO_BASSO)))
    return img


# La sagoma: un bipede curvo con la coda pesante, in coordinate relative al
# riquadro del disegno. E' una silhouette geometrica e non un'illustrazione:
# dichiarato, cosi' nessuno si aspetta un disegno a mano.
CORPO_PUNTI = [
    (0.50, 0.06), (0.58, 0.09), (0.62, 0.16), (0.61, 0.24),   # testa e collo
    (0.66, 0.30), (0.70, 0.40), (0.71, 0.52),                  # petto
    (0.68, 0.62), (0.72, 0.70), (0.78, 0.78), (0.74, 0.84),    # gamba destra
    (0.64, 0.82), (0.58, 0.86), (0.50, 0.84),
    (0.44, 0.87), (0.36, 0.85), (0.30, 0.79),                  # gamba sinistra
    (0.22, 0.84), (0.10, 0.86), (0.04, 0.82),                  # coda
    (0.16, 0.78), (0.26, 0.72), (0.32, 0.64),
    (0.34, 0.52), (0.36, 0.40), (0.40, 0.28),                  # dorso
    (0.42, 0.18), (0.45, 0.10),
]

# Le placche dorsali: coppie (posizione lungo il dorso, altezza).
PLACCHE = [(0.16, 0.055), (0.27, 0.075), (0.39, 0.085),
           (0.51, 0.080), (0.63, 0.062), (0.74, 0.042)]


def scala(punti, x0, y0, w, h):
    return [(x0 + px * w, y0 + py * h) for px, py in punti]


def disegna_dino(d, x0, y0, w, h, fase):
    corpo = scala(CORPO_PUNTI, x0, y0, w, h)
    d.polygon(corpo, fill=CORPO, outline=CORPO_BORDO)

    # Le placche si accendono a onda: e' il solo movimento della sagoma, e basta
    # a far sembrare l'animazione viva senza animare la figura.
    for i, (t, alt) in enumerate(PLACCHE):
        bx = x0 + (0.36 - 0.30 * t) * w + t * 0.30 * w
        by = y0 + (0.62 - 0.42 * t) * h
        punta = (bx - 0.02 * w, by - alt * h)
        base_a = (bx + 0.03 * w, by)
        base_b = (bx - 0.05 * w, by + 0.02 * h)
        onda = (fase - i / len(PLACCHE)) % 1.0
        acceso = onda < 0.22
        d.polygon([punta, base_a, base_b],
                  fill=ACCESO if acceso else ACCESO_FIOCO,
                  outline=CORPO_BORDO)

    # L'occhio, con un alone finto fatto di cerchi concentrici: PIL non ha una
    # sfocatura gratis e tre cerchi costano meno di un filtro su 36 frame.
    ex, ey = x0 + 0.565 * w, y0 + 0.135 * h
    for r, c in ((0.020 * w, ACCESO_FIOCO), (0.012 * w, ACCESO),
                 (0.006 * w, (220, 255, 252))):
        d.ellipse([ex - r, ey - r, ex + r, ey + r], fill=c)

    # La bocca: una fessura, non denti. I denti a poligoni sembrano un errore.
    d.line([(x0 + 0.50 * w, y0 + 0.185 * h), (x0 + 0.615 * w, y0 + 0.165 * h)],
           fill=ACCESO_FIOCO, width=max(2, int(0.006 * w)))


def genera(uscita):
    cartella = os.path.dirname(os.path.abspath(uscita))
    os.makedirs(cartella, exist_ok=True)
    f_nome = font(int(ALTEZZA * 0.075))
    f_sotto = font(int(ALTEZZA * 0.026), grassetto=False)

    frame = []
    for i in range(FRAME):
        fase = i / FRAME
        img = sfondo()
        d = ImageDraw.Draw(img)

        bw, bh = LARGHEZZA * 0.46, ALTEZZA * 0.66
        disegna_dino(d, (LARGHEZZA - bw) / 2, ALTEZZA * 0.06, bw, bh, fase)

        # La linea di scansione, che scende e riparte: dice "sta caricando"
        # senza una barra di avanzamento, che mentirebbe non sapendo a che punto
        # siamo davvero.
        sy = ALTEZZA * 0.06 + (fase * 1.15 % 1.0) * bh
        d.line([(LARGHEZZA * 0.24, sy), (LARGHEZZA * 0.76, sy)],
               fill=ACCESO_FIOCO, width=2)

        testo = "GodziDroid"
        lt = d.textlength(testo, font=f_nome)
        d.text(((LARGHEZZA - lt) / 2, ALTEZZA * 0.79), testo,
               font=f_nome, fill=TESTO)
        sotto = "Android on Windows ARM64"
        ls = d.textlength(sotto, font=f_sotto)
        d.text(((LARGHEZZA - ls) / 2, ALTEZZA * 0.90), sotto,
               font=f_sotto, fill=ACCESO_FIOCO)

        p = os.path.join(cartella, "frame%03d.png" % i)
        img.save(p)
        frame.append(p)

    desc = "%d %d %d\np 0 0 part0\n" % (LARGHEZZA, ALTEZZA, FPS)
    # ZIP_STORED e non ZIP_DEFLATED: vedi la trappola 1 in testa al file.
    with zipfile.ZipFile(uscita, "w", zipfile.ZIP_STORED) as z:
        z.writestr("desc.txt", desc)
        for i, p in enumerate(frame):
            z.write(p, "part0/frame%03d.png" % i)
    for p in frame:
        os.remove(p)

    print("scritto %s (%d frame, %dx%d a %d fps, %d byte)"
          % (uscita, FRAME, LARGHEZZA, ALTEZZA, FPS, os.path.getsize(uscita)))


if __name__ == "__main__":
    genera(sys.argv[1] if len(sys.argv) > 1
           else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "bootanimation.zip"))
