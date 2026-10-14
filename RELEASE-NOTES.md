# Release notes

## 0.3.0-rc.1 — native Vulkan prerelease

This prerelease introduces native Qualcomm Vulkan through Venus;
GLES rendering and window composition use Zink. On the development machine,
Android booted, visible frames were presented and 3DMark Wild Life completed
with the user confirming that the 3D scenes were visible. The benchmark UI
reported “Maxed out”; its internal score is not an official comparative score.
This is a test-profile result, not broad hardware coverage or a guarantee for
other applications. The packaged candidate passed a clean-data installation
using verified official GApps/vendor images and a final reboot on the
maintainer's PC. The final reboot reached readiness in 27.656 seconds at
1600x2560; normal shutdown completed in 5.016 seconds. All 332 runtime ZIP
entries and final archive checksums were verified.

**Start Vulkan builds only through `Start.cmd`**, which invokes
`Start-Habumi.ps1` to select the native ICD and set the process environment.
Do not launch `runtime\\bin\\Habumi.exe` directly: it bypasses that setup.
The expected initial profile is GApps, an 800x1280 logical display at density 426
(1600x2560 guest framebuffer at 200% Windows scale),
120 Hz on AC and 60 Hz on battery, with a new 4 GiB data image. The 16 GiB data
image used in a local test profile is not part of this release target.

Install into a new, separate directory. The package does not include a user's
Android data; system/vendor images are downloaded and checked against their
published hashes. Do not overwrite an older installation or reuse its data
image for this candidate.

Initial graphics scope is Qualcomm Snapdragon Windows ARM64 with a compatible
native Vulkan ICD. If the required ICD is unavailable, startup should report a
clear error rather than silently selecting software rendering. The host Mesa
26.2.4 Zink build is based on the pinned `mmozeiko/build-mesa` asset; the
machine's stock driver is not redistributed. Other GPUs and hardware remain
unverified.

Async startup, shutdown, navigation, density and guest rotation passed live
test-profile checks. Thirteen public shell suites (648 checks) and 133 async
control checks passed. A normal test-profile shutdown completed in about five
seconds. A suspend/resume callback was observed, but repeated hibernation
reliability has not been established; callback handling is experimental and
opt-in. A clean-data packaged boot and normal shutdown passed locally.

An archived synthetic Vulkan prototype estimated 9.2% median frame-time
overhead, with an uncertainty interval crossing 10% and no confirmed FPS gain.
It was not adopted as a product optimization. No battery or energy benefit is
claimed. Signing, an updater, broader hardware coverage and a complete runtime
dependency and copyright audit remain outstanding. All runtime DLLs now have
license text references, and ADB 37.0.0-14910828 matches the installed SDK
byte-for-byte. The release includes the corresponding-source archive for the
project, patched QEMU, Linux kernel and native renderer.

A SystemUI AppOps null-pointer exception occurred during fresh initialization;
Home recovered and navigation passed. Repeated overnight standby/hibernation,
other hardware, Google sign-in, first-run download dialogs and a full variant
switch in both directions still need validation. These limits are recorded in
[the release validation record](docs/RELEASE-READINESS-0.3.0-rc.1.md).

## Historical — unreleased changes after the prepared 0.2.4 archive

The following entries record the state and tests from the 0.2.4 preparation
period. Statements about local installation, the running process or whether an
archive had been rebuilt apply only to that historical period; see the current
0.3.0-rc.1 section above for the present candidate status.

ARM WHPX exit statistics and periodic program-counter probes now require
`WINQ_WHPX_STAT=1`. Normal runs skip their counters/timings, statistic-thread
initialization and ten-second diagnostic vCPU cancels. Hyper-V synthetic
features and rendering settings are unchanged. The disabled path booted
Android on the development Surface Pro 11, and opt-in was verified in a
diskless WHPX VM. CPU/GPU energy savings and other-hardware behavior remain
unmeasured.

`WINQ_PRESENT_DIAG=1` enables presentation-only host timing without the broader
GPU probes. Reports include elapsed window duration and a new PowerShell
summarizer checks totals and computes frame-weighted stage means. Three fixed
scene samples at 120 Hz measured 0.164-0.169 ms per presentation callback;
about 95% of this host time was inside `SwapBuffers`. These are host call
times, not GPU time or the percentage of virtualization overhead.

The display safety timer now backs off from 5 ms to 30 ms after 250 ms without
input, commands or new frames. Event-driven input wakeups remain immediate;
new activity restores the fast timer. The first command on the shell's pipe
after idle can wait up to 30 ms. A paused synthetic frontend A/B confirms
fewer timer callbacks, about 181-182/s versus 32.7/s. Battery savings and
Android/game responsiveness remain to be measured in matched live runs.

