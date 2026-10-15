# Habumi Development Plan — 2026-10-15

## Baselines and working order

Use immutable release `0.3.0-rc.1` at `97153e8` as the release reference. The current local development baseline is `a6fd64b`. Keep release comparisons tied to those identifiers; do not describe historical results as runs performed for this plan.

Work through the phases in order. Each phase has an observable completion condition; defer the next phase until it is met or its remaining external validation is explicitly recorded.

## 1. Android stability: SystemUI crashes and MediaProvider/GMS ANRs

**Objective:** Reproduce and eliminate the reported SystemUI crash and MediaProvider/Google Play services ANRs under the supported Android image and normal app-install/use workload.

**Complete when:** Fresh, timestamped guest logs show no recurrence across a documented repeatable workload and soak period; affected services remain responsive, and the result is compared with the release baseline. Record image/build identity, workload, duration, and log evidence.

**Validation:** Here, inspect source, existing logs and scripts, add or run reproducible host-side checks where they cover shell behavior, and analyze captured guest traces. A decisive Android rebuild requires the Android source/build environment on Linux x86_64. Reproduction and final confirmation require booting the target image and exercising Android; do not infer success from a clean host build or stale logs.

## 2. Resume, standby, and recovery

**Objective:** Make suspend/standby, resume, and recovery paths predictable without leaving Android, QEMU, or the shell in a stuck or orphaned state.

**Complete when:** Repeated documented cycles reach the expected guest and shell states, input and rendering recover, and shutdown still completes cleanly. Record timeouts and recovery behavior for interrupted cycles.

**Validation:** Here, inspect state-machine code and run host-only tests/probes that do not require a live VM. Final evidence needs a live VM on the target Windows ARM64 hardware, including a real standby/resume cycle and checks for audio, input, display, and process cleanup.

## 3. Installation, updates, and image variants

**Objective:** Make first install, app installation, runtime updates, and variant switching recoverable and clear when downloads, extraction, or ADB fail.

**Complete when:** Fresh-install, update, APK-install, and variant-switch flows each have a recorded success case; interrupted/failing cases report an actionable cause and leave a retryable state without corrupting the active image or user data.

**Validation:** Here, run isolated unit and scripted tests for parsing, archives, queues, and failure handling. End-to-end checks require network access, a live Android guest, and representative APKs; variant and update checks should include interruption and retry on the target machine.

## 4. Functional interface and rotation

**Objective:** Keep the native shell controls reachable and truthful while startup, ADB actions, rotation, and variant changes proceed asynchronously.

**Complete when:** The existing controls retain their commands and enable/disable semantics; queued operations do not block the message loop; portrait-to-landscape and reverse rotation leave Android framebuffer and QEMU client dimensions aligned. Failures are visible in the shell log.

**Validation:** Here, build the native shell and run the existing control, startup, and async tests where their prerequisites are available. Rotation and resize/DPI checks need a live guest and a Windows ARM64 display. Preserve the rotation ownership split: the shell queries Android, sends the guest resolution through the named pipe, then applies Android rotation; QEMU updates the display and posts resizing to the HWND-owning thread. Never call window resizing synchronously across those threads.

## 5. Compatibility and distribution

**Objective:** Ship a reproducible package with documented supported host/runtime combinations, correct licenses, a clean upgrade path from the release baseline, and a signed updater. Code signing may depend on an externally supplied signing identity/certificate; availability must be confirmed and must not be assumed.

**Complete when:** A clean package can be assembled from declared inputs, its inventory and hashes are recorded, startup and upgrade work on a clean supported machine, and release notes identify known limits. The updater is signed and its signature is verified, or the external signing identity/certificate dependency is explicitly resolved and documented before claiming completion. No validation result is inferred from an older package.

**Validation:** Here, inspect packaging scripts, manifests, and license inventories and run deterministic packaging checks when dependencies exist. Clean-machine installation, Windows ARM64 execution, and checks across any claimed host/runtime matrix require external machines. Android system-image builds require Linux x86_64.

**Current boundary:** The repository has no automatic updater, and the release
does not claim code signing. A signed updater is future development, with both
an implementation and a supplied signing identity still required. Do not infer
one exists from the packaging scripts or delay unrelated interface work while
waiting for an unavailable certificate. Cross-hardware validation also needs
machines beyond the current development PC.

## 6. Native interface glow-up

**Objective:** Modernize Habumi's Win32 shell after functional behavior is stable, retaining its native accessibility, DPI behavior, copyable diagnostics, and existing actions.

**Chosen direction:** Follow the Windows theme with a clean, compact layout and native controls. Respect the Windows light/dark preference consistently across the shell.

**Bounded changes:** (1) clarify the startup/status area and group hardware actions into a compact, consistently sized toolbar; (2) distinguish power, navigation, and device controls through restrained Windows-native styling while preserving IDs and enabled states; (3) add clear hierarchy to the log with a visible label and readable native styling, keeping selection/copy and current tail/truncation behavior. The existing `fin_disponi` layout in `app/guscio/finestra.c` is the natural home for geometry changes. No new visual assets are established in the app, so do not assume or invent a brand asset set or introduce a framework rewrite.

**Complete when:** The native shell builds and the redesigned controls remain usable at supported DPI settings and narrow/resized window sizes; the functional control and rotation checks from phase 4 still pass, and the log remains selectable and bounded.

**Validation:** Here, review and build the native Win32 implementation. Visual, DPI, focus, accessibility, and live-control checks require the Windows ARM64 target. A browser mockup would not faithfully represent Win32 controls, DPI scaling, focus, or window behavior.

## UI source map

- `app/guscio/finestra.c`: native window, phase/status text, nine hardware buttons, log control, DPI-aware layout, and presentation of log updates.
- `app/guscio/main.c`: message dispatch and state-machine progression; UI events are queued rather than doing long work in window callbacks.
- `app/guscio/comandi.c`: asynchronous ADB control queue and rotation sequence.
- `app/guscio/tubo.c`: shell-to-QEMU named-pipe commands.
- `qemu/ui-winq/winq-ciclo.c`: guest resolution update and request to resize the host window.
- `qemu/ui-winq/winq-window.c`: HWND ownership, input, and posted resize handling.
- `qemu/HOST-WINDOW.md`: threading and window-lifecycle constraints for the QEMU host window.
