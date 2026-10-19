# Development status

- **Commit:** A0 implementation commit containing this checkpoint; parent
  `b64e11fdee8128697bf8f1c8932f86c6dd169f23`. Recorded 19 October 2026.
- **Completed:** A0 clean QEMU/shell build recipes, pinned inputs, consolidated
  host-power/rotation sources and isolated development package. See the
  [recipe and artifact hashes](DEVELOPMENT-BASELINE-A0.md).
- **Tests:** 2081-step clean QEMU build; full shell build; all 333 ZIP entries
  verified; invalid input/existing output rejected; fresh-data boot, rotation and
  click targeting; three simulated pause/resume cycles; normal close and reopen.
  Live tests ran on 15 October. Final hash/link/source checks ran on 19 October.
- **Open limit:** verified in the isolated profile, not distributed. Guest probes
  remain experimental. Real standby, other hardware, signing and complete
  third-party source rebuilds remain validation gates; upstream warnings are
  recorded. No new GPU-overhead or battery claim follows from A0.
- **Next:** A1 proof: QEMU creates its child window in the shell container;
  introduce versioned session/capability/acknowledgement IPC and asynchronous
  viewport/focus commands, retaining separate-window diagnostics and direct
  texture presentation. Validate resize, focus, input, rotation, dragging and close
  before replacing the interface. See the [implementation plan](IMPLEMENTATION-PLAN.md).
