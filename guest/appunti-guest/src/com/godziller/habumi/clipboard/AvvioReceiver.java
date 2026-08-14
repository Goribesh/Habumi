package com.godziller.habumi.clipboard;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Avvia ServizioAppunti a boot finito.
 *
 * PERCHE' UN RECEIVER E NON UN'ACTIVITY: l'app non ha interfaccia e nessuno la
 * lancera' mai a mano. BOOT_COMPLETED e' l'unico aggancio che resta, e mette
 * l'app in lista d'attesa temporanea per i limiti sull'esecuzione in secondo
 * piano, quindi startService da qui e' consentito anche su API 33.
 */
public final class AvvioReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context contesto, Intent intento) {
        if (intento == null || !Intent.ACTION_BOOT_COMPLETED.equals(intento.getAction())) {
            return;
        }
        contesto.startService(new Intent(contesto, ServizioAppunti.class));
    }
}
