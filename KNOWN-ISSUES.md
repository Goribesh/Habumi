# Known issues

This page describes known limits of the
[0.3.0-rc.1 prerelease](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.1).
Historical observations are identified as such; a documentation update is not
a new runtime test. Package checks are recorded in
[the release validation record](docs/RELEASE-READINESS-0.3.0-rc.1.md).

## Hardware and boot

**Hardware coverage is limited.** Full feature and performance measurements
come from a Surface Pro 11, Snapdragon X Plus X1P64100, Adreno X1-85. External
Surface Pro 12" reports established additional boot behaviour, but do not
validate every feature on those machines. A first run on other hardware still
needs a report naming the SoC, Windows build and GPU driver.

**Surface Pro 12" (X1P-42) multi-vCPU boot remains intermittent.** Since 0.2.3,
the Hyper-V synthetic-feature grant is enabled by default. The recorded boot
time improved from 81-130 minutes to about two minutes, and six-vCPU boots have
succeeded, but repeated hangs have also been observed. `vcpu=1` in
`runtime\bin\config.txt` is the fallback measured as reliable on the reporting
machine. Retries halve the vCPU count after no serial output, or after repeated
hangs at the same serial position. The shipped default remains `vcpu=6`.

**Older boot-watchdog descriptions are obsolete.** 0.2.1 and earlier could
kill a healthy but slow boot. The current kernel-progress watchdog uses a
60-second grace period, 45 seconds of serial silence and a five-minute ceiling.
Android readiness has a separate 120-second threshold; the shell can continue
waiting while serial output still advances. Messages distinguish a live but
unfinished boot from missing ADB or stopped progress. These thresholds do not
guarantee boot on every machine. See [RELEASE-NOTES.md](RELEASE-NOTES.md) for
the historical 0.2.4 cleanup fixes.

## Android stability and audio

**A fresh prerelease startup recorded a SystemUI exception.** The clean
20260927 GApps initialization captured an AppOps-related null-pointer exception
in SystemUI; Android subsequently reached a readable Home and accepted
navigation. Its recurrence rate is not established. See the
[release preparation record](docs/RELEASE-READINESS-0.3.0-rc.1.md).

Development after 0.3.0-rc.1 reproduced the same exception on another fresh
data image. A new shell check waits for stable SystemUI/compositor PIDs before
marking startup ready. A proposed upstream source null-list guard is recorded;
an isolated Dex prototype subsequently passed one fresh-data boot and a short
codec/storage workload. This is not yet a distributed guest fix. See
[the startup investigation](docs/ANDROID-STARTUP-READINESS-2026-10-15.md).
The prototype's build gates and runtime limits are in
[the SystemUI probe record](docs/ANDROID-SYSTEMUI-NULL-PROBE-2026-10-15.md).

**Post-initialization GApps testing also recorded background-service ANRs.**
Google Play Services' intent-filter verification receiver and MediaProvider's
external-storage service exceeded Android response deadlines in a subsequent
test boot. The collected SystemUI crash buffer was empty on that boot. Their
cause and recurrence rate are not yet established; they are not repaired by
the new shell readiness check.

A subsequent isolated investigation traced MediaProvider's main-thread wait
to OMX codec enumeration with a declared but missing service. An opt-in
nonblocking lookup prototype restored the Codec2 list and emulated storage;
it remains an experiment, and the published runtime is unchanged. See
[the media startup investigation](docs/ANDROID-MEDIA-STABILITY-2026-10-15.md)
for its exact image constraints and limited test coverage.

**Historical `system_server` crashes remain unresolved.** Recorded crashes
during network activity and package installation restarted the Android framework
and closed running applications. Binder failures reached Java as an uncaught
`RuntimeException`. The exact triggering transaction was not established for
every occurrence, so one common cause has not been proven. This is not a claim
that every current session crashes. If it happens again, capture
`adb logcat -b crash -d` and `adb shell dmesg` before restarting; the active
serial log and its archived sessions are also useful.

**Audio during physical interaction needs a controlled check.** The virtio-snd
freeze and the measured starvation gaps were corrected, with a 4096-frame HAL
ring removing the recorded 12 gaps per 30 seconds. Clicks during physical input
were recorded, but Android touch sounds were enabled during those captures.
The follow-up with touch sounds disabled was left pending. The residual clicks
therefore are neither proven fixed nor conclusively attributed to an audio bug.
The ring size is not a measurement of end-to-end audio latency.

## Graphics and window

**Manual rotation is fixed in the development profile; published builds retain
the older presentation limitation.** The native WGL/Zink candidate passed
upright portrait/landscape rendering and matching mouse clicks. It also fits
the host window within the monitor work area. Automatic app-requested orientation
changes and the ANGLE path remain unvalidated. See
[the interface test record](docs/NATIVE-INTERFACE-2026-10-15.md).

