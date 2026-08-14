#!/bin/sh
# Il driver virtio-snd del guest smette di spedire audio e non riparte piu'.
#
# IL SINTOMO, MISURATO. Con Spotify in riproduzione, dopo qualche secondo:
#
#     /proc/asound/card0/pcm0p/sub0/status
#       state: RUNNING   delay: 1024   avail: 0   avail_max: 0
#       hw_ptr: 0        appl_ptr: 1024
#
# L'applicazione ha riempito tutto il buffer (appl_ptr = buffer_size = 1024
# frame), ALSA ha fatto partire lo stream, e l'hardware non ha consumato NEMMENO
# UN FRAME: hw_ptr resta a zero per sempre. Dall'altra parte QEMU, interrogato
# 270 volte al secondo, trova la coda di trasmissione VUOTA e scrive zero byte
# al backend. Il guest non e' lento: e' fermo. Per l'utente la canzone "va
# avanti nel tempo ma non suona", perche' il tempo lo conta l'applicazione, non
# la scheda.
#
# avail_max = 0 e' la misura che chiude il caso: e' il massimo spazio libero mai
# osservato da quando lo stream e' stato preparato. Se lo stream avesse suonato
# anche solo per un periodo, sarebbe maggiore di zero. Questo stream non ha mai
# funzionato: e' nato gia' morto.
#
# LA CAUSA. In virtsnd_pcm_msg_send() i dati non partono un byte alla volta: il
# driver accumula in msg->length e spedisce il messaggio solo quando il periodo
# e' esattamente pieno:
#
#     msg->length += n;
#     if (msg->length == period_bytes) { ...virtqueue_add_sgs()... }
#
# Il confronto e' di UGUAGLIANZA. E msg->length viene riportato a zero in un
# solo punto di tutto il driver: virtsnd_pcm_msg_complete(), cioe' quando il
# messaggio torna indietro dall'host. Un messaggio che ha accumulato un resto
# parziale e non e' mai stato messo in coda non viene mai completato, quindi non
# viene mai azzerato -- e virtsnd_pcm_prepare(), che pure rimette a posto
# hw_ptr, msg_count e pcm_indirect, non lo tocca:
#
#     vss->xfer_xrun = false;
#     vss->suspended = false;
#     vss->msg_count = 0;
#     memset(&vss->pcm_indirect, 0, sizeof(vss->pcm_indirect));
#
# Al giro successivo quel messaggio riparte da un residuo, la somma SALTA
# period_bytes invece di centrarlo, e da quel momento quel messaggio non parte
# mai piu'. Se capita a tutti i messaggi del buffer, non parte piu' niente:
# hw_ptr inchiodato a zero, esattamente quello che si misura.
#
# I messaggi sopravvivono al ciclo perche' virtsnd_pcm_hw_free() li libera solo
# se la coda e' gia' vuota ("If the queue is flushed, we can safely free the
# messages here"), e perche' un semplice snd_pcm_prepare() dopo un XRUN non
# passa nemmeno da hw_free.
#
# LA CORREZIONE. Azzerare le lunghezze accumulate in prepare, che e' il punto in
# cui il driver dichiara di ripartire da capo -- accanto a hw_ptr e msg_count,
# insieme ai quali questa riga sarebbe dovuta stare fin dall'inizio.
#
# LA STRUMENTAZIONE, e perche' resta. Due dev_warn: uno dice quante lunghezze
# residue sono state trovate (se non se ne trovano mai, questa diagnosi e'
# sbagliata e va detto), l'altro segnala il caso in cui c'era un periodo intero
# da spedire, la coda era vuota, e non e' partito niente -- cioe' il guasto
# osservato. Sono a frequenza limitata e in condizioni sane non stampano nulla:
# costano zero e sono l'unico modo per accorgersi da fuori che il difetto e'
# tornato.
#
# IDEMPOTENTE: rieseguirlo su un albero gia' modificato non fa nulla e esce 0.
set -eu

