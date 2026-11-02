# Development status

- **Commit:** A4 correction `705ed76`; Home confirmation `27183e9`. Not pushed.
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
  User acceptance exposed a hidden update panel: fixed only the launcher
  startup show state; visibility/no-console regression passes. A stale older
  report-generation test (PID 23488, 21 GiB private memory) was terminated;
  current text-report test now has owned-child cleanup, deadline/memory bounds
  and a stalled-child regression. Bounded PS5.1 rerun: 4.1 s / 85 MiB parent
  peak. Isolated Android returned to Home in 31 s (QEMU 31164), no PowerShell
  above 512 MiB. Local patch receipt: `Builds/A4/VisibilityFix/patch`;
  archive SHA-256 `0D035E022E873D564EDEF62017F6DA42FD4DCB4DFAE330724A8BB29887713F81`.
  The user confirmed the reopened Home is visible and usable on 2 November.
  The user also confirmed Updates is visible and Check completes successfully.
  The user confirmed Apps opens Calculator and the file-export dialog completes
  the shared-storage export. A4 is accepted on this isolated profile.
- **Open limit:** Original
  F571 candidate lacks the visibility correction; the isolated profile has the
  verified local software patch, with rollback originals retained. Physical
  power-loss, signing and broader hardware remain A5 gates.
- **Distribution:** public rc.2 and the personal installation are unchanged.
  A4 is accepted locally, not distributed.
- **Next:** A5: consolidate the visibility correction into a complete candidate,
  then validate installation, variants, media, clipboard and standby; record
  signing/provenance gates. Reuse A4 evidence instead of repeating its suites.

Details and evidence: [A4 desktop integration](A4-DESKTOP-INTEGRATION.md).
