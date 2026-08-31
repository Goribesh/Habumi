#!/bin/sh
# Porta il timer sintetico di Hyper-V (stimer0) sul kernel ARM64 del guest.
#
# NON SI SPEDISCE. Il kernel di serie non ha nulla di tutto questo: lo script si
# chiama solo con KERNEL_HYPERV=1 nell'ambiente di build-guest-kernel.sh, e senza
# quella variabile il kernel prodotto e' identico a quello di sempre. E' materiale
# di ricerca sull'issue #1, tenuto riproducibile finche' il campo non ne giustifica
# la spedizione.
#
# PERCHE' ESISTE. Su X1P-42 (Surface Pro 12) Hyper-V virtualizza il timer
# architetturale per le partizioni WHP senza funzioni sintetiche e perde gli
# interrupt: il guest resta in attesa di un timer che non arriva mai e l'avvio
# passa da 5 secondi a due ore. Concedere alla partizione le funzioni sintetiche
# (qemu/patches/qemu-hv-sintetici.patch) ripara l'host; questa e' l'altra meta',
# quella del guest, che smette di dipendere dal timer rotto e usa lo stimer0 di
# Hyper-V piu' la pagina TSC. Misurato: il guest gira su stimer (PPI 24) e pagina
# TSC, avvio in 21,6 s sulla macchina sana.
#
# COSA TOCCA, e perche' ognuna delle quattro:
#
#  1. drivers/hv/Kconfig — HYPERV_TIMER e' "def_bool HYPERV && X86". Il driver e'
#     gia' scritto per essere multi-architettura (ha tutto il ramo per-cpu IRQ che
#     x86 non usa), ma su ARM64 non veniva mai compilato.
#
#  2. drivers/clocksource/hyperv_timer.c — la via ACPI presuppone le tabelle. Un
#     guest avviato con -kernel su QEMU virt ha solo il device tree: CONFIG_ACPI=y
#     ma acpi_disabled a runtime, quindi acpi_register_gsi fallirebbe. Si aggiunge
#     la via OF, che mappa la PPI direttamente sul dominio del GIC. E si aggiunge
#     una initcall autonoma, perche' su x86 clocksource e stimer li avvia la catena
#     di VMBus e qui VMBus non c'e' affatto (niente ACPI, nessun nodo nel DT):
#     senza quella, il timer sintetico resterebbe compilato e mai usato.
#
#  3. arch/arm64/include/asm/hyperv_timer.h — NUOVO, il gemello dell'header x86.
#     hyperv_timer.c usa tre x86-ismi: hv_get_raw_timer, hv_raw_get_msr e
#     __bss_decrypted. Su ARM64 il primo e' il contatore architetturale, il secondo
#     coincide con la via normale (non c'e' paravisor da scavalcare) e il terzo e'
#     un attributo SEV che qui non serve.
#
#  4. arch/arm64/hyperv/mshyperv.c — la rilevazione. Le due vie di serie non
#     vedono questo ambiente: l'ACPI non c'e', e lo UUID SMCCC dell'hypervisor e'
#     quello di QEMU, non di Hyper-V. Ma le funzioni sintetiche sono davvero
#     concesse alla partizione e le ipercall rispondono. Si aggiunge quindi un
#     parametro di riga di comando, SPENTO DI DEFAULT.
#
# PERCHE' UN PARAMETRO E NON UNA FORZATURA. Per una settimana questo albero ha
# portato "return true" al posto della rilevazione SMCCC, con un commento che
# diceva di non committarlo. Un kernel compilato cosi' esegue ipercall Hyper-V
# in qualunque ambiente: dove la partizione NON ha le funzioni sintetiche — cioe'
# ovunque senza la patch QEMU — muore all'avvio. Chiunque avesse ricompilato da
# quell'albero avrebbe prodotto un kernel guasto senza saperlo. Con hyperv.force
# il default e' il comportamento di serie e la forzatura e' una scelta scritta
# sulla riga di comando del guest.
#
# LO SCRIPT DISINNESCA QUELLA FORZATURA se la trova: e' il punto per cui e' nato.
#
# IDEMPOTENTE: rieseguirlo su un albero gia' modificato non fa nulla e esce 0.
set -eu

