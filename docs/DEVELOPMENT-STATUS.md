# Development status

- **Commit:** runtime console suppression commit containing this checkpoint;
  parent `ab076bc`. Recorded 19 October 2026; local, not pushed.
- **Completed:** first A2 milestone: shared runtime/maintenance exclusion,
  offline backup, verified staged restore, retained originals, interruption
  recovery and bounded local diagnostic export. Existing A0/A1 and compact UI
  remain. QEMU now starts without a console window and the PowerShell-to-shell
  hop avoids ShellExecute. Use the [workspace layout](WORKSPACE-LAYOUT.md).
- **Tests:** full shell build, 13 suites/674 checks including actual child spawn,
  console/pipe/exit-code checks; 337 package entries and inputs verified. Live
  compact profile boot/health/rendering passed. See
  [console follow-up](CONSOLE-FREE-LAUNCHER.md) and the previous
  [A2 data evidence](A2-DATA-FOUNDATION.md).
- **Open limit:** A2 remains in progress: growth, native data controls and fuller
  diagnostic presentation remain. Local development package, not distributed;
  physical power-loss, broad hardware and prior release gates remain open. No
  new graphics overhead or battery claim. User confirmation/title of the reported
  black window remains pending; background console-host counts do not prove
  visibility.
- **Next:** confirm the reported window's visibility/title with the user, then
  resume A2: build a pinned ARM64 maintenance initramfs with ext4 tools; verify
  4-to-16-GiB growth and file preservation on a disposable copy, then integrate
  transactional growth. See the [implementation plan](IMPLEMENTATION-PLAN.md).