ALBERO="${1:-}"
if [ -z "$ALBERO" ] || [ ! -d "$ALBERO/sound/virtio" ]; then
    echo "uso: $0 <albero-del-kernel>"
    echo "     esempio: $0 \$HOME/linux-6.18.35"
    exit 1
fi

HDR="$ALBERO/sound/virtio/virtio_pcm.h"
MSG="$ALBERO/sound/virtio/virtio_pcm_msg.c"
OPS="$ALBERO/sound/virtio/virtio_pcm_ops.c"
for f in "$HDR" "$MSG" "$OPS"; do
    [ -f "$f" ] || { echo "FERMO: manca $f"; exit 1; }
done

fatto=0

# --- 1. il prototipo nell'intestazione --------------------------------------
if grep -q 'virtsnd_pcm_msg_azzera_lunghezze' "$HDR"; then
    echo "  1/4 prototipo: gia' presente"
else
    python3 - "$HDR" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
a = "unsigned int virtsnd_pcm_msg_pending_num(struct virtio_pcm_substream *vss);"
b = (a + "\n\n"
     "unsigned int virtsnd_pcm_msg_azzera_lunghezze(struct virtio_pcm_substream *vss);")
if a not in t:
    sys.exit("il prototipo di pending_num non e' nella forma attesa: albero diverso da 6.18?")
open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  1/4 prototipo: aggiunto"
    fatto=1
fi

# --- 2. la funzione che azzera le lunghezze ---------------------------------
if grep -q 'virtsnd_pcm_msg_azzera_lunghezze' "$MSG"; then
    echo "  2/4 funzione di azzeramento: gia' presente"
else
    python3 - "$MSG" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
a = ("/**\n"
     " * virtsnd_pcm_msg_pending_num() - Returns the number of pending I/O messages.\n")
b = ("/**\n"
     " * virtsnd_pcm_msg_azzera_lunghezze() - winq: scarta i resti accumulati.\n"
     " * @vss: VirtIO PCM substream.\n"
     " *\n"
     " * msg->length viene azzerato solo in virtsnd_pcm_msg_complete(), cioe' solo\n"
     " * per i messaggi che sono passati dalla virtqueue. Un messaggio rimasto con un\n"
     " * resto parziale non centra mai piu' il confronto di uguaglianza in\n"
     " * virtsnd_pcm_msg_send() e non viene spedito mai piu'. Chiamata da prepare,\n"
     " * dove il substream riparte da capo.\n"
     " *\n"
     " * Context: Process context. Expects the VirtIO substream spinlock NOT held.\n"
     " * Return: Number of messages that still carried a leftover.\n"
     " */\n"
     "unsigned int virtsnd_pcm_msg_azzera_lunghezze(struct virtio_pcm_substream *vss)\n"
     "{\n"
     "\tunsigned int i;\n"
     "\tunsigned int sporchi = 0;\n"
     "\n"
     "\tguard(spinlock_irqsave)(&vss->lock);\n"
     "\n"
     "\tif (!vss->msgs)\n"
     "\t\treturn 0;\n"
     "\n"
     "\tfor (i = 0; i < vss->nmsgs; i++) {\n"
     "\t\tif (!vss->msgs[i] || !vss->msgs[i]->length)\n"
     "\t\t\tcontinue;\n"
     "\t\tvss->msgs[i]->length = 0;\n"
     "\t\tsporchi++;\n"
     "\t}\n"
     "\n"
     "\treturn sporchi;\n"
     "}\n"
     "\n") + a
if a not in t:
    sys.exit("il commento di pending_num non e' nella forma attesa")
open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  2/4 funzione di azzeramento: aggiunta"
    fatto=1
fi

# --- 3. l'avviso quando non parte niente ------------------------------------
if grep -q 'winq-snd: SID %u: nessun messaggio' "$MSG"; then
    echo "  3/4 avviso in msg_send: gia' presente"
