# Compact native interface

The user prioritized this layout correction after A1, before A2. It implements
the compact presentation part of A3; settings, fullscreen and screenshots remain
separate work. The earlier A1 window was a transport/rendering proof and is not
the intended product layout.

## Chosen design

- Keep the native Windows title bar, window controls, DPI and theme behavior.
- Android fills the client area except for a **36-DIP right command rail**.
  Remove the extra title, startup panel, margins and permanent activity log.
- Show Back, Home, Recents, Rotate and More as small vector buttons with complete
  accessible names, keyboard activation, focus indicators and tooltips.
- More contains window fitting, power, volume, key mapping, Android variant and
  activity log. Existing controllers keep their readiness/disabled states.
- Start at a readable size within 80% of the monitor's available client height.
  Preserve the guest aspect ratio when resizing and maximizing; on rotation,
  retain the long edge where possible and fit the current monitor's work area.
- Show startup/shutdown state in the native title; the ready title is Habumi.
- Open activity only on request in an owned, non-modal tool window. Closing it
  never closes Android. When it is closed, skip edit-control updates; file
  logging continues. The viewer shows retained recent activity; the full session
  remains in `guest/logs/registro-guscio.log`.

The existing diagnostic separate-window launch remains available. This layout
uses the A1 child directly and changes no QEMU texture or rendering path.

## Implementation and checks

Pure layout helpers cover portrait/landscape fitting, scaled rails, invalid and
degenerate dimensions, and bounded arithmetic. A presentation callback from the
ordered controls controller updates window geometry only after the resolution
command was delivered. Controller tests verify the dimensions, startup sync,
failed delivery and that ordinary hardware controls do not trigger the callback.

## Verification on 19 October 2026

- Full shell build and all 13 existing shell suites passed. The ordered controls
  suite passed 197/197 checks. Pure layout tests passed with warnings as errors.
- The hidden, own-process window test passed 30/30 checks, including removal of
  the old panels, view/rail bounds, log close without a shutdown request, log
  reopen and destruction of owned windows. It starts no QEMU process.
- The isolated profile opened with a 488 x 750 logical-pixel outer window and
  approximately 450 x 720 Android view. Maximizing fitted a 562 x 900 portrait
  view; rotation fitted a 900 x 562 landscape view. The rail stayed 36 DIP and
  no large unused presentation bands appeared. Live Home, Calendar targeting
  after rotation and app-drawer gestures passed. In the final build, a generated
  keyboard `c` reached the focused local app search and filtered its results;
  Backspace cleared it. This does not replace physical-keyboard acceptance.
- More and its existing command entries were displayed, and the activity viewer
  opened. A UI automation attempt to close that viewer targeted the root window
  instead, producing normal Android shutdown; this was not counted as a live
  viewer-close pass. The separate hidden regression test covers that lifecycle.
- The acceptance session shut down normally in 5.031 seconds. After installing
  the final shell with backups, launcher validation passed and Android readiness
  took 37.172 seconds. A fresh guest-health snapshot confirmed boot completion
  and live compositor, SystemUI and system_server processes. The final profile
  is left open.

Final package: `Desktop/Habumi-Compact-Final-20261019/distribution/` contains
`Habumi-development-A1.zip`, with all **334 entries verified**. The package keeps
the A1 development identifier; `CompactInterface=true` and source snapshots
identify this revision. Its provenance records the parent commit plus hashes of
all 49 shell input files; all matched the working source after packaging.

| Artifact | SHA-256 |
|---|---|
| Final ZIP | `698B8901E72A452495D56A432092DF3FB44524313AA2963A1056BF9FBF32BD72` |
| Final shell | `0DEA15C845A54B67A73A4CC27559BD57CBAE78F223A7612757B1D94C1DE29D92` |
| Reused A1 QEMU | `4FC0695D5428E29F25F714953884F675D6C354286F107D7DDA0028AFC46742DF` |

`ui-acceptance-shell.log` and `health/compact-final.json` under that output root
record the shutdown and final boot. The earlier `Habumi-Compact-Ready-20261019`
package is superseded. Only the shell and its release/provenance manifests were
replaced in `Desktop/Habumi-A1-Test`, with previous files retained in
`before-compact-interface` and `before-compact-final`. Guest data and the personal
installation were not replaced.

This is a local development interface, not a published release or a new graphics
overhead/battery measurement. Theme changes, multi-monitor DPI transitions,
physical keyboard/touchscreen, real standby and broad hardware validation still
need dedicated acceptance evidence. A3 settings, fullscreen and screenshots
remain unimplemented by this change.
