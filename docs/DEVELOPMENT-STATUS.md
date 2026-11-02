# Development status

- **Commit:** local A4 implementation commit; parent `fa1c1e4`. Not pushed.
- **Completed:** local app library and launcher-based Windows shortcuts;
  verified shared-storage file export; manual staged software update, rollback
  and recovery. Candidate: `Packages/Habumi-A4-20261102-acceptance`, 356 entries,
  archive SHA-256 `F571F765D3C7D44EC20F669AA5680E704A3FAA2533033A7FBD767B1B080D75EF`.
- **Tests:** full shell build; 15 suites / 746 counted checks plus app IPC
  lifecycle checks; export 24 + worker 12; update 30 on PS7 and Windows PS5.1;
  update-worker/channel rejection fixtures; 43 existing data-worker checks.
  Real guest: 15 named apps and exact 65,539-byte export. Complete package
  apply/rollback/reapply passed; final candidate installed. Config and full
  16-GiB data SHA-256 stayed identical. All 66 source/build inputs match the
  candidate receipt. Cold app launch logged completion at 41.906 s; an
  independent foreground query confirmed Settings. Initial polling used the
  wrong field name (`mResumedActivity` instead of `ResumedActivity`).
  Real Windows shortcut opened Calculator in 2.47 s with the same QEMU PID
  (34452); receipt: `Builds/A4/warm-shortcut-launch.json`.
- **Open limit:** new-dialog manual acceptance remains unverified. The previous
  approval-review quota failure is resolved; the pending test passed through
  normal approval review. Physical power-loss, signing and broader hardware
  remain A5 gates. No graphics/backend changes in A4.
- **Distribution:** public rc.2 and the personal installation are unchanged.
  A4 is implemented locally, not accepted or distributed.
- **Next:** user acceptance of More > Apps, Export Android file, and Updates.
  Record results before marking A4 accepted or preparing A5. Do not repeat
  inventories/builds without a new change or an unresolved regression.

Details and evidence: [A4 desktop integration](A4-DESKTOP-INTEGRATION.md).
