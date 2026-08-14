# Third-party components

Habumi itself is GPLv2 — see [LICENSE](LICENSE). It ships and depends on
software written by other people, listed here.

The full license texts live in `app/release/LICENSES/` and are copied into
every release archive. This file points at them rather than duplicating them,
so there is one copy to keep correct instead of two.

## QEMU — and ours is modified

The emulation core is [QEMU](https://www.qemu.org), GPLv2. **Our copy is not
the original**: it contains `winq`, a native Win32 display backend written for
this project, integrated into QEMU's source tree.

The GPL requires anyone redistributing a modified binary to offer the
corresponding source. This is that offer, stated precisely rather than
generically:

- **Starting point:** QEMU 11.0.3, from the official project site —
  <https://download.qemu.org/qemu-11.0.3.tar.xz>
- **Our changes:** the patches in `qemu/patches/`, tracked in this repository
  with their history, plus the sources in `qemu/ui-winq/`. Applied on top of
  exactly that version, they reproduce the binary shipped in the release.
- **License text:** `app/release/LICENSES/QEMU-COPYING.txt`, copied verbatim
  from our QEMU tree.

## Mesa — and ours is modified too

`runtime/bin/opengl32.dll` is [Mesa](https://www.mesa3d.org/), primarily MIT.
**It is neither the Windows `opengl32.dll` nor a stock Mesa build**: it is the
gallium `d3d12` driver, built for Windows ARM64 by this project, with five
patches on top. Without it the release would fall back on the system OpenGL and
lose the work those patches do — `dxil-hash` alone is the difference between
starting and exiting with code 87.

- **Starting point:** Mesa 26.2.0, git tag `mesa-26.2.0`, commit
  `9f0a761020bca92f2b07156a0621e5360cb8eca5`
- **Our changes:** the five patches in `research/mesa-patches/`, one file each,
  tracked in this repository
- **License text and full provenance:** `app/release/LICENSES/MESA-NOTA.txt`.
  Mesa is not uniformly MIT — individual files carry their own SPDX
  identifiers, and that note says where upstream keeps the full set.

## virglrenderer — and ours is modified too

`runtime/bin/libvirglrenderer-1.dll` is
[virglrenderer](https://gitlab.freedesktop.org/virgl/virglrenderer), MIT. It is
what turns the guest's GL commands into GL on the host. **Our copy is not the
original**: it carries a fork's worth of changes, from a Windows `eventfd`
substitute to a fix for a context that disappeared when a sub-context was
destroyed.

- **Our changes:** the numbered patches in `research/patches/`, tracked in this
  repository. They include instrumentation patches, and those are in the shipped
  binary too — leaving them out of the offer would make it incomplete, not
  cleaner.
- **License:** MIT. See the upstream project for the text and the copyright
  holders.

## Runtime libraries in `runtime/bin`

The 107 DLLs alongside `qemu-nostro.exe` — `opengl32.dll` is ours and counted
separately, above — come from the
[MSYS2](https://www.msys2.org/) project, built for ARM64 with clang. A census
taken against the running process confirmed nearly all of them are loaded
directly by QEMU; the remaining two are needed by `adb.exe`.

**Declared gap, not hidden:** the exact license text of each of those 107
libraries has not been collected individually. They are open-source libraries
under a mix of permissive and copyleft licenses, but "a mix" is not good enough
for a distribution aimed at a wider audience than one internal test. Completing
this is outstanding work. Details in
`app/release/LICENSES/DLL-MSYS2-NOTA.txt`.

## Android Platform Tools

`adb.exe`, `AdbWinApi.dll` and `AdbWinUsbApi.dll` are from Google's Android SDK
Platform-Tools, under the Apache License 2.0. The notice as Google distributes
it is in `app/release/LICENSES/ANDROID-PLATFORM-TOOLS-NOTICE.txt`.

## The audio HAL

A 19 KB file, `audio.primary.waydroid.so`, carried inside our initramfs, is
derived from the [Waydroid](https://waydro.id/) project's audio HAL, itself
based on the Android Open Source Project. Apache License 2.0, which permits
redistributing a modified binary with attribution.

License in `app/release/LICENSES/APACHE-2.0.txt`; what was changed and why in
`app/release/LICENSES/HAL-AUDIO-ATTRIBUZIONE.txt`.

## The Android images

`system.img` and `vendor.img` are **not redistributed**. They download
themselves from Waydroid's official sources on first run, unmodified, so their
licenses do not attach to anything in this repository or in the release archive.

This was a deliberate constraint on the release, not a convenience: every
customization this project makes lives in the initramfs, and the vendor image
was returned to its pristine state specifically so that this could stay true.
