# Habumi 0.3.0-rc.3 release candidate

Habumi runs Android 13 on Windows 11 ARM64 with Windows Hypervisor Platform
and a compatible Qualcomm Vulkan driver. This candidate includes the A4 desktop
workflows and fixes for Android variant switching and video playback.

- **Apps and files.** Open installed Android apps from More > Apps, create
  Windows shortcuts to individual apps, and export a shared-storage file with
  size and hash verification. Shortcuts wait for Android to be ready.
- **Updates.** Check the selected release channel manually. The updater stages
  and verifies software before replacement, retains the previous software, and
  preserves Android data, settings and keymaps.
- **Variant switching.** Keep stock SystemUI on the verified Vanilla build and
  retain the matching overlay on GApps. The file verifier no longer lets QEMU
  inherit the handle used to hash an inactive image.
- **Video.** Both variants played the test H.264/AAC clip through Codec2 after
  the pinned OMX probe stopped waiting for an absent service.

The development Surface verified both variant boots, in-place GApps-to-Vanilla
replacement, Android-to-Windows file export, staged software update and
rollback, Unicode clipboard transfer in both directions, and video with audio
delivered to the Windows audio session. One 2.58-second Modern Standby cycle
resumed on the same QEMU process. Play Store reached the sign-in screen.

The unsigned launcher and other unsigned project binaries still require code
signing under the A5 plan. Repeated standby, longer suspension, hibernation,
microphone recording, signed-in Play Store use, and more hardware remain open.
The DLL inventory identifies the packaged hashes and license references. Its
copyright and dependency source coverage is still incomplete.

## Build and verification

Run `guest/scripts/rebuild-a5-initramfs.py --base <RC2 initramfs.img> --output
<new initramfs.img>`. The recipe checks the RC2 input hash, replaces the guest
init and OMX probe, and writes an output receipt. The rebuilt initramfs is
byte-identical to the live-tested A5 image with Python 3.14.2 and zlib-ng 1.3.1.

Supply the image and its receipt to `app/scripts/prepare-native-release.ps1`
using `-InitramfsPath` and `-InitramfsReceipt`. The recipe verifies the image
and source hashes and embeds the receipt in `software-inputs.json`. Reuse the
verified QEMU, renderer, Android helper and maintenance inputs recorded by the
package. The source archive records the project commit and QEMU overlay hashes.
Detailed device results and outstanding checks are tracked in
[the development checkpoint](DEVELOPMENT-STATUS.md).