ALBERO="${1:-}"
if [ -z "$ALBERO" ] || [ ! -d "$ALBERO/drivers/clocksource" ]; then
    echo "uso: $0 <albero-del-kernel>"
    echo "     esempio: $0 \$HOME/linux-6.18.35"
    exit 1
fi

KCONFIG="$ALBERO/drivers/hv/Kconfig"
TIMER="$ALBERO/drivers/clocksource/hyperv_timer.c"
MSHV="$ALBERO/arch/arm64/hyperv/mshyperv.c"
HEADER="$ALBERO/arch/arm64/include/asm/hyperv_timer.h"
for f in "$KCONFIG" "$TIMER" "$MSHV"; do
    [ -f "$f" ] || { echo "FERMO: manca $f"; exit 1; }
done

fatto=0

# --- 0. la forzatura di prova, se e' rimasta nell'albero ---------------------
# Va per prima: un albero con dentro il "return true" produce un kernel che muore
# all'avvio ovunque manchi il grant, e quello e' il difetto piu' caro dei cinque.
if grep -q 'WINQ: prova forzata' "$MSHV"; then
    python3 - "$MSHV" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
a = "\treturn true; /* WINQ: prova forzata, non committare */\n"
b = "\treturn arm_smccc_hypervisor_has_uuid(&hyperv_uuid);\n"
if a not in t:
    sys.exit("la forzatura c'e' ma non nella forma attesa: guardare a mano")
open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  0/5 forzatura di prova: DISINNESCATA, rimessa la rilevazione SMCCC"
    fatto=1
else
    echo "  0/5 forzatura di prova: assente"
fi

# --- 1. HYPERV_TIMER anche su ARM64 -----------------------------------------
if grep -q 'def_bool HYPERV && (X86 || ARM64)' "$KCONFIG"; then
    echo "  1/5 Kconfig: gia' aperto ad ARM64"
else
    python3 - "$KCONFIG" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
a = "config HYPERV_TIMER\n\tdef_bool HYPERV && X86\n"
b = "config HYPERV_TIMER\n\tdef_bool HYPERV && (X86 || ARM64)\n"
if a not in t:
    sys.exit("HYPERV_TIMER non e' nella forma attesa: albero diverso da 6.18?")
open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b, 1))
PY
    echo "  1/5 Kconfig: HYPERV_TIMER aperto ad ARM64"
    fatto=1
fi

# --- 2. l'header ARM64, se non c'e' -----------------------------------------
if [ -f "$HEADER" ]; then
    echo "  2/5 asm/hyperv_timer.h: gia' presente"
