# A0: reproducible development baseline

This is an isolated development build after public commit `b64e11f`.
It does not replace 0.3.0-rc.1 or the personal installation.

## Inputs and reproducibility boundary

The checked-in [baseline lock](../app/release/development-baseline.json) pins
the published runtime archive, corresponding-source asset, QEMU source archive,
renderer/Mesa/kernel versions and the five reused critical binary inputs.
Obtain the two archives from the [immutable prerelease assets](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.1)
and extract `qemu-source-full.tar.gz` from the corresponding-source ZIP.
The hashes are checked before source preparation or package extraction.

The source-offer QEMU archive contains the host-power, resource-error and native
Venus integration used in the release. It is a patched source snapshot, not an
unmodified upstream tarball. The development recipe overlays all nine current
WinQ modules and their headers. Previously private host-power implementations
in the cycle/window/queue header and the idle-policy header are now consolidated
in the development sources. The clean build uses neither the old QEMU tree,
its compile database, cached objects nor its link response file.

This reproduces the **shell and full QEMU build with pinned binary dependencies**.
It does not promise bit-identical executable output or rebuild every third-party
DLL/kernel from scratch. Those components are verified inputs with version/hash
records; broader toolchain and source reproducibility remain A5 work.
The initramfs includes the explicitly marked SystemUI/OMX development probes;
source-built production replacements remain a later validation gate.

## Build and package

Run from the repository in PowerShell with MSYS2 CLANGARM64 installed. Use new
output directories. In these examples `C:/build` is a chosen writable workspace;
the commands do not infer a personal runtime or modified QEMU checkout.

```powershell
./qemu/scripts/build-development-baseline.ps1 `
  -QemuSourceArchive C:/inputs/qemu-source-full.tar.gz `
  -OutputRoot C:/build/habumi-a0-qemu -Jobs 4

./app/scripts/prepare-development-baseline.ps1 `
  -RuntimeArchive C:/inputs/Habumi-0.3.0-rc.1.zip `
  -QemuBuildRoot C:/build/habumi-a0-qemu `
  -InitramfsPath C:/inputs/development-initramfs.img `
  -OutputRoot C:/build/habumi-a0-package
```

The experimental initramfs must match the lock's SHA-256. Its reproduction inputs
and probe limits are described in [SystemUI](ANDROID-SYSTEMUI-NULL-PROBE-2026-10-15.md)
and [media startup](ANDROID-MEDIA-STABILITY-2026-10-15.md). A stock prerelease
initramfs is intentionally rejected for this particular development baseline.

The QEMU receipt records archive/overlay hashes, compiler and toolchain versions.
Overlay and shell snapshots identify working-tree inputs independently of HEAD;
development packaging explicitly records dirty source state. The shell is built
from its copied source snapshot. Normal release preparation still requires an
explicit renderer build. Its link command now includes the native UI's Advapi32
dependency.

Outputs include `build-inputs.json`, copied QEMU source/build/logs, shell-source
snapshot, `development-build.json`, per-file inventory, `SHA256SUMS.txt` and
`distribution/Habumi-development-A0.zip`. The package uses ADB port 17555 and
clipboard port 15556, matching the guest helper's compiled callback port.
No installed Android data, system/vendor images or session logs are copied.
The archive is reopened and every entry is hashed after packaging.

For testing, extract the ZIP into a new isolated folder and run
`Start-Habumi.ps1 -Validate`, then `Start.cmd`. Use fresh data. If verified
system/vendor images are supplied locally, QEMU must open them read-only;
they are test inputs and are not added to the distribution archive.

## Local evidence, 15 October 2026

- Full QEMU build: 2081 Ninja steps; no cached objects from the earlier tree.
- Full shell build: `-Wall -Werror -O2`, successful.
- Source hash mismatch rejected before creating output; an existing build
  directory was refused and its executable hash remained unchanged.
- Final archive: all 333 entries verified; validation passed after extraction.
- Fresh `/data`: 4 GiB, created from the empty template, with no copied apps/data.
- Android ready at 39.594 seconds; a live portrait/landscape rotation pair rendered
  upright and a landscape Calendar icon click opened the expected application.
- First boot ID `4fd7359c-29dd-4704-b9a4-9c62b767a355`; captured ANR/crash buffers
  were empty at uptime 224.31 seconds.
- Three simulated pause/resume cycles preserved boot/service identity. This
  does not establish real Windows standby/hibernate delivery.
- Normal close completed in 6.063 seconds without forced termination.
- Reopening the same profile reached readiness at 36.640 seconds and synchronized
  presentation to `user_rotation=0`; its normal close completed in 4.531 seconds.
  The preserved shell log confirms this lifecycle, but no second guest crash/ANR
  capture was collected. This evidence was reconciled on 19 October 2026.
- The old QEMU executable remained hash
  `3C3A5D86817EE737C1F4305036F66780F0E6368F1FE9202D188B4BD3C381A75A`.

| Artifact | SHA-256 |
| --- | --- |
| Clean QEMU | 1CFDAFAAF94C6AC49D6AB9D5FE96EED191C79C5C4321856EAA0AFF8233BAB381 |
| Final packaged shell | 9F85F6075CEC0E09BDD7F70443F2E314DF40F015685EC9C613958FDD5366FD85 |
| Final development ZIP | 03A38B5ED18DC7D60233A3474A0CD2AB9D53CDAAB9CD23F88C3E90322130FF54 |

Build artifacts: `Desktop/Habumi-A0-Qemu-20261015`; package/evidence:
`Desktop/Habumi-A0-Final-20261015`; extracted test profile:
`Desktop/Habumi-A0-Test`. Earlier trial packaging is separate from the final ZIP.

Known build diagnostics: the pinned upstream/toolchain combination emits one
Meson warning about an empty plugin target and 44 `#pragma pack` warnings from
installed SPICE headers. These were not suppressed or represented as clean
Habumi-source warnings. They remain visible in `build.log`; the shell build has
no compiler warnings. A0 establishes a working source build, not warning-free
coverage of every third-party header or broad hardware compatibility.
