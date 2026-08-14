package com.godziller.habumi.clipboard;

import android.app.Service;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import java.io.DataInputStream;
import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

/**
 * Tiene gli appunti del guest allineati a quelli dell'host.
 *
 * IL CANALE. Una connessione TCP verso 10.0.2.2, cioe' l'host visto attraverso
 * la SLIRP di QEMU che esiste gia'. E' il guest a chiamare, non il contrario:
 * cosi' il guscio non deve sapere quando la VM esiste, e un riavvio del guest
 * si ripara da se'.
 *
 * LA TRAMA, identica al lato host (app/guscio/appunti-trama.c): 4 byte
 * big-endian di lunghezza, poi il testo UTF-8 senza terminatore. Non righe
 * terminate da newline, perche' gli appunti sono multiriga di natura e con la
 * lunghezza davanti non serve nessuna protezione dei caratteri.
 *
 * IL TETTO. 1 MiB, e oltre si RIFIUTA invece di troncare: appunti troncati in
 * silenzio sono peggio di appunti non passati.
 *
 * LA GUARDIA CONTRO L'ECO, la cosa senza la quale tutto il resto si impicca.
 * L'host scrive nel guest, il listener del guest scatta, il guest rimanda
 * all'host, l'host riscrive: l'anello e' il modo in cui questa funzione si
 * rompe. Si ricorda l'ULTIMO VALORE ricevuto dall'host -- il valore, non un
 * flag ne' un contatore -- e non lo si rispedisce. Un flag non basta perche'
 * non si sa quante volte il listener scattera' ne' quando; il valore invece
 * risponde alla domanda giusta, che e' "questo testo viene da me o da lui?".
 */
public final class ServizioAppunti extends Service {

    private static final String TAG = "HabumiAppunti";

    /** L'host visto dal guest attraverso la SLIRP. */
    private static final String HOST = "10.0.2.2";

    /**
     * Deve coincidere con la chiave porta_appunti di runtime/bin/config.txt
     * (stesso default). Questa app sta dentro system.img: cambiare la porta sul
     * guscio senza ricostruire l'APK vuol dire che il guest non si connette piu'
     * e non lo dice a nessuno, perche' l'assenza del guscio e' un caso normale.
     */
    private static final int PORTA = 15556;

    /** 1 MiB. Oltre si rifiuta, non si tronca. */
    private static final int TETTO_BYTE = 1024 * 1024;

    private static final int ATTESA_MIN_MS = 1000;
    private static final int ATTESA_MAX_MS = 30000;
    private static final int TIMEOUT_CONNESSIONE_MS = 2000;

    private ClipboardManager appunti;
    private Handler mano;

    private volatile boolean vivo;
    private volatile OutputStream uscita;
    private volatile Socket presa;

    private Thread filoRete;
    private Thread filoInvio;

    /**
     * LA GUARDIA CONTRO L'ECO. L'ultimo testo arrivato dall'host: quando il
     * listener degli appunti scatta per averlo scritto noi, questo confronto
     * e' cio' che impedisce di rimandarlo indietro all'infinito.
     */
    private volatile String ultimoRicevuto;

    /**
     * Il testo in attesa di partire, uno solo. Un valore nuovo sovrascrive
     * quello vecchio invece di accodarsi: degli appunti conta solo l'ultimo
     * stato, e una coda che si accumula mentre il guscio e' assente
     * rovescerebbe sull'host una raffica di valori scaduti alla riconnessione.
     * Protetto da serratura.
     */
    private String inAttesa;
    private final Object serratura = new Object();

    private final ClipboardManager.OnPrimaryClipChangedListener ascoltatore =
            new ClipboardManager.OnPrimaryClipChangedListener() {
                @Override
                public void onPrimaryClipChanged() {
                    appuntiGuestCambiati();
                }
            };