else
    python3 - "$MSG" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()

a1 = ("\tunsigned long start, end, i;\n"
      "\tunsigned int msg_count = vss->msg_count;\n")
b1 = ("\tunsigned long start, end, i;\n"
      "\tunsigned long byte_chiesti = bytes;\n"
      "\tunsigned int msg_count = vss->msg_count;\n")
if a1 not in t:
    sys.exit("le dichiarazioni di msg_send non sono nella forma attesa")
t = t.replace(a1, b1, 1)

a2 = ("\tif (msg_count == vss->msg_count)\n"
      "\t\treturn 0;\n")
b2 = ("\tif (msg_count == vss->msg_count) {\n"
      "\t\t/* winq: accumulare senza spedire e' normale finche' il periodo non\n"
      "\t\t * e' pieno. Non lo e' se c'era un periodo intero da mandare e la\n"
      "\t\t * coda era gia' vuota: li' l'audio si e' fermato e non riparte.\n"
      "\t\t */\n"
      "\t\tif (!vss->msg_count && byte_chiesti >= period_bytes)\n"
      "\t\t\tdev_warn_ratelimited(&vdev->dev,\n"
      "\t\t\t\t\"winq-snd: SID %u: nessun messaggio spedito con %lu byte "
      "pronti (periodo %lu, len[%lu]=%zu)\\n\",\n"
      "\t\t\t\tvss->sid, byte_chiesti, period_bytes, start,\n"
      "\t\t\t\tvss->msgs[start] ? vss->msgs[start]->length : 0);\n"
      "\t\treturn 0;\n"
      "\t}\n")
if a2 not in t:
    sys.exit("l'uscita anticipata di msg_send non e' nella forma attesa")
t = t.replace(a2, b2, 1)

open(p, "w", encoding="utf-8", newline="\n").write(t)
PY
    echo "  3/4 avviso in msg_send: aggiunto"
    fatto=1
fi

# --- 4. la chiamata in prepare ----------------------------------------------
if grep -q 'virtsnd_pcm_msg_azzera_lunghezze' "$OPS"; then
    echo "  4/4 azzeramento in prepare: gia' presente"
else
    python3 - "$OPS" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()

a1 = ("\tstruct virtio_device *vdev = vss->snd->vdev;\n"
      "\tstruct virtio_snd_msg *msg;\n"
      "\n"
      "\tif (!vss->suspended) {\n")
b1 = ("\tstruct virtio_device *vdev = vss->snd->vdev;\n"
      "\tstruct virtio_snd_msg *msg;\n"
      "\tunsigned int sporchi;\n"
      "\n"
      "\tif (!vss->suspended) {\n")
if a1 not in t:
    sys.exit("le dichiarazioni di prepare non sono nella forma attesa")
t = t.replace(a1, b1, 1)

a2 = ("\tvss->xfer_xrun = false;\n"
      "\tvss->suspended = false;\n"
      "\tvss->msg_count = 0;\n")
b2 = ("\tvss->xfer_xrun = false;\n"
      "\tvss->suspended = false;\n"
      "\tvss->msg_count = 0;\n"
      "\n"
      "\t/* winq: i messaggi sopravvivono al ciclo stop/prepare, e con loro il\n"
      "\t * resto parziale che impedisce per sempre a msg_send di centrare\n"
      "\t * period_bytes. Qui il substream riparte da capo: che ripartano anche\n"
      "\t * loro.\n"
      "\t */\n"
      "\tsporchi = virtsnd_pcm_msg_azzera_lunghezze(vss);\n"
      "\tif (sporchi)\n"
      "\t\tdev_warn(&vdev->dev,\n"
      "\t\t\t \"winq-snd: SID %u: %u messaggi con lunghezza residua, azzerati\\n\",\n"
      "\t\t\t vss->sid, sporchi);\n")
if a2 not in t:
    sys.exit("l'azzeramento in prepare non e' nella forma attesa")