**Vulkan is verified on the development machine, not across supported
hardware.** A test-only candidate using native Qualcomm Vulkan through Venus
and Zink booted Android, presented visible frames and completed 3DMark Wild
Life; the user confirmed the 3D scenes were visible. The result was marked
“Maxed out” because portions exceeded 60 FPS, so its internal score is not an
official comparative score. A separate archived prototype estimated 9.2%
median frame-time overhead for one synthetic workload, but its uncertainty
interval crossed 10%, no FPS improvement was confirmed, and it was not adopted
as a product optimization. Neither result guarantees performance or rendering
correctness on other devices or applications. The standard installation does
not use the test profile; there, the HAL override applied only to the benchmark
process, leaving other apps on the stock HAL. See the dated
[Venus WSI report](docs/NATIVE-VULKAN.md) and [control confirmation](docs/NATIVE-VULKAN.md).

**The published runtime does not include the new presentation/input rotation
transform.** Its earlier async-control checks covered Android settings and
requested dimensions; they did not establish upright host rendering. The
post-release development fix is recorded above and has not replaced release
assets. See [the interface test record](docs/NATIVE-INTERFACE-2026-10-15.md).

**The boot animation occupies only part of the screen in the recorded setup.**
Cropping versus scaling still needs verification before changing its dimensions.

**The high-resolution default has a measurable cost.** `guest_scale=yes` is
enabled in the shipped configuration. At 200% scale, 1280x800 logical pixels
become 2560x1600 guest pixels. The latest recorded UI A/B measured a
90th-percentile frame time of 16-18 ms at that resolution versus 7-8 ms at
1280x800, with density adjusted to preserve logical geometry. It did not
establish an improvement in the reported jank rate. The fixed host client also
means guest rendering can be supersampled rather than presented pixel for
pixel. Reducing resolution trades sharpness for drawing cost.

**The recorded roughly 16 ms touch-to-pixel figure includes an estimate.**
Input delivery and frame work were timed, but the final host presentation was
bounded rather than directly measured. It is not an optical end-to-end latency
measurement. The frame budget at the shipped 120 Hz is 8.3 ms.

## Host and lifecycle

**The prepared 0.2.4 archive still needs the variant-restart fixes.** During
a switch, the shell can keep its own serial reader open and then fail to
rotate that file, refusing the new boot. Its writer also uses `variante`
while the reader accepts `variant`, so the selected variant may not persist.
The current sources close the reader before rotation and save one canonical
`variant` entry, removing legacy and duplicate entries. These fixes were
tested on Windows and installed locally; they are not in the existing archive.
See [RELEASE-NOTES.md](RELEASE-NOTES.md).

**Suspend/resume callbacks are observed, but repeated reliability is not
established.** The test profile logged Windows Modern Standby and hibernation
callbacks, paused the VM and suspended WHPX partition time; a fresh ADB query
after resume reported boot readiness. Async startup, shutdown and controls also
passed live checks, including rotation, navigation and close/reopen. This
single overnight observation does not establish reliability across repeated
standby/hibernate cycles or all host hardware. It also does not promise that
the host is kept awake; no battery or power claim is made. See the [callback
experiment](docs/NATIVE-VULKAN.md) and [async controls
report](docs/NATIVE-VULKAN.md).

**One VM runs at a time.** Vanilla and Google-app variants have separate data
images, but they are alternatives selected at startup. They are not simultaneous
instances. Multi-instance support needs separate mutexes, command pipes, ports
and writable data images; see [ROADMAP.md](ROADMAP.md).

## Packaging and licensing

**Binaries are unsigned and there is no updater.** SmartScreen can warn or
block execution under some policies. New
versions require a manual download; there is no automatic notification or data
migration.

**The runtime DLL license inventory is incomplete.** The release ships 107
MSYS2 DLLs; a per-library inventory remains outstanding. See
`app/release/LICENSES/DLL-MSYS2-NOTA.txt` and [NOTICE.md](NOTICE.md).

**The first run depends on Waydroid's image servers.** Android system and vendor
images are downloaded rather than redistributed, with their published hashes
checked before use. Network availability is required on first run.

**Kernel and audio builds are not yet fully reproducible from this repository
alone.**
4096-frame `audio.primary.waydroid.so` is supplied as an overlay binary tied to
the pinned vendor build. There is no complete public rebuild recipe for that
artifact or the pinned kernel build. QEMU build instructions do not close this
gap.

**SELinux labelling remains unfinished.** The compositor and audio HAL overlays
do not yet carry proper SELinux labels.

## Reporting a problem

Include the version, expected and observed behaviour, SoC, Windows build, GPU
driver and `guest\logs\registro-guscio.log`. Earlier sessions are kept in
`guest\logs\archivio\`; `guest\logs\sessione-viva.log` contains guest serial
output. For boot failures, include the configured `vcpu`, the retry counts seen
in the log and whether `vcpu=1` changed the result. Full feature coverage and
successful boot on another machine are separate things worth reporting.
