# Development status

- **Commit:** A5 candidate built from `eeb372c` plus the packaging change that enables the A5 development label. A4 launcher correction `705ed76` is included. Local only.
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
- **A5 packaging and tests (23 November):** `Packages/Habumi-A5-20261123/Habumi-development-A5.zip` contains 356 verified entries and the launcher visibility correction. SHA-256 `B7F09D8083B488EAD99C3C1C1E939906A53F4F0E219FD97BF9E8441EDD345E60`. From a fresh extraction, launcher `--validate` passed, the Updates visibility/profile path/no-console regression passed, and no deployed guest disk was present. The launcher reports `NotSigned`.
- **A5 fresh boot:** the isolated `fresh-extraction` profile downloaded and verified vendor and GApps, created a new 4 GiB data disk, and reached Android readiness in 163.531 s including downloads. ADB confirmed `sys.boot_completed=1` and Launcher3 in the foreground. The clipboard helper connected on port 15556.
- **A5 variant regression:** the original candidate verified the Vanilla download and shut down GApps normally, but the guest init stopped on a SystemUI hash mismatch after switching. The probe was built for GApps. Guest init now retains stock SystemUI for the exact observed Vanilla APK hash, applies the verified probe to the matching GApps APK, and rejects unrecognized hashes. GApps recovery on the original profile reached readiness in 36.016 s. The local initramfs repack and hash receipt are under `Builds/A5-SystemUI-Fix`; the earlier A5 ZIP still contains the original initramfs.
- **Open limit:** the launcher warned that vendor build `1790493416` differs from the audio HAL reference `1775198504`. Audio playback and capture remain unverified. Variant switching and clipboard transfer remain open, along with signing, dependency/license coverage, physical power-loss and broader hardware.
- **Distribution:** public rc.2 and the personal installation are unchanged.
  A4 is accepted locally, not distributed.
- **A5 corrected Vanilla boot:** `Builds/A5-SystemUI-Fix/profile` used fresh Vanilla data. Guest log confirmed stock SystemUI and readiness at 38.016 s. ADB confirmed Launcher3 in the foreground.
- **A5 corrected package:** `Packages/Habumi-A5-Variant-Fix/Habumi-development-A5.zip`, 356 verified entries, SHA-256 `801268744461F7083E443444C36CFC53D8F4A84A33D77134345801CADAEE36D0`. Initramfs provenance receipt is beside the archive. From this ZIP, GApps booted with a fresh data disk and previously verified system/vendor images. The probe remained active, Android reached readiness at 48.516 s, and ADB confirmed Launcher3 in the foreground. The user reported both directions reaching Home through the variant menu. The inspected profile log did not capture those transitions, so the independently observed results cover startup in both variants separately.
- **A5 live tests:** GApps to Vanilla from the menu passed on retry. The first attempt hit Windows error 5 while replacing a seeded Vanilla system image. The file remained locked until normal shutdown. After retaining that copy under another name, the verified ZIP was reused; GApps shut down in 5.265 s and Vanilla reached readiness 36.015 s after the restart, with Launcher3 in the foreground. Earlier user confirmation covers both directions; this run captured only GApps to Vanilla. Play Store reached its signed-out screen in 565 ms. Sign-in, downloads and updates require a test account.
- **A5 multimedia:** an independently decodable 12-second H.264/AAC MP4 was tested in Android Gallery on both variants. Both launches timed out at about 10 s after creating NuPlayer, with GApps visibly stuck on the Gallery splash. This reproduction uses a file URI. Actual frame decode, audible output and microphone capture remain unverified. Clipboard helper connected, but the end-to-end test was skipped to preserve existing non-text Windows clipboard data.
- **A5 distribution audit:** 108 packaged DLLs match the inventory by SHA-256. All 102 distinct referenced license files exist. Copyright and source coverage remain incomplete per the inventory. Launcher and emulator are unsigned; no local personal certificate with Code Signing usage was found.
- **A5 playback correction:** Gallery timed out with a MediaStore URI as well. The LineageOS 20 media player also waits for `IOmx::getService()` during source setup. The experimental probe now has an opt-in playback mode that verifies the pinned ARM64 library and makes that lookup nonblocking alongside the existing IOmxStore lookup. The default probe was reproduced byte-for-byte. In the patched candidate, Gallery completed the same 12-second MP4 on Vanilla (361 video frames reported, zero drops) and GApps (357 frames reported, seven drops). Both used the Codec2 AVC/AAC decoders and wrote PCM to Android's speaker output. Host audibility and microphone capture remain separate checks.
- **A5 image replacement correction:** the asynchronous SHA-256 reader now uses a native non-inheritable handle. This prevents QEMU from retaining an image opened during verification. In the patched candidate, an inactive system image could be renamed and restored with QEMU running in either variant. Full menu replacement against a pre-existing image has yet to be rerun on this build.
- **A5 corrected ZIP:** `Packages/Habumi-A5-Media-Fix/Habumi-development-A5.zip`, 356 verified entries, SHA-256 `A823F4DA98860705CB72FD5D724DA4C04A2A2385F2E6FF3AD2093E7A64C4C16A`. Playback, audio and binary provenance receipts are beside the archive. The test profile is at the GApps Home.
- **A5 image replacement retest:** the corrected candidate replaced a pre-existing inactive Vanilla system image from the variant menu while GApps was running. Download verification and extraction completed without a sharing error. GApps shut down normally, and Vanilla reached readiness 26.000 s after restart.
- **A5 Windows audio:** a process-filtered Core Audio meter on the default render endpoint measured idle peak 0, peak 0.000397 with the test video, and peak 0.001740 with a 4x tone input. This confirms signal delivery to the Windows QEMU audio session. Physical speaker audibility and microphone capture remain open. Evidence and updated S0 baseline are beside the corrected archive in `windows-audio-signal.txt` and `standby-baseline.json`. Vanilla is open at Home.
- **Next:** verify bidirectional clipboard with a user-supplied test string and perform real standby/resume using the captured Windows event and QEMU identity baseline. Check physical speaker audibility and microphone capture. Use a test account for Play Store installs and updates, and additional hardware for compatibility checks.

Details and evidence: [A4 desktop integration](A4-DESKTOP-INTEGRATION.md).