else
    mkdir -p "$(dirname "$HEADER")"
    cat > "$HEADER" <<'EOF'
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * winq (esperimento issue #1): il gemello ARM64 dell'header x86. La lettura
 * grezza del tempo, per lo stimer di Hyper-V, e' il contatore architetturale.
 *
 * I due shim sotto coprono x86-ismi usati da hyperv_timer.c:
 *  - hv_raw_get_msr su x86 legge l'MSR scavalcando il paravisor; su ARM64 il
 *    paravisor non c'e' e la via normale (hv_get_msr -> hv_get_vpreg) E' la
 *    via cruda.
 *  - __bss_decrypted e' l'attributo SEV di x86 per la memoria condivisa in
 *    chiaro; su ARM64 senza memoria cifrata l'attributo e' vuoto.
 */
#ifndef _ASM_ARM64_HYPERV_TIMER_H
#define _ASM_ARM64_HYPERV_TIMER_H

#include <clocksource/arm_arch_timer.h>
#include <asm/mshyperv.h>

#define hv_get_raw_timer() arch_timer_read_counter()

#ifndef hv_raw_get_msr
#define hv_raw_get_msr(reg) hv_get_msr(reg)
#endif

#ifndef __bss_decrypted
#define __bss_decrypted
#endif

#endif
EOF
    echo "  2/5 asm/hyperv_timer.h: creato"
    fatto=1
fi

# --- 3. hyperv_timer.c: la via OF e l'initcall autonoma ----------------------
# La guardia cerca hv_setup_stimer0_irq_of, non HYPERV_STIMER0_VECTOR: quel nome
# compare gia' nel file vergine (e' il vettore che la via ACPI passa a
# acpi_register_gsi, definito su x86 in asm/mshyperv.h), quindi come marcatore
# della nostra modifica direbbe sempre "gia' fatto" e salterebbe il passo. Preso
# dalla verifica in coda a questo script alla prima prova su albero vergine.
if grep -q 'hv_setup_stimer0_irq_of' "$TIMER"; then
    echo "  3/5 hyperv_timer.c: gia' modificato"
else
    python3 - "$TIMER" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
n = 0

def sostituisci(prima, dopo, cosa):
    global t, n
    if prima not in t:
        sys.exit("ancora non trovata (%s): albero diverso da 6.18?" % cosa)
    t = t.replace(prima, dopo, 1)
    n += 1

# 3a. il vettore della PPI e gli include del device tree
a = "#include <asm/mshyperv.h>\n\nstatic struct clock_event_device __percpu *hv_clock_event;\n"
b = ("#include <asm/mshyperv.h>\n"
     "\n"
     "/*\n"
     " * winq (esperimento issue #1): su ARM64 il vettore del modo diretto dello\n"
     " * stimer0 e' un INTID del GIC che sceglie IL GUEST, non una costante\n"
     " * dell'architettura. Sulla macchina virt di QEMU le PPI occupate sono 23\n"
     " * (PMU), 26-28 e 30 (i timer architetturali): la 24 e' libera.\n"
     " */\n"
     "#ifdef CONFIG_ARM64\n"
     "#include <linux/of.h>\n"
     "#include <linux/of_irq.h>\n"
     "#include <linux/irqdomain.h>\n"
     "#define HYPERV_STIMER0_VECTOR\t24\n"
     "#endif\n"
     "\n"
     "static struct clock_event_device __percpu *hv_clock_event;\n")
sostituisci(a, b, "include e vettore")

# 3b. la via OF, accanto a quella ACPI
a = "void __weak hv_remove_stimer0_handler(void)\n{\n};\n\n#ifdef CONFIG_ACPI\n"
b = ("void __weak hv_remove_stimer0_handler(void)\n"
     "{\n"
     "};\n"
     "\n"
     "#ifdef CONFIG_ARM64\n"
     "/*\n"
     " * winq: la via ACPI qui sotto presuppone le tabelle, e un guest avviato con\n"
     " * -kernel su QEMU virt ha solo il device tree (CONFIG_ACPI=y ma\n"
     " * acpi_disabled a runtime, quindi acpi_register_gsi fallirebbe). La PPI si\n"
     " * mappa direttamente sul dominio del GIC: stessa strada che il device tree\n"
     " * farebbe, senza pretendere un nodo che non c'e'.\n"
     " */\n"
     "static bool stimer0_da_acpi;\n"
     "\n"
     "static int hv_setup_stimer0_irq_of(void)\n"
     "{\n"
     "\tstruct device_node *gic;\n"
     "\tstruct irq_fwspec fwspec;\n"
     "\tint ret;\n"
     "\n"
     "\tgic = of_find_compatible_node(NULL, NULL, \"arm,gic-v3\");\n"
     "\tif (!gic) {\n"
     "\t\tpr_err(\"stimer0: GIC v3 non trovato nel device tree\\n\");\n"
     "\t\treturn -ENODEV;\n"
     "\t}\n"
     "\tfwspec.fwnode = of_node_to_fwnode(gic);\n"
     "\tfwspec.param_count = 3;\n"
     "\tfwspec.param[0] = 1;\t/* GIC_PPI */\n"
     "\tfwspec.param[1] = HYPERV_STIMER0_VECTOR - 16;\n"
     "\tfwspec.param[2] = IRQ_TYPE_EDGE_RISING;\n"
     "\tstimer0_irq = irq_create_fwspec_mapping(&fwspec);\n"
     "\tof_node_put(gic);\n"
     "\tif (stimer0_irq <= 0) {\n"
     "\t\tpr_err(\"stimer0: mappatura della PPI %d fallita\\n\",\n"
     "\t\t       HYPERV_STIMER0_VECTOR);\n"
     "\t\tstimer0_irq = -1;\n"
     "\t\treturn -EINVAL;\n"
     "\t}\n"
     "\tret = request_percpu_irq(stimer0_irq, hv_stimer0_percpu_isr,\n"
     "\t\t\"Hyper-V stimer0\", &stimer0_evt);\n"
     "\tif (ret) {\n"
     "\t\tpr_err(\"stimer0: request_percpu_irq %d fallita, %d\\n\",\n"
     "\t\t       stimer0_irq, ret);\n"
     "\t\tstimer0_irq = -1;\n"
     "\t}\n"
     "\treturn ret;\n"
     "}\n"
     "#endif\n"
     "\n"
     "#ifdef CONFIG_ACPI\n")
sostituisci(a, b, "via OF")

# 3c. il bivio: senza ACPI a runtime si passa dal device tree
a = "\tint ret;\n\n\tret = acpi_register_gsi(NULL, HYPERV_STIMER0_VECTOR,\n"
b = ("\tint ret;\n"
     "\n"
     "#ifdef CONFIG_ARM64\n"
     "\tif (acpi_disabled)\n"
     "\t\treturn hv_setup_stimer0_irq_of();\n"
     "\tstimer0_da_acpi = true;\n"
     "#endif\n"
     "\tret = acpi_register_gsi(NULL, HYPERV_STIMER0_VECTOR,\n")
sostituisci(a, b, "bivio ACPI/OF")

# 3d. la rimozione: si de-registra dall'ACPI solo cio' che ci era stato registrato
a = ("\t\tfree_percpu_irq(stimer0_irq, &stimer0_evt);\n"
     "\t\tacpi_unregister_gsi(stimer0_irq);\n")
b = ("\t\tfree_percpu_irq(stimer0_irq, &stimer0_evt);\n"
     "#ifdef CONFIG_ARM64\n"
     "\t\tif (stimer0_da_acpi)\n"
     "\t\t\tacpi_unregister_gsi(stimer0_irq);\n"
     "#else\n"
     "\t\tacpi_unregister_gsi(stimer0_irq);\n"
     "#endif\n")
sostituisci(a, b, "rimozione simmetrica")

# 3e. l'initcall autonoma, perche' qui non c'e' VMBus ad avviare la catena
a = "\nvoid __init hv_remap_tsc_clocksource(void)\n"
b = ("\n#ifdef CONFIG_ARM64\n"
     "/*\n"
     " * winq: su x86 clocksource e stimer li avvia la catena di VMBus. Questo\n"
     " * guest non ha VMBus (niente ACPI, niente nodo nel device tree): senza\n"
     " * qualcuno che chiami hv_init_clocksource e hv_stimer_alloc, il timer\n"
     " * sintetico resterebbe compilato e mai usato. L'initcall e' tardo di\n"
     " * proposito: le percpu IRQ vogliono il GIC e i cpuhp gia' in piedi.\n"
     " */\n"
     "static int __init hv_stimer_autonomo_init(void)\n"
     "{\n"
     "\tif (!hv_is_hyperv_initialized())\n"
     "\t\treturn 0;\n"
     "\thv_init_clocksource();\n"
     "\treturn hv_stimer_alloc(true);\n"
     "}\n"
     "device_initcall(hv_stimer_autonomo_init);\n"
     "#endif\n"
     "\nvoid __init hv_remap_tsc_clocksource(void)\n")
sostituisci(a, b, "initcall autonoma")

if n != 5:
    sys.exit("applicate %d modifiche su 5" % n)
open(p, "w", encoding="utf-8", newline="\n").write(t)
PY
    echo "  3/5 hyperv_timer.c: via OF, bivio, rimozione e initcall autonoma"
    fatto=1
fi

# --- 4. la rilevazione per riga di comando ----------------------------------
if grep -q 'hyperv.force' "$MSHV"; then
    echo "  4/5 mshyperv.c: parametro hyperv.force gia' presente"
else
    python3 - "$MSHV" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
n = 0

def sostituisci(prima, dopo, cosa):
    global t, n
    if prima not in t:
        sys.exit("ancora non trovata (%s): albero diverso da 6.18?" % cosa)
    t = t.replace(prima, dopo, 1)
    n += 1

# 4a. l'include per kstrtobool
a = "#include <linux/cpuhotplug.h>\n"
b = "#include <linux/cpuhotplug.h>\n#include <linux/kstrtox.h>\n"
sostituisci(a, b, "include kstrtox")

# 4b. il parametro, accanto alle due rilevazioni di serie
a = "static bool __init hyperv_detect_via_smccc(void)\n"
b = ("/*\n"
     " * winq (esperimento issue #1): le due rilevazioni sopra non vedono questo\n"
     " * ambiente. Il guest gira su QEMU con WHPX: l'ACPI non c'e' (avvio con\n"
     " * -kernel, solo device tree) e lo UUID SMCCC dell'hypervisor e' quello di\n"
     " * QEMU, che e' il mediatore. Ma sotto c'e' Hyper-V per davvero, e con il\n"
     " * grant delle funzioni sintetiche (SyntheticProcessorFeaturesBanks, vedi\n"
     " * qemu/patches/qemu-hv-sintetici.patch) le ipercall rispondono.\n"
     " *\n"
     " * SPENTO DI DEFAULT, e non e' prudenza formale: un kernel che rilevasse\n"
     " * Hyper-V sempre eseguirebbe hv_get_vpreg anche dove la partizione non ha\n"
     " * le funzioni sintetiche, cioe' ovunque manchi quel grant, e li' morirebbe\n"
     " * all'avvio.\n"
     " */\n"
     "static bool hyperv_forzato __initdata;\n"
     "\n"
     "static int __init hyperv_imposta_forzatura(char *arg)\n"
     "{\n"
     "\tif (!arg) {\n"
     "\t\thyperv_forzato = true;\n"
     "\t\treturn 0;\n"
     "\t}\n"
     "\treturn kstrtobool(arg, &hyperv_forzato);\n"
     "}\n"
     "early_param(\"hyperv.force\", hyperv_imposta_forzatura);\n"
     "\n"
     "static bool __init hyperv_detect_via_smccc(void)\n")
sostituisci(a, b, "parametro hyperv.force")

# 4c. il bivio in hyperv_init
a = ("\tif (!hyperv_detect_via_acpi() && !hyperv_detect_via_smccc())\n"
     "\t\treturn 0;\n")
b = ("\tif (hyperv_forzato)\n"
     "\t\tpr_info(\"Hyper-V: rilevazione forzata da hyperv.force\\n\");\n"
     "\telse if (!hyperv_detect_via_acpi() && !hyperv_detect_via_smccc())\n"
     "\t\treturn 0;\n")
sostituisci(a, b, "bivio in hyperv_init")

if n != 3:
    sys.exit("applicate %d modifiche su 3" % n)
open(p, "w", encoding="utf-8", newline="\n").write(t)
PY
    echo "  4/5 mshyperv.c: aggiunto il parametro hyperv.force, spento di default"
    fatto=1
fi

# --- 5. verifica, sempre: e' il punto dello script ---------------------------
echo "=== verifica"
mancanti=0
for coppia in \
    "$KCONFIG:def_bool HYPERV && (X86 || ARM64)" \
    "$TIMER:define HYPERV_STIMER0_VECTOR" \
    "$TIMER:hv_setup_stimer0_irq_of" \
    "$TIMER:stimer0_da_acpi" \
    "$TIMER:hv_stimer_autonomo_init" \
    "$HEADER:hv_get_raw_timer" \
    "$MSHV:hyperv.force" \
    "$MSHV:hyperv_forzato" \
    "$MSHV:arm_smccc_hypervisor_has_uuid"; do
    f="${coppia%%:*}"
    s="${coppia#*:}"
    if [ -f "$f" ] && grep -qF "$s" "$f"; then
        printf "  %-38s %s\n" "$s" "in $(basename "$f")"
    else
        printf "  %-38s MANCANTE in %s\n" "$s" "$(basename "$f")"
        mancanti=1
    fi
done
# E il contrario: che la forzatura NON ci sia. E' meta' del motivo di esistere
# di questo script, quindi si verifica invece di darla per fatta.
if grep -q 'WINQ: prova forzata' "$MSHV"; then
    printf "  %-38s ANCORA PRESENTE in mshyperv.c\n" "forzatura di prova"
    mancanti=1
else
    printf "  %-38s assente, come deve essere\n" "forzatura di prova"
fi
[ "$mancanti" -eq 0 ] || { echo "FERMO: la modifica non e' completa."; exit 1; }
[ "$fatto" -eq 1 ] && echo "=== modifica applicata" || echo "=== nulla da fare, era gia' applicata"
exit 0
