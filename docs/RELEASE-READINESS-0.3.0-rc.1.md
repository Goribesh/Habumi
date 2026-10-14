# Habumi 0.3.0-rc.1 preparation

This is a local prerelease candidate, not a published GitHub release. Install in
a new directory and keep existing Android data separately. The release target
is Windows 11 ARM64 with the native Qualcomm Vulkan driver. Source comments
retain their established language; product text and release documents are English.

## Completed checks

- Public shell: 13 suites / 648 checks passed; asynchronous controls: 133 checks.
- Native renderer rebuilt from pinned upstream and three recorded patches with
  compiler warnings treated as errors. Native allocation/import, padded UI
  texture, 64-bit fence callback and sampled RGBA pixels passed.
- Initramfs rebuilt to a separate output; the installed profile's shared images
  were not modified. The kernel and QEMU retain their recorded tested builds.
- Package allowlist excludes installed Android data, system/vendor images,
  session logs, backups and the unused stock QEMU executable. ZIP entries are
  reopened and verified against per-file SHA-256 and length.
- Launcher tested with Windows PowerShell 5.1 and PowerShell 7. Hashing uses
  .NET directly; startup errors are written before showing their dialog.
- Package extracted into a directory with spaces. Official GApps/vendor ZIPs
  from build 20260927 were downloaded and verified against immutable Waydroid
  OTA revision `026205c747d314865fa18398375ff3b0e7bd7f70`, then extracted for
  the smoke test. No development system/vendor/data image was reused.
- A new 4 GiB data image was created by Habumi. Android logged readiness at
  81.875 seconds on this first initialization and applied density 426. ADB
  reported boot complete, the 1600x2560 framebuffer and an Adreno-backed Venus
  device. The Home screenshot shows icons, the status bar and navigation.
- The isolated control probe passed density, Home, Recents, Home and Back.
- Normal close logged guest Power down and QEMU exiting on its own in 4.484
  seconds, without the forced-shutdown fallback.

## Limits and publication conditions

A SystemUI AppOps-related null-pointer exception was captured during the fresh
initialization; Android subsequently reached a readable Home and accepted
navigation. Binder/compositor warnings and repeated overnight/hibernation
reliability remain open. Do not claim that all Android crashes are fixed.

The first-run image-download confirmation dialogs were not driven in this
smoke: verified official archives were prepared externally before launch.
Google sign-in, a full variant switch in both directions, other hardware and
general game compatibility are not established by these checks. The Wild Life
result belongs to the earlier development profile, not this fresh package.

The runtime inventory identifies 104 DLLs byte-for-byte with installed MSYS2
packages, alongside the ADB notices, pinned Mesa asset and rebuilt renderer.
Some package license texts and platform-tools version provenance remain
unverified; consult `app/release/LICENSES/RUNTIME-INVENTORY.json`. This is not
a claim that the entire dependency/licensing audit is complete.

Distribute the corresponding-source bundle with the runtime archive, including
the full patched QEMU and Linux source snapshots, native renderer source and
project source at the release commit. Preserve the `.config`, notices and build
instructions. SHA256SUMS must refer to the final archives, after any packaging
changes. Publishing a prerelease does not mark it stable or make it the latest
stable version. No maximum-10% overhead or battery-saving claim is included.

## Rebuilding

Clone `cmspam/winq-emu-virglrenderer` with the pinned revision available, then
run `app/scripts/build-native-renderer.ps1` into a new output directory. It
normalizes patch targets to LF, applies the linear base, complete native WSI
and warning-fix patches, and produces `build-inputs.json`. The generated Venus
headers are pinned source inputs, with explicit warning fixes.

`app/scripts/prepare-native-release.ps1` takes an assembled runtime, that
renderer build, a separately rebuilt initramfs and a new output directory.
It compiles the shell from this checkout, copies only `contenuto-native.txt`,
creates a portable profile and verifies the ZIP. It does not collect an
installation directory recursively. `WINQ_INITRAMFS_OUTPUT` directs the
initramfs builder to a new file instead of the active installation.

For QEMU, extract the corresponding patched source snapshot and configure with
the options in `qemu/scripts/build-qemu.sh`, then build the aarch64-softmmu
target. For the Linux snapshot, retain its supplied configuration and the
Android/virtio patches; `guest/scripts/build-guest-kernel.sh` documents the
toolchain and feature choices. Byte-identical output across different toolchain
versions or build timestamps is not promised.