    @Override
    public void onCreate() {
        super.onCreate();
        vivo = true;
        mano = new Handler(Looper.getMainLooper());
        appunti = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (appunti == null) {
            Log.e(TAG, "nessun ClipboardManager: mi fermo");
            stopSelf();
            return;
        }
        appunti.addPrimaryClipChangedListener(ascoltatore);

        filoRete = new Thread(new Runnable() {
            @Override
            public void run() {
                cicloRete();
            }
        }, "appunti-rete");
        filoRete.setDaemon(true);
        filoRete.start();

        // Un filo a parte per l'invio: il listener degli appunti gira sul thread
        // principale, dove una scrittura su socket costa una
        // NetworkOnMainThreadException.
        filoInvio = new Thread(new Runnable() {
            @Override
            public void run() {
                cicloInvio();
            }
        }, "appunti-invio");
        filoInvio.setDaemon(true);
        filoInvio.start();
    }

    @Override
    public int onStartCommand(Intent intento, int bandiere, int idAvvio) {
        // START_STICKY: se il sistema uccide il processo sotto pressione di
        // memoria lo rifa' partire da solo, e nessuno e' li' a rilanciarlo a
        // mano perche' l'app non ha interfaccia.
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intento) {
        return null;
    }

    @Override
    public void onDestroy() {
        vivo = false;
        if (appunti != null) {
            appunti.removePrimaryClipChangedListener(ascoltatore);
        }
        chiudiSilenzioso(presa);
        synchronized (serratura) {
            serratura.notifyAll();
        }
        if (filoRete != null) {
            filoRete.interrupt();
        }
        if (filoInvio != null) {
            filoInvio.interrupt();
        }
        super.onDestroy();
    }

    /* ---------------------------------------------------------------- rete */

    /**
     * Connessione, lettura fino alla caduta, riconnessione a scalare.
     *
     * IN SILENZIO. Il guscio puo' benissimo non esserci: l'emulatore si avvia
     * anche da avvia-android.ps1, senza guscio, ed e' un caso normale, non un
     * errore. Una riga di log a ogni tentativo fallito riempirebbe il logcat di
     * rumore per tutta la durata della sessione. Si registra solo cio' che
     * capita di rado: la connessione riuscita e la sua caduta.
     */
    private void cicloRete() {
        int attesa = ATTESA_MIN_MS;
        while (vivo) {
            Socket s = null;
            boolean eraConnesso = false;
            try {
                s = new Socket();
                s.connect(new InetSocketAddress(HOST, PORTA), TIMEOUT_CONNESSIONE_MS);
                // Gli appunti sono messaggi corti e isolati: l'attesa di Nagle
                // aggiungerebbe solo latenza a un incollaggio.
                s.setTcpNoDelay(true);
                presa = s;
                uscita = s.getOutputStream();
                eraConnesso = true;
                attesa = ATTESA_MIN_MS;
                Log.i(TAG, "connesso al guscio su " + HOST + ":" + PORTA);
                leggi(s);
            } catch (IOException e) {
                // Silenzio voluto: vedi il commento del metodo.
            } finally {
                uscita = null;
                presa = null;
                chiudiSilenzioso(s);
            }
            if (eraConnesso) {
                Log.i(TAG, "connessione col guscio caduta, riprovo");
            }
            if (!vivo) {
                break;
            }
            try {
                Thread.sleep(attesa);
            } catch (InterruptedException ie) {
                Thread.currentThread().interrupt();
                break;
            }
            attesa = Math.min(attesa * 2, ATTESA_MAX_MS);
        }
    }

    /** Legge trame finche' la connessione regge. Torna quando cade o va rifiutata. */
    private void leggi(Socket s) throws IOException {
        DataInputStream ingresso =
                new DataInputStream(new BufferedInputStream(s.getInputStream()));
        while (vivo) {
            // readInt e' big-endian per definizione: e' la stessa trama del
            // lato host, non una coincidenza da verificare a mano.
            int lung = ingresso.readInt();
            if (lung < 0 || lung > TETTO_BYTE) {
                // Trama malformata o oltre il tetto: si chiude e si torna in
                // ascolto, non si prova a interpretare. Il numero si stampa
                // senza segno perche' una lunghezza non lo ha.
                Log.w(TAG, "trama di " + (lung & 0xFFFFFFFFL)
                        + " byte, oltre il tetto di " + TETTO_BYTE + ": chiudo");
                return;
            }
            byte[] grezzo = new byte[lung];
            ingresso.readFully(grezzo);
            consegnaAgliAppunti(new String(grezzo, StandardCharsets.UTF_8));
        }
    }

