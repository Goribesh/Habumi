# Development status

- **Commit:** A1 implementation commit containing this checkpoint; parent
  `28250a96b06ad8278b0e92c30f56d89a2145b411`. Recorded 19 October 2026.
- **Completed:** A0 baseline and A1 opt-in native child-window proof, with separate
  shell/QEMU processes, direct texture presentation and versioned asynchronous
  session/capability/completion IPC. `Start-A1.cmd` selects the experiment;
  ordinary startup retains the diagnostic separate-window path.
- **Tests:** clean full QEMU and shell builds; existing shell/readiness suites;
  IPC and keyboard regression checks; 334 archive entries; isolated boots,
  rotation/clicks/keyboard, maximize/restore, dragging and normal close/reopen.
  See [A1 evidence and hashes](DEVELOPMENT-WINDOW-A1.md).
- **Open limit:** locally verified proof, not distributed. Physical keyboard and
  touchscreen, multiple monitors, other hardware, real standby and guest-probe
  production replacements remain explicit validation work. No new overhead or
  battery claim. Final UI is A3.
- **Next:** A2 offline data protection: define the profile maintenance/startup
  exclusion and backup manifest, then implement staged hash-verified restore
  against a disposable copy before storage growth. See the
  [implementation plan](IMPLEMENTATION-PLAN.md).
