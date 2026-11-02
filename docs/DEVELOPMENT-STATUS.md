# Development status

- **Commit:** rc.2 preparation commit containing this checkpoint; parent
  `9fdbdcc`. Recorded 2 November 2026; publication pending.
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
  enumeration, readable launcher and normal close 5.500 s. Evidence:
  `Builds/Checks/A3-20261102` and `RC2-20261102`.
- **Open limit:** the original hardware-watchdog trigger is unconfirmed. TCG
  removes WHPX from maintenance; no general stability guarantee follows.
  Other hardware and physical power-loss tests remain open. Issue #3's
  `chr_dev_init`/THP slowdown needs raw logs and target-hardware confirmation.
  Personal install and published release unchanged.
- **Next:** finalize runtime/source archive readback and the final packaged
  reboot, then publish the explicitly requested 0.3.0-rc.2 prerelease. A4 follows;
  A5 signing/broader hardware gates remain open.
