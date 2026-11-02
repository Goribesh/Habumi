/*
 * Habumi -- run Android apps natively on Windows on ARM
 * Copyright (C) 2026 Godziller
 *
 * Software libero sotto GNU General Public License versione 2. Il testo
 * integrale e' nel file LICENSE alla radice di questo repository.
 */

/* test-vm.c -- prove dell'assemblaggio degli argomenti, senza avviare QEMU.
 *
 * Perche' vale la pena provarlo: lo script che questo guscio sostituisce ha
 * avuto un difetto proprio qui. Passando gli argomenti come ARRAY, Start-Process
 * li uniate con spazi senza citare quelli che ne contengono, e QEMU riceveva
 * "printk.devkmsg=on" come nome di file. Citando a mano, le virgolette
 * finivano nel valore e il kernel leggeva
 *     Kernel command line: "console=ttyAMA0 audit=0"
 * cioe' non configurava la seriale e ignorava tutti gli androidboot.*: il guest
 * sembrava inchiodarsi e la finestra restava su "Display output is not active".
 * Una prova che guarda la stringa costa un secondo e chiude quella categoria. */
#include <stdio.h>
#include <string.h>
#include "vm.c"

static int totali = 0;
static int fallimenti = 0;

#define CHECK(expr) do { \
    totali++; \
    if (!(expr)) { \
        fallimenti++; \
        printf("FALLITO %s:%d  %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static void ci_sono_i_pezzi_indispensabili(void)
{
    Config c;
    char buf[4096];

    config_default(&c);
    CHECK(vm_argomenti(&c, buf, sizeof(buf)) > 0);
    CHECK(strstr(buf, "-M virt") != NULL);
    CHECK(strstr(buf, "-accel whpx") != NULL);
    CHECK(strstr(buf, "-cpu host") != NULL);
    CHECK(strstr(buf, "-m 6144") != NULL);
    CHECK(strstr(buf, "-smp 6") != NULL);
    CHECK(strstr(buf, "virtio-gpu-gl-pci") != NULL);
    CHECK(strstr(buf, "venus=on") != NULL);
    CHECK(strstr(buf, "blob=on") != NULL);
    CHECK(strstr(buf, "-display winq,gl=on") != NULL);
    CHECK(strstr(buf, "virtio-keyboard-pci") != NULL);
    CHECK(strstr(buf, "virtio-multitouch-pci") != NULL);
}

/* La rotella ha bisogno di un mouse, e di un mouse messo in un modo preciso.
 * Queste prove proteggono un meccanismo che, se rotto, fallisce in silenzio da
 * un lato (niente scorrimento) e in modo grosso dall'altro (niente tocco). */
static void il_mouse_c_e_e_non_e_legato_alla_console(void)
{
    Config c;
    char buf[4096];

    config_default(&c);
    CHECK(vm_argomenti(&c, buf, sizeof(buf)) > 0);
    /* Il mouse serve perche' e' l'unico dispositivo che dichiara REL_WHEEL. */
    CHECK(strstr(buf, "-device virtio-mouse-pci ") != NULL);
    /* Il legame degli altri due alla console e' cio' che impedisce al mouse di
     * rubare il tocco: senza, il primo gestore che rivendica i BTN si prende
     * anche INPUT_BUTTON_TOUCH. */
    CHECK(strstr(buf, "id=gpu0") != NULL);
    CHECK(strstr(buf, "virtio-keyboard-pci,display=gpu0") != NULL);
    CHECK(strstr(buf, "virtio-multitouch-pci,display=gpu0") != NULL);
    /* Il mouse NON deve avere display=: se qualcuno glielo aggiungesse "per
     * coerenza", la rotella tornerebbe al multitouch e non scorrerebbe piu',
     * senza un errore da nessuna parte. Questa prova esiste per fermare quella
     * modifica, ed e' il motivo per cui la si scrive al negativo. */
    CHECK(strstr(buf, "virtio-mouse-pci,display") == NULL);
}

static void la_riga_del_kernel_e_citata_una_volta_sola(void)
{
    Config c;
    char buf[4096];
    const char *p;
    int virgolette = 0;

    config_default(&c);
    vm_argomenti(&c, buf, sizeof(buf));
    p = strstr(buf, "-append");
    CHECK(p != NULL);
    if (!p) {
        return;
    }
    /* Deve esserci "console=ttyAMA0 ..." fra virgolette, e le virgolette non
     * devono comparire DENTRO il valore. */
    CHECK(strstr(p, "\"console=ttyAMA0") != NULL);
    CHECK(strstr(p, "\\\"") == NULL);
    for (; *p && *p != '\n'; p++) {
        if (*p == '"') {
            virgolette++;
        }
    }
    CHECK(virgolette == 2);
}

static void la_porta_adb_finisce_nell_inoltro(void)
{
    Config c;
    char buf[4096];

    config_default(&c);
    c.porta_adb = 16000;
    vm_argomenti(&c, buf, sizeof(buf));
    CHECK(strstr(buf, "hostfwd=tcp:127.0.0.1:16000-:5555") != NULL);
}

static void la_risoluzione_diventa_xres_e_yres(void)
{
    Config c;
    char buf[4096];

    config_default(&c);
    c.larghezza = 800;
    c.altezza = 1280;
    vm_argomenti(&c, buf, sizeof(buf));
    CHECK(strstr(buf, "xres=800") != NULL);
    CHECK(strstr(buf, "yres=1280") != NULL);
}

static void l_audio_compare_solo_se_acceso(void)
{
    Config c;
    char buf[4096];

    /* il default e' ACCESO, quindi i due pezzi ci sono senza che
     * nessuno tocchi la configurazione. */
    config_default(&c);
    vm_argomenti(&c, buf, sizeof(buf));
    /* out.fixed-settings=off e' MISURATO, non cosmetico: senza, QEMU apre
     * l'uscita a 44100 e ricampiona i 48000 del guest a ogni frame. Con il tono
     * di prova i campioni a zero -- cioe' i buchi che si sentono come suono
     * scattoso -- passano dal 37% al 14%, e il picco medio da 0,0249 a 0,0360. */
    CHECK(strstr(buf, "-audiodev dsound,id=a0,out.fixed-settings=off") != NULL);
    /* Il buffer host va tenuto PICCOLO e il timer veloce, perche' il guest
     * apre il PCM con 21 ms di buffer: con queste due i buchi passano dal 14%
     * a ZERO. Un buffer grande fa il contrario (misurato: 63%). */
    CHECK(strstr(buf, "out.buffer-length=30000") != NULL);
    CHECK(strstr(buf, "timer-period=2500") != NULL);
    CHECK(strstr(buf, "virtio-sound-pci,audiodev=a0") != NULL);

    /* E spegnendolo devono sparire ENTRAMBI: un audiodev senza il device, o il
     * device senza l'audiodev, e' una riga di comando che QEMU rifiuta. */
    c.audio = false;
    vm_argomenti(&c, buf, sizeof(buf));
    CHECK(strstr(buf, "audiodev") == NULL);
    CHECK(strstr(buf, "virtio-sound-pci") == NULL);
}

static void un_buffer_troppo_piccolo_ritorna_zero(void)
{
    Config c;
    char buf[32];

    config_default(&c);
    CHECK(vm_argomenti(&c, buf, sizeof(buf)) == 0);
}

/* Questo difetto e' stato trovato ESEGUENDO il guscio, non leggendo vm.c: il
 * guscio si fermava con "manca guest/images/initramfs.img" mentre il file
 * vive davvero in guest/images/android/. Nessuna delle prove sopra se ne
 * accorgeva, perche' guardano i pezzi della stringa (flag, numeri, virgolette)
 * e non i percorsi per intero. Il kernel resta in guest/images/ perche' viene
 * da una catena di costruzione diversa (build-guest-kernel.sh) da quella delle
 * immagini del guest (build-all.ps1, che le scrive sotto android/): la prova
 * qui sotto blocca un futuro appiattimento delle due cartelle in una sola. */
static void i_percorsi_delle_immagini_sono_sotto_android(void)
{
    Config c;
    char buf[4096];

    config_default(&c);
    CHECK(vm_argomenti(&c, buf, sizeof(buf)) > 0);
    CHECK(strstr(buf, "-kernel guest/images/kernel-guest-arm64") != NULL);
    CHECK(strstr(buf, "guest/images/android/initramfs.img") != NULL);
    CHECK(strstr(buf, "guest/images/android/system.img") != NULL);
    CHECK(strstr(buf, "guest/images/android/vendor.img") != NULL);
    CHECK(strstr(buf, "guest/images/android/data.img") != NULL);
    /* La forma appiattita e' il difetto: se ricompare, questa deve fallire. */
    CHECK(strstr(buf, "guest/images/system.img") == NULL);
}

/* La variante sceglie quale coppia di immagini finisce nella riga di comando:
 * vedi var_percorsi in varianti.h. vendor.img resta fisso per entrambe le
 * varianti -- tutte le personalizzazioni del progetto viaggiano
 * nell'initramfs, non nelle due immagini di sistema. */
static void la_variante_sceglie_le_immagini(void)
{
    Config c;
    char buf[4096];

    config_default(&c);
    c.variante = VAR_VANILLA;
    CHECK(vm_argomenti(&c, buf, sizeof(buf)) > 0);
    CHECK(strstr(buf, "guest/images/android/system.img") != NULL);
    CHECK(strstr(buf, "guest/images/android/data.img") != NULL);
    CHECK(strstr(buf, "guest/images/android/vendor.img") != NULL);
    CHECK(strstr(buf, "system-gapps.img") == NULL);
    CHECK(strstr(buf, "data-gapps.img") == NULL);

    config_default(&c);
    c.variante = VAR_GAPPS;
    CHECK(vm_argomenti(&c, buf, sizeof(buf)) > 0);
    CHECK(strstr(buf, "guest/images/android/system-gapps.img") != NULL);
    CHECK(strstr(buf, "guest/images/android/data-gapps.img") != NULL);
    CHECK(strstr(buf, "guest/images/android/vendor.img") != NULL);
    CHECK(strstr(buf, "system.img") == NULL);
    CHECK(strstr(buf, "data.img") == NULL);
}

/* Importante 1 della revisione: nessuna prova legava l'elenco
 * vm_necessari (cio' che vm_verifica_file controlla) alla stringa prodotta da
 * vm_argomenti (cio' che QEMU riceve davvero). Se i due elenchi tornano a
 * divergere -- esattamente il difetto appena corretto, con le immagini
 * spostate sotto android/ -- questa prova deve fallire da sola, senza
 * dipendere da quali file esistano sulla macchina: verifica l'INVARIANTE che
 * ogni percorso di vm_necessari che comincia con "guest/" compaia dentro la
 * stringa che vm_argomenti scrive.
 *
 * Si saltano di proposito i percorsi che non cominciano con "guest/":
 * runtime/bin/qemu-nostro.exe e' l'eseguibile stesso, mai un argomento passato
 * a se stesso, e adb.exe non c'entra con la riga di comando di QEMU (lo apre
 * un processo separato). Non e' una svista: sono gli unici due elementi di
 * vm_necessari che non hanno nulla da cercare in buf. */
static void invariante_necessari_in_argomenti(VarNome variante)
{
    Config c;
    char buf[4096];
    int i;
    const char *percorso;

    config_default(&c);
    c.variante = variante;
    CHECK(vm_argomenti(&c, buf, sizeof(buf)) > 0);

    for (i = 0; (percorso = vm_file_necessario(c.variante, i)) != NULL; i++) {
        if (strncmp(percorso, "guest/", 7) != 0) {
            continue;
        }
        CHECK(strstr(buf, percorso) != NULL);
    }
}

/* Importante 1 della revisione FINALE (dopo l'introduzione della variante):
 * la prova sopra chiamava SEMPRE config_default(), cioe' sempre vanilla, e
 * quindi non poteva accorgersi che vm_necessari era rimasto cablato sulla
 * vanilla mentre vm_argomenti sceglieva le immagini giuste per la variante
 * attiva -- proprio il difetto che questa revisione ha trovato. Una prova che
 * non distingue i due casi non prova niente: si esercita quindi l'invariante
 * su ENTRAMBE le varianti, non solo sul default. */
static void ogni_percorso_guest_di_vm_necessari_e_negli_argomenti(void)
{
    invariante_necessari_in_argomenti(VAR_VANILLA);
    invariante_necessari_in_argomenti(VAR_GAPPS);
}

/* Importante 1 della revisione finale, secondo controllo: la prova sopra
 * verifica solo che i file FISSI (kernel, initramfs, vendor.img) siano
 * negli argomenti, per entrambe le varianti -- ma quei tre non cambiano mai,
 * quindi da soli non discriminano il difetto delle due immagini scambiate.
 * Questa prova guarda invece proprio le DUE immagini che dipendono dalla
 * variante: con gapps attiva, l'elenco dei file necessari deve contenere
 * system-gapps.img e data-gapps.img, e NON deve contenere system.img ne'
 * data.img -- se vm_file_necessario tornasse a ignorare la variante e
 * restituire sempre la coppia vanilla, questa prova lo scopre anche senza
 * guardare vm_argomenti. */
static void con_gapps_i_necessari_sono_le_immagini_gapps(void)
{
    int i;
    const char *percorso;
    bool ha_system_gapps = false;
    bool ha_data_gapps = false;
    bool ha_system_vanilla = false;
    bool ha_data_vanilla = false;

    for (i = 0; (percorso = vm_file_necessario(VAR_GAPPS, i)) != NULL; i++) {
        if (!strcmp(percorso, "guest/images/android/system-gapps.img")) {
            ha_system_gapps = true;
        }
        if (!strcmp(percorso, "guest/images/android/data-gapps.img")) {
            ha_data_gapps = true;
        }
        if (!strcmp(percorso, "guest/images/android/system.img")) {
            ha_system_vanilla = true;
        }
        if (!strcmp(percorso, "guest/images/android/data.img")) {
            ha_data_vanilla = true;
        }
    }
    CHECK(ha_system_gapps);
    CHECK(ha_data_gapps);
    CHECK(!ha_system_vanilla);
    CHECK(!ha_data_vanilla);
}

/* Importante 1 della revisione, secondo controllo: qui si chiama
 * vm_verifica_file davvero, sul filesystem di questa macchina. Se i file ci
 * sono tutti deve tornare true. Se ne manca uno il controllo che conta e' che
 * "mancante" non sia vuoto: e' il nome che il guscio mostra a chi lo esegue,
 * e una stringa vuota sarebbe un messaggio d'errore inutile quanto nessun
 * messaggio. */
static void vm_verifica_file_riporta_un_nome_utile_se_manca_qualcosa(void)
{
    Config c;
    char mancante[512];
    bool trovato;

    config_default(&c);
    mancante[0] = '\0';
    trovato = vm_verifica_file(&c, mancante, sizeof(mancante));
    if (trovato) {
        CHECK(trovato);
    } else {
        CHECK(mancante[0] != '\0');
    }
}

/* Bloccante Critico della revisione finale: "chiudere due volte stacca la
 * corrente al guest". La decisione era prima un "if" a due rami inline nel
 * gestore di WM_APP+1 in main.c, su nove stati possibili, e nessuna prova
 * poteva raggiungerla. Estratta in vm_azione_chiusura (vm.c), si prova qui
 * stato per stato: un CHECK per ognuno dei nove VmStato, cosi' la mappatura
 * intera resta protetta, non solo il caso che ha causato il difetto
 * (VM_SPEGNIMENTO). Vedi vm.c per la ragione di ciascuna riga. */
static void ogni_stato_ha_la_sua_azione_di_chiusura(void)
{
    CHECK(vm_azione_chiusura(VM_PREPARA) == CHIUSURA_DURA);
    CHECK(vm_azione_chiusura(VM_AVVIA) == CHIUSURA_DURA);
    CHECK(vm_azione_chiusura(VM_ATTESA_KERNEL) == CHIUSURA_DURA);
    CHECK(vm_azione_chiusura(VM_ATTESA_ANDROID) == CHIUSURA_SPEGNI);
    CHECK(vm_azione_chiusura(VM_PRONTO) == CHIUSURA_SPEGNI);
    /* QUESTO E' IL CASO CHE HA CAUSATO IL BLOCCANTE: prima della correzione,
     * il chiamante trattava VM_SPEGNIMENTO come "altrimenti", cioe' uccisione
     * immediata invece di attesa. */
    CHECK(vm_azione_chiusura(VM_SPEGNIMENTO) == CHIUSURA_ATTENDI);
    CHECK(vm_azione_chiusura(VM_USCITO) == CHIUSURA_DURA);
    CHECK(vm_azione_chiusura(VM_MORTA) == CHIUSURA_DURA);
    CHECK(vm_azione_chiusura(VM_FALLITA) == CHIUSURA_DURA);
}

/* La decisione fra i due messaggi dei due minuti. Vale la pena provarla per la
 * stessa ragione della citazione della riga del kernel: il guasto che chiude si
 * era gia' presentato, e non era un errore di calcolo ma un messaggio che
 * accusava un avvio fermo mentre il guest stava parlando -- stampato 51 volte
 * su una sessione che poi e' arrivata in fondo. */
static void il_guardiano_distingue_vivo_e_lento_da_fermo(void)
{
    /* Prima dichiarazione: vale il totale, perche' non c'e' un prima. */
    CHECK(vm_righe_nuove(0, -1) == 0);
    CHECK(vm_righe_nuove(1, -1) == 1);
    CHECK(vm_righe_nuove(2144, -1) == 2144);

    /* Dichiarazioni successive: conta la differenza, non il totale. E' il
     * cuore del difetto: 2144 righe ferme da due minuti sono un guest fermo,
     * non un guest che ha detto 2144 cose. */
    CHECK(vm_righe_nuove(2144, 2144) == 0);
    CHECK(vm_righe_nuove(2145, 2144) == 1);
    CHECK(vm_righe_nuove(2400, 2144) == 256);

    /* Un totale che torna indietro non produce un numero negativo, che
     * sarebbe "fermo" proprio dove il guest ha ricominciato da capo. */
    CHECK(vm_righe_nuove(0, 2144) == 0);
    CHECK(vm_righe_nuove(10, 2144) == 0);
}

static void il_punto_di_blocco_non_e_il_numero_di_righe(void)
{
    char a[512], b[512], piccolo[8];
    vm_punto_normalizza("[ 35.210] calling chr_dev_init+0x0/0xa8 @ 1\r", a, sizeof(a));
    vm_punto_normalizza("[ 98.789] calling chr_dev_init+0x4/0xb0 @ 42", b, sizeof(b));
    CHECK(!strcmp(a, "calling chr_dev_init"));
    CHECK(!strcmp(a, b));
    /* 164 e 168 righe arrivano allo stesso initcall: il totale non decide. */
    CHECK(vm_riprova_vcpu(6, 164, true, a, b, 6) == 3);
    CHECK(vm_riprova_vcpu(6, 168, true, a, b, 6) == 3);
    CHECK(vm_riprova_vcpu(3, 168, true, a, b, 6) == 3);
    CHECK(vm_riprova_vcpu(3, 168, true, a, b, 3) == 1);
    CHECK(vm_riprova_vcpu(1, 168, true, a, b, 1) == 1);
    vm_punto_normalizza("[ 98.789] calling another_init+0x0/0xa8 @ 1", b, sizeof(b));
    CHECK(vm_riprova_vcpu(6, 164, true, a, b, 6) == 6);
    CHECK(vm_riprova_vcpu(6, 164, false, a, a, 6) == 6);
    CHECK(vm_riprova_vcpu(6, 164, true, "", "", 6) == 6);
    CHECK(vm_riprova_vcpu(6, 0, true, "", "", 0) == 3);
    CHECK(vm_riprova_vcpu(6, 0, false, "", "", 0) == 6);
    vm_punto_normalizza("[ 1.23] ordinary message 123", a, sizeof(a));
    CHECK(!strcmp(a, "ordinary message 123"));
    vm_punto_normalizza("[not a timestamp] preserve this", a, sizeof(a));
    CHECK(!strcmp(a, "[not a timestamp] preserve this"));
    vm_punto_normalizza("[ 1.23]   \r\n", a, sizeof(a));
    CHECK(!a[0]);
    vm_punto_normalizza("different suffixes must not match", piccolo, sizeof(piccolo));
    CHECK(!piccolo[0]);
}

static void il_riavvio_chiude_il_proprio_lettore_prima_di_ruotare(void)
{
    char precedente[MAX_PATH];
    char temporanea[MAX_PATH];
    char radice[MAX_PATH];
    char archivio[MAX_PATH];
    char letto[64] = {0};
    WIN32_FIND_DATAA dato;
    HANDLE ricerca;
    FILE *f;
    Config c;

    CHECK(GetCurrentDirectoryA(sizeof(precedente), precedente) > 0);
    CHECK(GetTempPathA(sizeof(temporanea), temporanea) > 0);
    CHECK(GetTempFileNameA(temporanea, "hvm", 0, radice) != 0);
    CHECK(DeleteFileA(radice));
    CHECK(CreateDirectoryA(radice, NULL));
    CHECK(SetCurrentDirectoryA(radice));
    CHECK(CreateDirectoryA("guest", NULL));
    CHECK(CreateDirectoryA("guest/logs", NULL));
    f = fopen("guest/logs/sessione-viva.log", "wb");
    CHECK(f != NULL);
    if (f) {
        fputs("prova della sessione precedente\n", f);
        fclose(f);
    }
    vm_seriale = fopen("guest/logs/sessione-viva.log", "rb");
    CHECK(vm_seriale != NULL);
    vm_seriale_pos = 0;
    vm_seriale_frammento = false;
    CHECK(vm_segui_seriale() == 1);
    CHECK(!strcmp(vm_punto_seriale, "prova della sessione precedente"));
    f = fopen("guest/logs/sessione-viva.log", "ab");
    CHECK(f != NULL);
    if (f) {
        fputs("[ 35.210] calling chr_dev_init", f);
        fclose(f);
    }
    CHECK(vm_segui_seriale() == 0);
    CHECK(!strcmp(vm_punto_seriale, "prova della sessione precedente"));
    f = fopen("guest/logs/sessione-viva.log", "ab");
    CHECK(f != NULL);
    if (f) {
        fputs("+0x0/0xa8 @ 1\n", f);
        fclose(f);
    }
    CHECK(vm_segui_seriale() == 1);
    CHECK(!strcmp(vm_punto_seriale, "calling chr_dev_init"));
    f = fopen("guest/logs/sessione-viva.log", "ab");
    CHECK(f != NULL);
    if (f) {
        for (int i = 0; i < 1200; i++) { fputc('x', f); }
        fputc('\n', f);
        fclose(f);
    }
    CHECK(vm_segui_seriale() > 1);
    CHECK(!vm_punto_seriale[0]);
    CHECK(!DeleteFileA("guest/logs/sessione-viva.log"));
    CHECK(GetLastError() == ERROR_SHARING_VIOLATION);
    config_default(&c);
    vm_c = &c;
    /* Il lancio fallisce perche' runtime/bin/qemu-nostro.exe non esiste qui.
     * La rotazione deve pero' essere gia' riuscita, senza perdere la prova. */
    CHECK(!vm_lancia());
    CHECK(vm_seriale == NULL);
    CHECK(!vm_punto_seriale[0] && !vm_blocco_silenzio && !vm_seriale_frammento);
    CHECK(GetFileAttributesA("guest/logs/sessione-viva.log") ==
          INVALID_FILE_ATTRIBUTES);
    ricerca = FindFirstFileA("guest/logs/archivio/sessione-viva-*.log", &dato);
    CHECK(ricerca != INVALID_HANDLE_VALUE);
    if (ricerca != INVALID_HANDLE_VALUE) {
        snprintf(archivio, sizeof(archivio), "guest/logs/archivio/%s",
                 dato.cFileName);
        f = fopen(archivio, "rb");
        CHECK(f != NULL);
        if (f) {
            CHECK(fgets(letto, sizeof(letto), f) != NULL);
            CHECK(!strcmp(letto, "prova della sessione precedente\n"));
            fclose(f);
        }
        FindClose(ricerca);
        CHECK(DeleteFileA(archivio));
    }
    vm_chiudi();
    DeleteFileA("guest/logs/sessione-viva.log");
    CHECK(RemoveDirectoryA("guest/logs/archivio"));
    CHECK(RemoveDirectoryA("guest/logs"));
    CHECK(RemoveDirectoryA("guest"));
    CHECK(SetCurrentDirectoryA(precedente));
    CHECK(RemoveDirectoryA(radice));
    vm_c = NULL;
}

/* Esegue il vero vm_lancia con questo eseguibile al posto di QEMU. Nessun
 * guest viene avviato: il figlio misura console e stderr prima di uscire. */
static void qemu_non_crea_console_e_conserva_stderr(void)
{
    char previous[MAX_PATH], temporary[MAX_PATH], root[MAX_PATH], self[MAX_PATH];
    char result[128] = {0}, logs[8192];
    Config c;
    FILE *file;
    bool launched, marker = false;
    CHECK(GetCurrentDirectoryA(sizeof(previous), previous) > 0);
    CHECK(GetModuleFileNameA(NULL, self, sizeof(self)) > 0);
    if (!GetTempPathA(sizeof(temporary), temporary) ||
        !GetTempFileNameA(temporary, "hqc", 0, root)) { CHECK(false); return; }
    CHECK(DeleteFileA(root));
    CHECK(CreateDirectoryA(root, NULL));
    if (!SetCurrentDirectoryA(root)) { CHECK(false); return; }
    CHECK(CreateDirectoryA("runtime", NULL));
    CHECK(CreateDirectoryA("runtime/bin", NULL));
    CHECK(CreateDirectoryA("guest", NULL));
    CHECK(CreateDirectoryA("guest/logs", NULL));
    CHECK(CopyFileA(self, "runtime/bin/qemu-nostro.exe", TRUE));
    config_default(&c);
    vm_c = &c;
    CHECK(SetEnvironmentVariableA("HABUMI_VM_CONSOLE_PROBE", "1"));
    launched = vm_lancia();
    SetEnvironmentVariableA("HABUMI_VM_CONSOLE_PROBE", NULL);
    CHECK(launched);
    if (launched) {
        DWORD code = 0;
        CHECK(WaitForSingleObject(vm_processo, 5000) == WAIT_OBJECT_0);
        CHECK(GetExitCodeProcess(vm_processo, &code) && code == 23);
        CHECK(WaitForSingleObject(vm_thread_stderr, 5000) == WAIT_OBJECT_0);
        file = fopen("console-probe.txt", "rb");
        CHECK(file != NULL);
        if (file) {
            fread(result, 1, sizeof(result) - 1, file);
            fclose(file);
            CHECK(strstr(result, "console=0") != NULL);
            CHECK(strstr(result, "pipe=1") != NULL);
        }
        while (registro_nuove(logs, sizeof(logs)) > 0) {
            if (strstr(logs, "qemu-console-free-stderr")) { marker = true; }
        }
        CHECK(marker);
    }
    vm_chiudi();
    vm_c = NULL;
    CHECK(DeleteFileA("runtime/bin/qemu-nostro.exe"));
    CHECK(DeleteFileA("console-probe.txt"));
    CHECK(RemoveDirectoryA("runtime/bin"));
    CHECK(RemoveDirectoryA("runtime"));
    CHECK(RemoveDirectoryA("guest/logs"));
    CHECK(RemoveDirectoryA("guest"));
    CHECK(SetCurrentDirectoryA(previous));
    CHECK(RemoveDirectoryA(root));
}

int main(void)
{
    if (getenv("HABUMI_VM_CONSOLE_PROBE")) {
        FILE *report = fopen("console-probe.txt", "wb");
        if (!report) { return 2; }
        fprintf(report, "console=%d\npipe=%d\n", GetConsoleWindow() != NULL,
                GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_PIPE);
        fclose(report);
        fprintf(stderr, "qemu-console-free-stderr\n");
        return 23;
    }
    registro_apri();
    ci_sono_i_pezzi_indispensabili();
    il_mouse_c_e_e_non_e_legato_alla_console();
    la_riga_del_kernel_e_citata_una_volta_sola();
    la_porta_adb_finisce_nell_inoltro();
    la_risoluzione_diventa_xres_e_yres();
    l_audio_compare_solo_se_acceso();
    un_buffer_troppo_piccolo_ritorna_zero();
    i_percorsi_delle_immagini_sono_sotto_android();
    la_variante_sceglie_le_immagini();
    ogni_percorso_guest_di_vm_necessari_e_negli_argomenti();
    con_gapps_i_necessari_sono_le_immagini_gapps();
    vm_verifica_file_riporta_un_nome_utile_se_manca_qualcosa();
    ogni_stato_ha_la_sua_azione_di_chiusura();
    il_guardiano_distingue_vivo_e_lento_da_fermo();
    il_punto_di_blocco_non_e_il_numero_di_righe();
    il_riavvio_chiude_il_proprio_lettore_prima_di_ruotare();
    qemu_non_crea_console_e_conserva_stderr();
    registro_chiudi();

    printf("test-vm: %d su %d passati\n", totali - fallimenti, totali);
    return fallimenti ? 1 : 0;
}