t = t.replace(a2, b2, 1)

open(p, "w", encoding="utf-8", newline="\n").write(t)
PY
    echo "  4/4 azzeramento in prepare: aggiunto"
    fatto=1
fi

# --- 5. quanto costa la coda di lavoro ---------------------------------------
#
# PERCHE'. Il completamento di ogni periodo non avvisa ALSA direttamente: mette
# in coda un work (schedule_work su vss->elapsed_period) perche'
# snd_pcm_period_elapsed non si puo' chiamare tenendo vss->lock. Il costo di
# quel rinvio non e' mai stato misurato, e i conti lo indicano:
# AudioFlinger impiega ~100 ms per una scrittura da 1024 frame che vale 21,3 ms
# di audio; quella scrittura aspetta 4 periodi, quindi ~25 ms a periodo contro
# i 5,3 dovuti. I ~20 ms di troppo hanno la forma di una latenza di workqueue.
#
# Questa e' SOLO una misura: stampa il ritardo medio fra il completamento e
# l'esecuzione del work. Va tolta quando il difetto e' chiuso.
if grep -q 'winq_stamp' "$HDR"; then
    echo "  5/5 misura della coda di lavoro: gia' presente"
else
    python3 - "$HDR" "$MSG" "$ALBERO/sound/virtio/virtio_pcm.c" <<'PY'
import sys
hdr, msg, pcm = sys.argv[1], sys.argv[2], sys.argv[3]

t = open(hdr, encoding="utf-8").read()
a = "\tstruct work_struct elapsed_period;"
if a not in t:
    sys.exit("il campo elapsed_period non e' nella forma attesa")
b = (a + "\n"
     "\t/* winq: misura del ritardo della coda di lavoro. Solo diagnostica. */\n"
     "\tu64 winq_stamp;\n"
     "\tu64 winq_somma;\n"
     "\tunsigned int winq_n;")
open(hdr, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))

t = open(msg, encoding="utf-8").read()
a = ("\tif (vss->xfer_enabled) {\n"
     "\t\tstruct snd_pcm_runtime *runtime = vss->substream->runtime;\n")
if a not in t:
    sys.exit("il ramo xfer_enabled di msg_complete non e' nella forma attesa")
b = ("\tif (vss->xfer_enabled) {\n"
     "\t\tstruct snd_pcm_runtime *runtime = vss->substream->runtime;\n"
     "\n"
     "\t\t/* winq: quando il periodo e' stato completato davvero. */\n"
     "\t\tvss->winq_stamp = ktime_get_ns();\n")
open(msg, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))

t = open(pcm, encoding="utf-8").read()
a = ("\tstruct virtio_pcm_substream *vss =\n"
     "\t\tcontainer_of(work, struct virtio_pcm_substream, elapsed_period);\n"
     "\n"
     "\tsnd_pcm_period_elapsed(vss->substream);")
if a not in t:
    sys.exit("virtsnd_pcm_period_elapsed non e' nella forma attesa")
b = ("\tstruct virtio_pcm_substream *vss =\n"
     "\t\tcontainer_of(work, struct virtio_pcm_substream, elapsed_period);\n"
     "\n"
     "\t/* winq: quanto e' passato da quando il periodo e' stato completato a\n"
     "\t * quando la coda di lavoro ci ha finalmente eseguiti. A 48 kHz con\n"
     "\t * periodi da 256 frame il budget e' 5333 us: se la media si avvicina\n"
     "\t * a quel valore o lo supera, il rinvio e' il collo di bottiglia.\n"
     "\t */\n"
     "\tif (vss->winq_stamp) {\n"
     "\t\tvss->winq_somma += ktime_get_ns() - vss->winq_stamp;\n"
     "\t\tvss->winq_stamp = 0;\n"
     "\t\tif (++vss->winq_n >= 200) {\n"
     "\t\t\tdev_info(&vss->snd->vdev->dev,\n"
     "\t\t\t\t \"winq-snd: SID %u: ritardo medio della coda di lavoro %llu us su %u periodi (budget 5333 us)\\n\",\n"
     "\t\t\t\t vss->sid, vss->winq_somma / vss->winq_n / 1000,\n"
     "\t\t\t\t vss->winq_n);\n"
     "\t\t\tvss->winq_somma = 0;\n"
     "\t\t\tvss->winq_n = 0;\n"
     "\t\t}\n"
     "\t}\n"
     "\n"
     "\tsnd_pcm_period_elapsed(vss->substream);")
