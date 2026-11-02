# Habumi

**Run Android 13 apps on Windows 11 ARM64.**

Habumi is a portable Android environment for ARM64 Windows PCs, currently
targeting Snapdragon systems with a compatible Qualcomm Vulkan driver. It
uses QEMU, a Linux guest kernel and a native display backend. Hardware support
is limited and no general performance guarantee is made.

Under the hood it is QEMU with a display backend written for this project, a
Linux guest kernel built here, and an initramfs that assembles Android without
modifying the vendor images.

---

## Status — read this before you install

The current prerelease is **0.3.0-rc.2**. It has been packaged and
checked on one development PC; that does not establish compatibility on other
devices. Read [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for remaining limits.

**[KNOWN-ISSUES.md](KNOWN-ISSUES.md) is the full list** of what does not work
yet, ordered by what it costs you. The short version: no code signing, so
SmartScreen can warn; there are no automatic updates; graphics support initially
targets the native Qualcomm Vulkan driver; and repeated overnight resume still
needs validation. What would make
the project better, and where help is wanted, is in [ROADMAP.md](ROADMAP.md).

For a source-backed comparison of existing tools and missing product workflows,
see the [BlueStacks feature-gap catalog](docs/BLUESTACKS-FEATURE-GAPS-2026-10-15.md).
It distinguishes the published release from recent isolated-profile development.

Two things that are choices rather than defects: the guest images are **pinned**
to Waydroid OTA commit `026205c747d314865fa18398375ff3b0e7bd7f70` (build `20260927`), because an image that changed by itself would
change the bugs by itself; and **comments and internal documentation are in
Italian**, see [Language](#language) below.

**[0.3.0-rc.2 prerelease](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.2)**
adds offline data maintenance and a compact interface with settings, fullscreen,
screenshots and rotation. The latest stable release remains
[0.2.3](https://github.com/Goribesh/Habumi/releases/tag/0.2.3). See
[release notes](RELEASE-NOTES.md) and the
[RC2 validation record](docs/RELEASE-READINESS-0.3.0-rc.2.md).

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

1. Download [Habumi-0.3.0-rc.2.zip](https://github.com/Goribesh/Habumi/releases/download/0.3.0-rc.2/Habumi-0.3.0-rc.2.zip), verify it against `SHA256SUMS.txt`, and unpack it into a new directory.
2. Run **`Habumi.exe`** from the top-level folder. The launcher prepares the native graphics environment.

Use a new directory. Keep older installations and Android data separately.
Launching `runtime\bin\Habumi.exe` directly bypasses launcher setup.

There is nothing to install: no registry entries, no administrator rights, no
services. To uninstall, delete the folder.

On first run Habumi downloads the pinned, read-only GApps and vendor images from
the [Waydroid](https://waydro.id/) project and checks their published hashes.
It creates a new 4 GiB Android data image. The clean RC2 candidate booted to
ready in 51.578 seconds at density 426 and 1600x2560, then closed normally in
5.500 seconds. These measurements describe one development PC.

## What works

RC2 candidate features checked on one development PC:

- **Compact right toolbar** for Android navigation and More actions
- **Settings** for CPU, memory, display size, refresh, density, audio and guest
  scaling. Saves preserve comments and unknown keys; restart to apply settings.
- **Fullscreen** with entry and exit in the always-available More menu
- **On-request screenshots** saved as validated PNG files under Pictures\Habumi
- **Manual rotation and physical touch/keyboard** checked in portrait and
  landscape. App-requested orientation changes remain unverified.
- **Offline data tools** for backup, restore, recovery and diagnostic export;
  grow data from 4 to 16 GiB with software emulation while retaining the
  original until verification
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
- **Native Vulkan** through Venus on the Qualcomm driver. The clean package
  enumerated one Vulkan device and presented a valid launcher image. The
  `gpuvkjson` response did not identify a physical-device version, so no GPU
  version claim is made. App and device coverage remains limited.
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
component manifest accompany the prerelease. See
[RC2 validation](docs/RELEASE-READINESS-0.3.0-rc.2.md) for package checks and
remaining gates.

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
