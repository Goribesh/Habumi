# Habumi

**Run Android apps natively on Windows on ARM.**

Habumi runs Android 13 on Windows 11 ARM64 machines — Snapdragon X Plus,
X Elite and similar — with **no instruction translation at all**. Your CPU
executes the same ARM64 instructions a phone would. That is the whole point: an emulator that
translates x86 Android to ARM, or ARM to x86, pays for it in every frame. This
one does not.

Under the hood it is QEMU with a display backend written for this project, a
Linux guest kernel built here, and an initramfs that assembles Android without
modifying the vendor images.

---

## Status — read this before you install

The full feature and performance measurements come from a Surface Pro 11.
External reports from Surface Pro 12" machines helped fix boot failures in
0.2.3, but multi-vCPU boot on that hardware is still intermittent. Hardware
coverage remains limited; see [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for the measured
fallback and the remaining limitations.

**[KNOWN-ISSUES.md](KNOWN-ISSUES.md) is the full list** of what does not work
yet, ordered by what it costs you. The short version: no code signing, so
SmartScreen can warn; there are no automatic updates; graphics support initially
targets the native Qualcomm Vulkan driver; and repeated overnight resume still
needs validation. What would make
the project better, and where help is wanted, is in [ROADMAP.md](ROADMAP.md).

Two things that are choices rather than defects: the guest images are **pinned**
to Waydroid OTA commit `026205c747d314865fa18398375ff3b0e7bd7f70` (build `20260927`), because an image that changed by itself would
change the bugs by itself; and **comments and internal documentation are in
Italian**, see [Language](#language) below.

**[0.3.0-rc.1](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.1)**
is the native Vulkan prerelease: Venus forwards Android Vulkan to the Qualcomm
driver, while Zink handles GLES composition. It also includes asynchronous
Android controls, variant-restart fixes and English product text. The latest
stable release remains [0.2.3](https://github.com/Goribesh/Habumi/releases/tag/0.2.3).
The prerelease passed a clean-data boot and a final packaged reboot on the
maintainer's PC; a first-initialization SystemUI exception and repeated
overnight resume remain open. See [RELEASE-NOTES.md](RELEASE-NOTES.md),
[the native Vulkan guide](docs/NATIVE-VULKAN.md), and
[the validation record](docs/RELEASE-READINESS-0.3.0-rc.1.md).

If it breaks, send `guest\logs\registro-guscio.log`. Previous sessions are
preserved in `guest\logs\archivio\`.

## Requirements

| | |
|---|---|
| OS | Windows 11 **ARM64**. Not x86-64: there is nothing here to run on it |
| Disk | allow **12 GB free** for the default GApps images, downloads and initial 4 GiB data image |
| Network | about **1.3 GB** on the default GApps first run — the Android images download themselves |
| Virtualization | the **Windows Hypervisor Platform** feature must be enabled |
| Graphics | a compatible native **Qualcomm Vulkan ICD** installed by the device graphics driver |

`tar.exe`, shipped with Windows 11, is used to extract the downloaded images. It
is a real dependency, not an implementation detail.

## Install

1. Download [Habumi-0.3.0-rc.1.zip](https://github.com/Goribesh/Habumi/releases/download/0.3.0-rc.1/Habumi-0.3.0-rc.1.zip), verify it against the release's `SHA256SUMS.txt`, and unpack it into a new directory.
2. Run **`Start.cmd`**, which selects the native graphics driver and launches Habumi.

For this prerelease, use a new directory. Keep your existing installation and
Android data separately. Launching `runtime\bin\Habumi.exe` directly bypasses
the Vulkan setup.

There is nothing to install: no registry entries, no administrator rights, no
services. To uninstall, delete the folder.

On first run Habumi notices the Android images are missing and downloads them
from the [Waydroid](https://waydro.id/) project's official images, verifying the
sha256 each project publishes before using them. It then creates the Android
`/data` filesystem from an empty image included in the package. Later runs skip
all of this.

## What works

Measured on a Surface Pro 11 — Snapdragon X Plus X1P64100, 10 cores, Adreno
X1-85. These feature measurements do not establish compatibility on every
Windows ARM machine:

- **Touch, multi-touch and guest rotation**. The host window still needs to
  follow the rotated framebuffer's shape; see the known issues
- **Audio playback and input**, through an audio HAL with a 4096-frame ring.
  The measured starvation gaps were fixed; audio during physical interaction
  still needs a controlled check
- **Shared clipboard**, both directions, between Windows and Android
- **Drag and drop** of files, folders and APKs onto the window — they land in
  `/sdcard/Download`; an APK is installed
- **Keyboard mapping** for games: keys become touches, `WASD` becomes a virtual
  stick, profiles live in `runtime\keymaps\` as plain text
- **Two Android variants** — with and without Google apps — stored separately,
  each keeping its own apps and data. One VM runs at a time; switching does
  not erase anything
- **Native Vulkan in the development profile**, through Venus on Qualcomm;
  Wild Life completed with visible scenes. The release package requires its
  own fresh-install validation; this is not general app or device coverage
- **Asynchronous boot checks, shutdown and controls**, including density,
  navigation and guest rotation. A pending ADB client no longer holds the UI
  timer for the duration of the request

## Build from source

Requires MSYS2 with the CLANGARM64 toolchain, a WSL distribution (the initramfs
build needs a real aarch64 Linux), and — for the guest clipboard app — the
Android SDK build-tools and a JDK.

```bash
bash app/scripts/build-guscio.sh    # the shell application
bash app/scripts/test-guscio.sh     # 13 test suites, no VM needed
```

QEMU is built separately; the patches that add the `winq` display backend are in
`qemu/patches/`, applied on top of QEMU 11.0.3.

The native renderer uses the pinned upstream revision and linear patch set in
`app/scripts/build-native-renderer.ps1`. The corresponding-source bundle and
component manifest must accompany the prerelease. See
[release preparation](docs/RELEASE-READINESS-0.3.0-rc.1.md) for package-specific
validation and remaining gates.

For repeatable host GPU/CPU and battery measurements, see
[the measurement protocol](qemu/HOST-OVERHEAD.md). The sampler reads counters
without changing the running VM or its settings.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the rules this project holds itself
to — some of them are not obvious and exist because breaking them cost time.

## Language

The user interface, runtime diagnostics, this README, and the contributor
documents are in English. Key-map profiles use English key names; legacy
Italian key-name aliases remain readable.
**Everything else — the source comments, and the documentation the scripts
carry inside themselves — is in Italian.**

This is worth knowing before you clone. The comments in this codebase are not
decoration: they carry why a measurement disproved a hypothesis, why a line
exists, what was tried and failed. Translating them properly is planned work,
not a `sed` command.

This repository starts from a single commit, on purpose: it is a starting point,
not an archive. The reasoning that matters is written where it belongs — next to
the code it explains, and in [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for what is
still wrong.

## License

GPLv2. See [LICENSE](LICENSE).

QEMU is GPL and **our copy is modified**, so the corresponding source offer is
specific rather than vague: QEMU 11.0.3 from the official site, plus the patches
in `qemu/patches/`. Mesa and virglrenderer are modified too, each with its own
patch set under `research/`. [NOTICE.md](NOTICE.md) names every third-party
component, the exact upstream version it starts from, and its license.

The **name** is not covered by the license — see [TRADEMARK.md](TRADEMARK.md).

## Supporting the project

Habumi is free software and will stay that way. If it is useful to you, there is
a sponsor button; there is nothing behind it that you don't already have.
