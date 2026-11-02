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
not a performance benchmark. Manual acceptance and distribution are pending.

## Acceptance still needed

- On a real display, enter fullscreen and exit through the always-visible More
  menu; verify that the original window state returns.
- Save settings, close and reopen Habumi, and confirm the requested values at
  startup.
- Capture both portrait and landscape Android screens and inspect the PNGs.
- Check physical keyboard and touch input in both orientations.
- Multi-monitor behavior has not been tested on physical displays.
