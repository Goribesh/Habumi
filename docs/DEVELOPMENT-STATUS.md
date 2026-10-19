# Development status

- **Commit:** compact interface commit containing this checkpoint; parent
  `c2c5a86`. Recorded 19 October 2026; local, not pushed.
- **Completed:** user-prioritized compact presentation after A1: native frame,
  Android view and 36-DIP right rail; aspect-fitted resizing/rotation/maximize;
  existing controls in More and activity viewer on request. Separate shell/QEMU
  processes and direct texture presentation remain. This completes only the
  presentation portion of A3. `Start-A1.cmd` selects the development interface.
- **Tests:** full shell build, 13 shell suites, controls 197/197, pure layout and
  hidden window/log lifecycle 30/30; all 334 package entries and 49 shell source
  hashes verified. Live portrait/landscape, mouse, generated keyboard input,
  gestures, maximize, normal shutdown/reopen and final guest-health check passed. See
  [compact interface evidence](COMPACT-INTERFACE-2026-10-19.md).
- **Open limit:** local development profile, not distributed. A3 settings,
  fullscreen and screenshots remain; physical keyboard/touchscreen, theme/DPI
  transitions, other hardware, real standby and production guest replacements
  need further evidence. No new overhead or battery claim.
- **Next:** A2 offline data protection: define the profile maintenance/startup
  exclusion and backup manifest, then implement staged hash-verified restore
  against a disposable copy before storage growth. See the
  [implementation plan](IMPLEMENTATION-PLAN.md).
