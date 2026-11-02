# Development status

- **Commit:** publication checkpoint; parent/tag source `bf7768b`.
  Recorded 2 November 2026. Main and the development branch are pushed.
- **Completed:** A2 accepted in the isolated Surface profile: TCG-only growth
  4-to-16 GiB with retained original, real backup/restore, proof-file preservation
  and user-confirmed runtime/data-panel/restart handoff. A3 accepted: settings
  persistence/application, fullscreen, PNG, rotation and physical input verified.
  Issue #3 recovery correction included; its target-hardware cause remains open.
- **Tests:** 13 shell suites / 702 checks, including 28 new retry/serial checks;
  complete development shell build; 57 A3 config/fullscreen checks, screenshot
  failure cases and console-free launcher probe pass. A3 boot 35.515 s; saved
  90 Hz applied after restart (36.000 s), then restored to 120 Hz. Fresh rc.2
  candidate: 347 entries verified, new 4-GiB data, ready 51.578 s, Vulkan device
  enumeration, readable launcher and normal close 5.500 s. Final runtime
  inventory/reboot passed: ready 28.516 s, normal close 4.500 s. Evidence:
  `Builds/Checks/A3-20261102` and `RC2-20261102`.
- **Open limit:** the original hardware-watchdog trigger is unconfirmed. TCG
  removes WHPX from maintenance; no general stability guarantee follows.
  Other hardware and physical power-loss tests remain open. Issue #3's
  `chr_dev_init`/THP slowdown needs raw logs and target-hardware confirmation.
  Personal installation unchanged. The runtime remains an experimental prerelease.
- **Distribution:** [0.3.0-rc.2](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.2)
  published with runtime, corresponding source and SHA256SUMS. GitHub confirms
  all three uploaded SHA-256 hashes. Runtime has 347 verified entries; source
  has 24 entries and matches `bf7768b`, including 19 compiled QEMU overlays.
  Stable remains 0.2.3. Publication receipt is in the rc.2 package folder.
- **Next:** A4 essential app library, launcher-based shortcuts, file export and
  user-initiated staged update/rollback, one bounded package at a time. A5
  signing and broader hardware gates remain open; issue #3 is not closed.
