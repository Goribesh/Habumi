# Development status

- **Commit:** graphical launcher commit containing this checkpoint; parent
  `357e62a`. Recorded 19 October 2026; local, not pushed.
- **Completed:** A0/A1 and compact presentation portion of A3, now with a native
  GUI bootstrap that prepares Vulkan without creating a console. The isolated
  profile has a direct desktop shortcut, Habumi Compact (Test). See
  [compact interface evidence](COMPACT-INTERFACE-2026-10-19.md).
- **Tests:** both bootstrap modes passed console, path, working directory,
  arguments and error/exit-code probes. Full package compilation and all 335 ZIP
  entries verified; real launcher validation passed without restarting Android.
  Existing shell/QEMU source is unchanged. See
  [launcher evidence](CONSOLE-FREE-LAUNCHER.md).
- **Open limit:** local development profile, not distributed. A3 settings,
  fullscreen and screenshots remain; physical keyboard/touchscreen, theme/DPI
  transitions, other hardware, real standby and production guest replacements
  need further evidence. Cold startup through the new shortcut awaits user
  acceptance. No new overhead or battery claim.
- **Next:** A2 offline data protection: define the profile maintenance/startup
  exclusion and backup manifest, then implement staged hash-verified restore
  against a disposable copy before storage growth. See the
  [implementation plan](IMPLEMENTATION-PLAN.md).
