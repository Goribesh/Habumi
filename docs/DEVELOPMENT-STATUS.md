# Development status

- **Commit:** A2 acceptance commit containing this checkpoint; parent `a9d8f33`.
  Recorded 22 October 2026; local, not pushed.
- **Completed:** A2 accepted in the isolated Surface profile: TCG-only growth
  4-to-16 GiB with retained original, real backup/restore, proof-file preservation
  and user-confirmed runtime/data-panel/restart handoff. A3 work is in progress.
- **Tests:** 40 maintenance contract checks; actual growth committed in 88.275 s;
  guest ready in 34.094 s, normal close 6.282 s, reopen ready 29.516 s. Proof
  hash and 16-GiB capacity rechecked. Evidence: `Builds/Checks/a2-final-20261022`.
- **Open limit:** the original hardware-watchdog trigger is unconfirmed. TCG
  removes WHPX from maintenance; no general stability guarantee follows.
  Other hardware and physical power-loss tests remain open. Personal install
  and published release unchanged; no new Kernel-Power 41 since 19 October.
- **Next:** complete A3 atomic settings, reversible fullscreen, on-request PNG
  and orientation checks, then prepare the explicitly requested GitHub prerelease.
