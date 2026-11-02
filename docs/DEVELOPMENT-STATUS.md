# Development status

- **Commit:** issue #3 recovery commit containing this checkpoint; parent
  `6c7cd44`. Recorded 2 November 2026; local, not pushed.
- **Completed:** A2 accepted in the isolated Surface profile: TCG-only growth
  4-to-16 GiB with retained original, real backup/restore, proof-file preservation
  and user-confirmed runtime/data-panel/restart handoff. A3 work remains in
  progress. Issue #3 triaged; retry recovery now compares normalized terminal
  serial content instead of counts, only after silence timeouts.
- **Tests:** 13 shell suites / 702 checks, including 28 new retry/serial checks;
  complete development shell build passes. A2 evidence remains in
  `Builds/Checks/a2-final-20261022`; no new VM or host-power test this session.
- **Open limit:** the original hardware-watchdog trigger is unconfirmed. TCG
  removes WHPX from maintenance; no general stability guarantee follows.
  Other hardware and physical power-loss tests remain open. Issue #3's
  `chr_dev_init`/THP slowdown needs raw logs and target-hardware confirmation.
  Personal install and published release unchanged.
- **Next:** complete A3 atomic settings, reversible fullscreen, on-request PNG
  and orientation checks, then prepare the explicitly requested GitHub prerelease.
