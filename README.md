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

Everything listed under [What works](#what-works) has been measured, not
assumed. But this release has **only ever been installed on one machine**, the
one it was developed on. That is its single biggest limitation, and no amount of
testing here can substitute for someone else running it.

**[KNOWN-ISSUES.md](KNOWN-ISSUES.md) is the full list** of what does not work
yet, ordered by what it costs you. The short version: no code signing, so
SmartScreen will warn; no automatic updates; no Vulkan inside the guest; and the
virtual machine freezes if Windows goes to sleep underneath it. What would make
the project better, and where help is wanted, is in [ROADMAP.md](ROADMAP.md).

Two things that are choices rather than defects: the guest images are **pinned**
to Waydroid build `20260403`, because an image that changed by itself would
change the bugs by itself; and **comments and internal documentation are in
Italian**, see [Language](#language) below.

If it breaks, the log in `guest\logs\registro-guscio.log` is the thing to send.

## Requirements

| | |
|---|---|
| OS | Windows 11 **ARM64**. Not x86-64: there is nothing here to run on it |
| Disk | about 8 GB free |
| Network | about **805 MB** on first run — the Android images download themselves |
| Virtualization | the **Windows Hypervisor Platform** feature must be enabled |

`tar.exe`, shipped with Windows 11, is used to extract the downloaded images. It
is a real dependency, not an implementation detail.

## Install

1. Download the release archive and unpack it wherever you like — any path works.
2. Run `runtime\bin\Habumi.exe`.

There is nothing to install: no registry entries, no administrator rights, no
services. To uninstall, delete the folder.

On first run Habumi notices the Android images are missing and downloads them
from the [Waydroid](https://waydro.id/) project's official images, verifying the
sha256 each project publishes before using them. It then creates the Android
`/data` filesystem from an empty image included in the package. Later runs skip
all of this.

## What works

Measured on a Surface Pro 11 — Snapdragon X Plus X1P64100, 10 cores, Adreno
X1-85 — which is the only machine any of this has been measured on:

- **Touch, multi-touch and rotation**, with the guest resolution following the window
- **Audio**, host and guest, through a rebuilt audio HAL with a 4096-frame ring
- **Shared clipboard**, both directions, between Windows and Android
- **Drag and drop** of files, folders and APKs onto the window — they land in
  `/sdcard/Download`; an APK is installed
- **Keyboard mapping** for games: keys become touches, `WASD` becomes a virtual
  stick, profiles live in `runtime\keymaps\` as plain text
- **Two Android variants** — with and without Google apps — side by side, each
  keeping its own apps and data. Switching does not erase anything

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

See [CONTRIBUTING.md](CONTRIBUTING.md) for the rules this project holds itself
to — some of them are not obvious and exist because breaking them cost time.

## Language

The user interface, this README, and the contributor documents are in English.
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
