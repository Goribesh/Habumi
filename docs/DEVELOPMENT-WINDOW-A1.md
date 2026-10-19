# A1: native child-window proof

A1 is an opt-in development experiment built after A0 (`28250a9`). Use
`Start-A1.cmd` in its isolated package. `Start.cmd` retains the separate-window
diagnostic path. This proof does not replace the final A3 interface.

The shell creates a Win32 container and passes its HWND, owner PID and a random
128-bit session token to QEMU. QEMU validates the parent and its Per-Monitor-V2
DPI context and creates its window with `WS_CHILD` directly. No `SetParent`
transition is used. Both processes and the existing GL/render thread ownership
remain separate. Viewport changes affect the presentation area, not Android's
framebuffer resolution. Rotation keeps the existing shared-texture transform.

The canonical wire definition is `app/guscio/habumi-session.h`; the QEMU build
recipe copies this exact header and records its hash. The local duplex pipe has
one session-specific name, rejects remote clients and verifies peer process IDs.
Version 1 negotiates capabilities for hello, viewport and focus. Frames identify
the session and request sequence, carry an expiry and return completion/error.
Unknown versions, sessions, expired commands and invalid viewports are rejected.

The shell UI only publishes/coalesces intentions. Its worker performs bounded,
cancellable overlapped I/O and validates response identity/capabilities. QEMU's
window thread waits for pipe events together with window messages and applies
viewport/focus operations itself. It never acquires the BQL or moves the GL
context. The event wait introduces no periodic IPC polling while connected.
The existing legacy mapping/rotation pipe remains available; migrating all
commands and names to profile namespaces is later work.

## Reproduction

Use the A0 [pinned inputs and clean QEMU recipe](DEVELOPMENT-BASELINE-A0.md),
then pass `-DevelopmentName A1` to
`app/scripts/prepare-development-baseline.ps1`. Use new output directories.
The package inventory includes `Start-A1.cmd` and marks the child-window mode
experimental. As with A0, provide read-only verified Android images and fresh
user data only in the extracted test profile.

## Verification, 19 October 2026

- Clean isolated QEMU build: 2081 Ninja steps, then recorded incremental rebuilds
  of the window/input modules for final IPC priority and keyboard handling.
  The copied-header timestamp was explicitly refreshed to force a real rebuild;
  `overlay-rebuild.log` and `keyboard-rebuild.log` show compilation and linking.
- Full shell build and all 13 existing suites passed. Startup readiness tests:
  188/188 checks plus VM-state cancellation/retry/readiness coverage passed.
  Updated controls and shutdown recipes also passed: controls 180/180,
  asynchronous ADB 58 checks and the VM shutdown/state checks.
- The fake pipe-server suite passed 16 checks/cases: version/session/expiry and
  geometry validation, response sequence/capability/rejection handling, real
  client-initiated disconnect/reconnect, generation change and cancellation of
  a blocked read in under one second. Five keyboard scan-code checks passed.
- All 334 final archive entries were reopened and hashed; extracted launcher
  validation passed. No installed user data was packaged.
- First candidate: fresh 4 GiB data booted at 64.703 seconds including initial
  disk preparation. Child creation, v1/capabilities handshake, maximization and
  restore, portrait/landscape rotation, Calendar click targeting, Home and an
  app-drawer mouse gesture passed. Normal close took 5.641 seconds.
- During window dragging, 400 guest response samples had a maximum host receive
  gap of 84.918 ms (mean 62.020 ms), with no gap above 500 ms. This is a short UI
  responsiveness probe, not a continuous rendering or power benchmark.
- Boot ID and compositor/SystemUI/system-server identities stayed unchanged
  through uptime 371.86 seconds; the collected crash buffer was empty.
- Final candidate: warm reopen was ready at 29.672 seconds, with a new handshake
  and portrait synchronization. A generated `C` key appeared as `c` in Android's
  local app search. Its normal close completed in 4.531 seconds; a further
  reopen through the Vulkan launcher reached readiness in 36.640 seconds,
  with a new session handshake and a ready guest-health snapshot.

The keyboard probe exposed injected Windows key events with scan code zero.
Only those events now use the host virtual-key map; supplied physical scan
codes keep their original path. Dedicated navigation keys retain their E0
prefix even when the current Italian layout omits it from `MapVirtualKeyEx`.
Unicode `VK_PACKET` text injection is not added by this change.

| Final artifact | SHA-256 |
| --- | --- |
| QEMU | 4FC0695D5428E29F25F714953884F675D6C354286F107D7DDA0028AFC46742DF |
| Shell | 7AC653638D685E18A7C97F83336430F42776CAB26F997D39C2632455244A9003 |
| Development ZIP | CF090867EB2B07B63605B06186D45359E2EB58C915F29B5A4B87682EAFE3D7AD |

Final package: `Desktop/Habumi-A1-Ready-20261019/distribution/Habumi-development-A1.zip`.
Build/receipts: `Desktop/Habumi-A1-Qemu-20261019`. Test profile:
`Desktop/Habumi-A1-Test`. Initial UI/drag/crash evidence is preserved under
`Desktop/Habumi-A1-Verified-20261019`; that earlier package and
`Habumi-A1-Final-20261019` are superseded, not the final candidate.

Multi-monitor DPI transitions, physical touchscreen input, other graphics
hardware and real standby require separate evidence. The live keyboard check
used generated key events; a human physical-keyboard confirmation was not
collected. A successful proof is not a new battery/graphics-overhead measurement
or a distributed release. The final interface and additional commands remain
A3 and later work.