open(pcm, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  5/5 misura della coda di lavoro: aggiunta"
    fatto=1
fi

# --- 6. un period_elapsed per periodo, e il puntatore che avanza di uno -------
#
# IL DIFETTO, e perche' e' questo. Fotografato in flagrante con Spotify:
#
#     state: RUNNING   avail: 0   hw_ptr: 1024   appl_ptr: 2048
#     QEMU: coda_qemu=vuota  virtqueue=vuota  (nessuna bussata persa)
#
# hw_ptr fermo a ESATTAMENTE buffer_size. Non e' un caso: e' la firma.
#
# Il driver alimenta la virtqueue dalla callback .ack, che passa per
# snd_pcm_indirect_playback_transfer(). Quella copia solo finche':
#
#     while (rec->hw_ready < rec->hw_buffer_size && rec->sw_ready > 0)
#
# e hw_ready cala dentro snd_pcm_indirect_playback_pointer():
#
#     int diff = ptr - rec->hw_io;
#     if (diff) { ...; rec->hw_ready -= diff; }
#
# dove ptr e' vss->hw_ptr, che virtsnd_pcm_msg_complete() AVVOLGE:
#
#     if (vss->hw_ptr >= vss->buffer_bytes)
#             vss->hw_ptr -= vss->buffer_bytes;
#
# Se fra due letture del puntatore si completa un buffer INTERO, ptr torna al
# valore di prima: diff = 0, hw_ready resta a hw_buffer_size, la condizione del
# while e' falsa per sempre. Niente copie, niente virtsnd_pcm_msg_send(),
# virtqueue vuota, hw_ptr inchiodato. L'applicazione riempie il buffer ALSA e
# si blocca in write; AudioFlinger dopo un po' molla e riapre; e siccome la
# scheda ha UN SOLO sottostream di riproduzione, quello congelato lo tiene
# occupato e ogni apertura successiva fallisce -- da qui il
# "Failed to open pcm_out after 100 tries" dell'HAL, che e' la conseguenza.
#
# PERCHE' SI COMPLETA UN BUFFER INTERO FRA DUE LETTURE. schedule_work() e'
# idempotente: se il work e' gia' in coda, le chiamate successive non fanno
# nulla. Con 4 periodi in volo tutti e 4 possono completarsi prima che il work
# giri UNA volta, e a quel punto hw_ptr e' avanzato di 4 x period = buffer.
# Il ritardo della coda non c'entra (misurato: 28-272 us su 5333 di budget):
# conta quante VOLTE gira, non quando.
#
# LA CORREZIONE. Contare i periodi completati e chiamare snd_pcm_period_elapsed
# una volta per ciascuno, facendo avanzare un puntatore riportato di UN periodo
# alla volta. Cosi' diff vale sempre period_bytes, mai zero, e hw_ready cala
# come deve. Il puntatore riportato insegue vss->hw_ptr e coincide con lui a
# regime: non si inventa posizioni, le consegna una per volta invece che tutte
# insieme.
if grep -q 'winq_riportato' "$HDR"; then
    echo "  6/6 un period_elapsed per periodo: gia' presente"
else
    python3 - "$HDR" "$MSG" "$ALBERO/sound/virtio/virtio_pcm.c" "$OPS" <<'PY'
import sys
hdr, msg, pcm, ops = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

t = open(hdr, encoding="utf-8").read()
a = "\tu64 winq_stamp;"
if a not in t:
    sys.exit("manca il campo winq_stamp: applicare prima il punto 5")
b = ("\t/* winq: periodi completati e non ancora annunciati ad ALSA, e la\n"
     "\t * posizione gia' annunciata. Vedi il punto 6 della patch.\n"
     "\t */\n"
     "\tunsigned int winq_pendenti;\n"
     "\tunsigned int winq_riportato;\n" + a)
open(hdr, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))

