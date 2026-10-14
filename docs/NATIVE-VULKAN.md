# Native Vulkan prerelease

Habumi [0.3.0-rc.1](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.1)
targets Windows 11 ARM64 machines with a compatible native Qualcomm Vulkan
driver. The packaged candidate passed a fresh-data boot, final reboot and
normal close on the maintainer's PC. The development profile has booted
Android 13, enumerated an Adreno-backed Venus device and completed Wild Life
with visible scenes. Coverage on other devices and applications remains open.

## Starting the package

Extract into a new directory and run `Start.cmd`. It calls `Start-Habumi.ps1`,
verifies critical component hashes, locates the Qualcomm ICD registered by the
installed graphics driver, and sets the environment for this process only.
Launching `runtime/bin/Habumi.exe` directly bypasses this graphics setup. A
missing native ICD produces a startup error; the launcher does not select a
software Vulkan driver as a fallback. The device graphics driver is not bundled.

Android Vulkan uses Venus forwarding to native Qualcomm Vulkan. GLES and window
composition use virgl and host Mesa Zink on the same native driver. This still
has virtualization, transport and composition costs; it is not GPU passthrough.
ARM64 CPU instructions execute through WHPX without cross-architecture
instruction translation.

The initial profile uses GApps, six vCPUs, 6 GiB of guest memory, an 800x1280
logical display with guest scaling, and density 426. At 200% Windows scale the
guest framebuffer is 1600x2560. Refresh is advertised as 120 Hz on AC and 60 Hz
on battery at VM startup; changing power source during a run does not change
the advertised refresh. A fresh data image is 4 GiB. The larger 16 GiB image
used during development is not redistributed. System/vendor images are fetched
from the image sources selected by the product and checked against their hashes;
no installed applications, Google accounts or developer data are packaged.

## Reliability and measurements

Boot readiness, shutdown and the declared Android controls use owned
asynchronous ADB clients. Navigation, density, a rotation pair, normal shutdown
and reopen passed in the development profile. Rotation does not yet make the
host window follow every framebuffer shape. File/APK transfer still has its
separate existing behavior.

The prerelease enables experimental host-power handling. Actual Windows
callbacks, an owned VM pause/resume and a responsive Android boot-property query
were observed after an overnight transition. Repeated hibernation, unchanged
guest identity across that interval and broad hardware coverage remain unproven.

The archived optimization prototype estimated 9.2% median additional frame
time for one offscreen Vulkan workload, with an interval of 8.58%-10.52%. It was
not adopted as a product optimization. There is no general maximum-10% overhead
guarantee and no measured host-battery benefit. Queue batching, direct transport,
GPU probes and trace recording are disabled in the release launcher.

## Component provenance

`release-profile.json` records hashes of the packaged shell, QEMU, renderer,
Zink DLL, kernel, initramfs and empty data template. Host Mesa is the ARM64 Zink
asset from [mmozeiko/build-mesa 26.2.4](https://github.com/mmozeiko/build-mesa/releases/tag/26.2.4),
with archive SHA-256 `aad83c138849348a6f70d8c50fd6a51b27af84ebbdf3fc93a204fde2c6c71b85`.
The bridge is rebuilt from pinned fork revision
`e80354b8f5a0dedc62374b905a181874682659cc`, the linear base patch, complete native
WSI patch and warning fixes. Generated Venus protocol headers are vendored at
their existing protocol revision; the warning patch must be reapplied if those
headers are regenerated. The builder treats compiler warnings as errors.

See [release preparation](RELEASE-READINESS-0.3.0-rc.1.md),
[known issues](../KNOWN-ISSUES.md), and the packaged `LICENSES` directory.