    private void cicloInvio() {
        while (vivo) {
            String testo;
            synchronized (serratura) {
                while (vivo && inAttesa == null) {
                    try {
                        serratura.wait();
                    } catch (InterruptedException ie) {
                        Thread.currentThread().interrupt();
                        return;
                    }
                }
                testo = inAttesa;
                inAttesa = null;
            }
            if (testo == null) {
                continue;
            }
            OutputStream o = uscita;
            if (o == null) {
                // Guscio assente: il valore si perde, ed e' giusto cosi'.
                // Spedirlo alla riconnessione vorrebbe dire sovrascrivere gli
                // appunti dell'host con qualcosa che l'utente ha copiato
                // minuti prima.
                continue;
            }
            try {
                spedisci(o, testo);
            } catch (IOException e) {
                // La caduta la gestisce il filo di rete: qui basta chiudere per
                // farlo uscire dalla readInt.
                chiudiSilenzioso(presa);
            }
        }
    }

    private void spedisci(OutputStream o, String testo) throws IOException {
        byte[] corpo = testo.getBytes(StandardCharsets.UTF_8);
        if (corpo.length > TETTO_BYTE) {
            Log.w(TAG, "appunti di " + corpo.length + " byte, oltre il tetto di "
                    + TETTO_BYTE + ": rifiutati invece che troncati");
            return;
        }
        byte[] trama = new byte[4 + corpo.length];
        trama[0] = (byte) (corpo.length >>> 24);
        trama[1] = (byte) (corpo.length >>> 16);
        trama[2] = (byte) (corpo.length >>> 8);
        trama[3] = (byte) corpo.length;
        System.arraycopy(corpo, 0, trama, 4, corpo.length);
        // Una write sola: lunghezza e testo non si separano mai, nemmeno se la
        // connessione cade a meta'.
        o.write(trama);
        o.flush();
    }

    private static void chiudiSilenzioso(Socket s) {
        if (s == null) {
            return;
        }
        try {
            s.close();
        } catch (IOException e) {
            // Chiudere una presa gia' morta non e' una notizia.
        }
    }

    /* ------------------------------------------------------------- appunti */

    /** Testo arrivato dall'host: va negli appunti del guest. */
    private void consegnaAgliAppunti(final String testo) {
        // LA GUARDIA: ci si ricorda PRIMA di scrivere. setPrimaryClip fa
        // scattare il listener, e se il valore non fosse gia' registrato il
        // listener lo rimanderebbe all'host, che lo riscriverebbe qui, e cosi'
        // per sempre.
        ultimoRicevuto = testo;
        mano.post(new Runnable() {
            @Override
            public void run() {
                try {
                    appunti.setPrimaryClip(ClipData.newPlainText("", testo));
                } catch (RuntimeException e) {
                    Log.w(TAG, "setPrimaryClip rifiutato: " + e);
                }
            }
        });
    }

    /** Gli appunti del guest sono cambiati: forse vanno mandati all'host. */
    private void appuntiGuestCambiati() {
        ClipData dati;
        try {
            dati = appunti.getPrimaryClip();
        } catch (SecurityException e) {
            // Senza READ_CLIPBOARD_IN_BACKGROUND si finisce qui, ed e' l'unico
            // guasto che vale una riga: l'APK non e' firmato di piattaforma.
            Log.w(TAG, "lettura degli appunti negata: " + e);
            return;
        }
        if (dati == null || dati.getItemCount() == 0) {
            return;
        }
        // getText e non coerceToText: un'immagine o un URI danno null e si
        // ignorano in silenzio, che e' quello che deve succedere a ogni copia
        // di qualcosa che non e' testo.
        CharSequence cs = dati.getItemAt(0).getText();
        if (cs == null) {
            return;
        }
        String testo = cs.toString();
        if (testo.equals(ultimoRicevuto)) {
            // LA GUARDIA CONTRO L'ECO: questo testo l'ha scritto l'host, non
            // l'utente del guest. Rimandarlo indietro chiuderebbe l'anello.
            return;
        }
        synchronized (serratura) {
            inAttesa = testo;
            serratura.notifyAll();
        }
    }
}