t = open(msg, encoding="utf-8").read()
a = "\t\tvss->winq_stamp = ktime_get_ns();\n"
if a not in t:
    sys.exit("manca la marcatura del punto 5 in msg_complete")
b = (a + "\t\tvss->winq_pendenti++;\n")
open(msg, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))

t = open(pcm, encoding="utf-8").read()
a = "\tsnd_pcm_period_elapsed(vss->substream);\n}"
if a not in t:
    sys.exit("la coda di virtsnd_pcm_period_elapsed non e' nella forma attesa")
b = ("\t/* winq: uno per periodo. Annunciarli tutti insieme fa avanzare il\n"
     "\t * puntatore di un buffer intero, e snd_pcm_indirect_playback_pointer\n"
     "\t * legge diff = 0 perche' hw_ptr avvolge: da li' non si riprende piu'.\n"
     "\t */\n"
     "\tfor (;;) {\n"
     "\t\tunsigned int periodo = snd_pcm_lib_period_bytes(vss->substream);\n"
     "\t\tunsigned int buffer = vss->buffer_bytes;\n"
     "\n"
     "\t\tscoped_guard(spinlock_irqsave, &vss->lock) {\n"
     "\t\t\tif (!vss->winq_pendenti || !periodo || !buffer)\n"
     "\t\t\t\treturn;\n"
     "\t\t\tvss->winq_pendenti--;\n"
     "\t\t\tvss->winq_riportato += periodo;\n"
     "\t\t\tif (vss->winq_riportato >= buffer)\n"
     "\t\t\t\tvss->winq_riportato -= buffer;\n"
     "\t\t}\n"
     "\n"
     "\t\tsnd_pcm_period_elapsed(vss->substream);\n"
     "\t}\n"
     "}")
open(pcm, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))

t = open(ops, encoding="utf-8").read()
a = ("\treturn snd_pcm_indirect_playback_pointer(substream,\n"
     "\t\t&vss->pcm_indirect,\n"
     "\t\tvss->hw_ptr);")
if a not in t:
    sys.exit("virtsnd_pcm_pb_pointer non e' nella forma attesa")
b = ("\t/* winq: la posizione ANNUNCIATA, che avanza di un periodo per volta.\n"
     "\t * vss->hw_ptr puo' aver gia' fatto un giro intero, e allora l'helper\n"
     "\t * calcolerebbe diff = 0 e non liberebbe mai spazio.\n"
     "\t */\n"
     "\treturn snd_pcm_indirect_playback_pointer(substream,\n"
     "\t\t&vss->pcm_indirect,\n"
     "\t\tvss->winq_riportato);")
t = t.replace(a, b, 1)

a2 = "\tsporchi = virtsnd_pcm_msg_azzera_lunghezze(vss);"
if a2 not in t:
    sys.exit("manca la chiamata del punto 4 in prepare")
b2 = ("\tvss->winq_pendenti = 0;\n"
      "\tvss->winq_riportato = 0;\n"
      "\n" + a2)
t = t.replace(a2, b2, 1)
open(ops, "w", encoding="utf-8", newline="\n").write(t)
PY
    echo "  6/6 un period_elapsed per periodo: aggiunto"
    fatto=1
fi

if [ "$fatto" = 0 ]; then
    echo "=== virtio-snd: albero gia' a posto, niente da fare"
else
    echo "=== virtio-snd: albero modificato"
fi
