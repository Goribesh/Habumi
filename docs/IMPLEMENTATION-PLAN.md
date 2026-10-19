# Implementation plan

Work is divided into packages, with one package per development session. It reuses the existing Android guest, QEMU, Venus, and renderer components where possible. The final product uses one compact native Win32 window following the Windows theme, with English UI/documentation and a portable installation. Development uses an isolated profile; source builds, tested builds, and distributed packages are tracked as distinct artifacts. A 10% graphics overhead is a workload-specific goal, not a guarantee.

## A — Reproducible baseline and first delivery

### A0 — Pin and reproduce the development baseline

Pin source archives, versions, patches, build inputs, and hashes so another checkout can reproduce the current development runtime without edits to an untracked QEMU tree. The QEMU source offer `research/results/release-source-offer/qemu-source-full.tar.gz` has SHA-256 `5A6EEA88B47B691EB9F432702110D2DF83DB05BE4639A174CABD8874FC8E0A13`; comparison of archive member hashes confirms it contains the exact cached host-power/resource source. Build from that pinned archive and the consolidated WinQ modules. Pin the renderer, Mesa/OpenGL runtime, kernel, initramfs, toolchain, and guest image provenance alongside the QEMU inputs.

**Acceptance:** a clean source preparation and build records inputs and output hashes; an extracted package with fresh data boots and closes normally. See [the A0 recipe and evidence](DEVELOPMENT-BASELINE-A0.md) and [the current checkpoint](DEVELOPMENT-STATUS.md).

### A1 — Native window and process session

Create the compact native Win32 window and QEMU child directly, without `SetParent`. Use asynchronous version, capability, session, and acknowledgement IPC. Keep rendering in separate processes with direct shared-texture presentation and no frame copies. Declare Per-Monitor-V2 DPI awareness.

**Acceptance:** launch, handshake, resize, display, and close a development session repeatedly; confirm separate process ownership, direct texture sharing, no frame-copy path, and correct per-monitor DPI behavior.

### A2 — Safe data and diagnostics

Provide offline backup and restore for configuration, keymaps, and all user data, with hashes and staging. Grow data only from 4 GiB through 16 GiB using a maintenance guest and ext4 tools, without requiring user-managed WSL. Record migration metadata and diagnostic export for G26, G27, G29, and H01.

**Acceptance:** restore a verified backup into an isolated profile; reject corrupt or incomplete staged data; validate grow-only resizing and the exported migration and diagnostic metadata.

### A3 — Settings and display controls

Preserve settings, comments, and unknown configuration keys during edits; save atomically. Add reversible fullscreen, on-request PNG capture, and rotation behavior for G01, G09, G21, and H03.

**Acceptance:** round-trip known and unknown settings, survive interrupted saves, toggle fullscreen without losing window state, capture on request, and verify rotation across supported orientations.

### A4 — Library, export, and manual updates

Implement library behavior for G17 and shortcuts for G18 through the Vulkan launcher, waiting for Android readiness. Add file export for G19 and manual staged update/rollback for G25 and G26. Do not add automatic background updates.

**Acceptance:** launch a library item only after readiness, create and use shortcuts, export files, and complete a staged update and rollback without changing user data unexpectedly.

### A5 — Release validation and provenance

Validate V01–V07 and complete signing and provenance work H04.

**Acceptance:** focused checks cover each supported path; a clean full build and isolated live session pass; the signed distributable has traceable source and build provenance. Keep source-built, tested, and distributed artifacts separately identified.

## B — Measurement and energy

Address G02–G05 and H02, then validate V08 with repeatable workload-specific measurements, including energy. Report the workloads and uncertainty; treat 10% overhead as a goal rather than a platform-wide promise.

## C — Input and mapping

Start with the mapper and XInput path (G34–G39, G41, G45), then address G10 and G12. Validate mapping, focus, reconnect, and device removal in the native window.

## D — Namespaces

Implement G30 and G31 first, then G32 and G33. Namespace mutexes, events, pipes, ports, clipboard helpers and writable disks, retaining per-instance process ownership.

## E — Multimedia

Address G06–G08, G20, G22, G28, G46, G50, and G51. Reuse current guest components and validate audio/video behavior in the isolated profile.

## F — Remaining and optional work

Schedule remaining P3 items as optional follow-up after the first delivery; they do not gate package A. S01–S05 are outside the first delivery.

## Verification cadence

For each package, run focused tests for the changed behavior, then a full build and live isolated session at the end of the package. Avoid repeating unrelated suites between packages. Record the exact source state, inputs, tests, and artifact hashes for each checkpoint.

Keep a short checkpoint with commit, completed work, tests, open limit and the exact next step. Use at most two GPT-6-luna subagents for inventories, mechanical checks or independent edits, with disjoint file ownership and reports of at most ten lines. Central review owns rendering, concurrency, IPC and disk operations. External hardware or signing gates do not stop independent development.
