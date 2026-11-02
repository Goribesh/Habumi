# A3 settings and display controls

## Use

Open **More > Settings** to change the saved CPU count, RAM, guest width and
height, refresh rate, battery refresh rate, density, audio, and guest scaling.
Saving updates the configuration atomically and preserves comments and unknown
keys. Close and reopen Habumi to apply the saved values. The **Startup config**
column shows the requested configuration at launch; it does not query the GPU
for applied display values. This build uses the fixed native Vulkan backend.

Use **More > Full screen** to enter fullscreen. The More menu stays available;
choose **Exit full screen** to restore the window. No F11 shortcut is promised.

Choose **More > Save screenshot** to request an Android screenshot. Habumi saves
the validated PNG under `Pictures/Habumi`; **More > Open screenshots folder**
opens that folder. Capture runs in a hidden worker with a 20-second bound,
validates PNG data with Windows Imaging Component, and rejects files over
64 MiB. It uses ADB on demand and does not continuously copy display frames.

## Automated evidence

Run `app/scripts/test-a3.ps1` with the MSYS2 CLANGARM64 compiler. It builds and
runs the native config-edit test (19 checks), atomic config-save test (25
checks), and fullscreen test (13 checks), then runs the existing screenshot
recipe. Screenshot cases cover Unicode and spaced paths, an existing target,
invalid PNG data, nonzero ADB exit, cancellation, timeout, and oversized output.
Fixtures are retained under the user's Habumi Checks folder for inspection.

The native recipe passed all 57 config/fullscreen checks and the screenshot
cases. The full shell builds with warnings as errors; the 13 shell suites pass
702 checks. A separate launcher probe passed GUI subsystem, console suppression,
Unicode/space paths, working directory, child-window mode and exit propagation.

On 2 November 2026 the isolated package verified all 347 entries, SHA-256
`42A75E893EFFEDCA308BA894A226118696DFFF2C1EE2285C344A35FC42BD52D4`.
Four changed software files were installed in `Habumi-A2-Acceptance-20261022`,
with originals retained in `maintenance/software-A3-20261102`. The configuration
and Android disks were preserved. Android reached readiness in 35.515 seconds;
one on-request 1600x2560 PNG decoded correctly and visually showed Home. The A2
proof-file hash and 16-GiB data capacity were rechecked. Evidence lives under
`Habumi-Workspace/Builds/Checks/A3-20261102`. These are development observations,
not a performance benchmark. A2/A3 are now distributed in the
[0.3.0-rc.2 prerelease](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.2);
the development package hash above remains a separate historical artifact.

## Manual acceptance, 2 November 2026

The user confirmed fullscreen entry/exit, the screenshot action, rotation and
physical keyboard/touch in both orientations. They then saved the refresh limit
as 90 Hz in Settings and closed normally (5.547 s). The next launch requested
90 Hz; QEMU confirmed both its presentation interval and the rate announced to
Android. Readiness took 36.000 s. Normal close took 4.500 s; the native atomic
save routine restored 120 Hz while offline, preserving the A2 comment/unknown
key. The profile is left closed for release preparation.

A3's daily-use workflow is accepted on this Surface. Multi-monitor behavior is
not physically tested. The settings comparison deliberately identifies launch
requests rather than presenting them as current GPU/guest queries. Signing,
broader hardware and long standby validation belong to the remaining release
gates; no general stability or performance guarantee follows.
