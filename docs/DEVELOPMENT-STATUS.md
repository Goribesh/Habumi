# Development status

- **Commit:** A3 implementation commit containing this checkpoint; parent
  `4af97b7`. Recorded 2 November 2026; local, not pushed.
- **Completed:** A2 accepted in the isolated Surface profile: TCG-only growth
  4-to-16 GiB with retained original, real backup/restore, proof-file preservation
  and user-confirmed runtime/data-panel/restart handoff. A3 work remains in
  progress: atomic settings, reversible fullscreen and on-request PNG are built
  and running in the isolated profile. Issue #3 retry correction is local.
- **Tests:** 13 shell suites / 702 checks, including 28 new retry/serial checks;
  complete development shell build; 57 A3 config/fullscreen checks, screenshot
  failure cases and console-free launcher probe pass. The 347-entry package
  boots in 35.515 s; real Home PNG, A2 proof hash and 16-GiB capacity rechecked.
  Evidence: `Builds/Checks/A3-20261102`; manual UI checks pending.
- **Open limit:** the original hardware-watchdog trigger is unconfirmed. TCG
  removes WHPX from maintenance; no general stability guarantee follows.
  Other hardware and physical power-loss tests remain open. Issue #3's
  `chr_dev_init`/THP slowdown needs raw logs and target-hardware confirmation.
  Personal install and published release unchanged.
- **Next:** receive fullscreen/input/rotation acceptance, verify saved settings
  after restart, then prepare the explicitly requested GitHub prerelease with
  corresponding-source and dependency provenance. No new release is published.