The optional `battery_hz` configuration selects the advertised refresh when
starting Android on battery. The supplied configuration uses 60 Hz on battery
and keeps 120 Hz on AC, without changing resolution. Lower explicit `hz`
values are preserved; missing/zero `battery_hz` keeps the previous behavior.
Power-source changes during a running VM take effect at the next VM start.
These local binaries were installed without stopping the existing session;
the current running processes still use the preceding build. No release
archive was rebuilt or published.

User-facing English text is now consistent across the shell menus, download
and transfer dialogs, configuration diagnostics, key-map errors, QEMU/WHPX
diagnostics and the guest's custom audio messages. Sizes and elapsed times use
a decimal point. New key maps save English key names such as `SPACE` and
`ENTER`; existing Italian key-name aliases remain readable. `--arguments`
is available alongside the existing `--argomenti` diagnostic option.

QEMU's additional diagnostic translations are carried in
`qemu/patches/qemu-user-messages-english.patch`, applied after the functional
and diagnostic patch series. The kernel patch script also migrates message
text in already patched source trees. Source comments and internal identifiers
keep their established names.

Variant restarts could fail even after QEMU exited: the shell kept its own
serial reader open while trying to rotate the log. The reader is now closed
before rotation. The stale-log safety check remains in place for files held
by another process.

Saving a variant now uses the same `variant` key as the reader. Legacy
`variante` entries and duplicate variant entries are removed while preserving
the other settings and the separate Android data images.

Windows regression tests reproduce the file-sharing failure, verify log
rotation after closing the reader, and reload both variant selections from
the rewritten configuration. All 13 shell test suites passed. A rebuilt shell
booted the local GApps installation to Android readiness in about 49 seconds
and the Android home screen was observed. This is a boot check, not a Google
sign-in or application compatibility test. A full UI switch in both directions
remains pending: the local development images differ from the official hashes,
so the menu would first download replacement images.

The English shell, QEMU and kernel were rebuilt and installed locally.
All 13 shell suites passed (645 checks in development, 638 in the public
source tree), as did the four winq suites. The full QEMU patch series was
applied to pristine 11.0.3 sources and reproduced the four translated source
files. The rebuilt kernel's translated message strings and the kernel patch
script's idempotence were checked. With the updated binaries, GApps reached
Android readiness in 25.8 seconds and the home screen was observed; the
variant menu and `--config` output were verified in English. Runtime settings
were preserved. This does not establish new hardware or app coverage.

These corrections do not modify the existing `Habumi-0.2.4.zip` archive.

## 0.2.4 — prepared candidate

The archive has been prepared and locally exercised. As of October 5, 2026,
0.2.4 has not been published as a GitHub release; the latest downloadable
release is [0.2.3](https://github.com/Goribesh/Habumi/releases/tag/0.2.3).
These notes describe the prepared candidate and the corresponding source fixes.

Abruptly terminating the shell could leave QEMU running, holding the clipboard
port and session logs. A detached ADB server could also inherit log handles and
prevent the next startup even after both Habumi and QEMU had exited.

The candidate addresses that chain:

- QEMU belongs to the shell's job object and is killed when that job closes.
- Clipboard sockets and long-lived log handles are made non-inheritable.
- Startup refuses a failed serial-log rotation rather than reading an old
  session as evidence of a healthy new boot.
- Android-readiness timeout messages distinguish missing ADB, continuing
  serial progress and a boot that has stopped making progress.

Recorded local validation includes consecutive boots of 27 and 22 seconds
with the same ADB server alive between them, and a packaged-binary boot of
28 seconds with clean shutdown. This does not establish new hardware coverage
or resolve the multi-vCPU limitation on Surface Pro 12".

Candidate archive: `Habumi-0.2.4.zip`.

```text
sha256 30943f7ba3f3f1cc20d2b6d60ab78ea86ff623adbc2b28ab2f7d360d1fdf4ae6
```

Documentation and source changes after packaging do not update an existing
archive. A future package must be checked against the sources it actually ships.

## 0.2.3

Enabled the Hyper-V synthetic-feature grant by default and retuned the boot
watchdog using external Surface Pro 12" measurements: 60 seconds of grace,
45 seconds of serial silence and a five-minute kernel-progress ceiling.
Diagnostic logging and a new boot animation were also included. Boot improved
substantially on the reporting hardware; intermittent multi-vCPU hangs remain.
