# Known issues

This page describes known limits of the
[0.3.0-rc.2 prerelease](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.2).
Historical observations are identified as such. Package checks are recorded in
[the RC2 validation record](docs/RELEASE-READINESS-0.3.0-rc.2.md).

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

**Issue #3 retry handling is corrected, but the reported CPU slowdown remains
unresolved.** The Surface Pro 12" report describes a slowdown around
`chr_dev_init` and THP with multiple vCPUs. Retry logic now compares normalized
serial progress to avoid an incorrect retry decision, but that does not fix or
explain the underlying slowdown. Raw logs and target-hardware confirmation are
still needed. See [the issue 3 triage record](docs/ISSUE-3-BOOT-TRIAGE.md).

**Older boot-watchdog descriptions are obsolete.** 0.2.1 and earlier could
kill a healthy but slow boot. The current kernel-progress watchdog uses a
60-second grace period, 45 seconds of serial silence and a five-minute ceiling.
Android readiness has a separate 120-second threshold; the shell can continue
waiting while serial output still advances. Messages distinguish a live but
unfinished boot from missing ADB or stopped progress. These thresholds do not
guarantee boot on every machine. See [RELEASE-NOTES.md](RELEASE-NOTES.md) for
the historical 0.2.4 cleanup fixes.

## Android stability and audio

**A historical fresh startup recorded a SystemUI exception.** The earlier clean
20260927 GApps initialization captured an AppOps-related null-pointer exception
in SystemUI; Android subsequently reached a readable Home and accepted
navigation. The RC2 clean boot reached ready without a FATAL, RCU stall or
kernel panic, but one boot does not establish that the historical exception or
other Android faults cannot recur. See the
[RC2 validation record](docs/RELEASE-READINESS-0.3.0-rc.2.md).

Development after 0.3.0-rc.1 reproduced the same exception on another fresh
data image. A new shell check waits for stable SystemUI/compositor PIDs before
marking startup ready. A proposed upstream source null-list guard is recorded;
an isolated Dex prototype subsequently passed one fresh-data boot and a short
codec/storage workload. RC2 includes that compatibility probe in its initramfs;
it is not a verified general fix or an upstream vendor-image change. See
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

**Manual rotation and physical input passed RC2 acceptance on one PC.** The
candidate passed portrait and landscape checks with touch and keyboard. This
does not validate app-requested orientation changes, other GPU paths, DPI
transitions or multiple physical monitors. See
[the RC2 validation record](docs/RELEASE-READINESS-0.3.0-rc.2.md).

**Vulkan was checked on one development PC.** The clean RC2 candidate enumerated
one Vulkan device and showed a valid launcher image. Its `gpuvkjson` output was
`[{}]`, so no physical-device identity or version is claimed. Earlier Wild Life
and synthetic-overhead measurements are profile-specific and do not establish
general performance. Other Qualcomm drivers, GPUs and apps remain unverified.

**App-requested orientation changes remain unverified.** The manual rotate
control passed on the RC2 candidate, but automatic changes requested by Android
apps have not been accepted across apps and graphics paths.

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

**Only one Android variant runs at a time.** Vanilla and GApps profiles have
separate data images and are selected at startup. Variant restart handling is
included in RC2; both switch directions and all first-run recovery cases still
need broader acceptance. See [RELEASE-NOTES.md](RELEASE-NOTES.md).

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
block execution under some policies. Updates require a manual download; the A4
updater is not implemented.

**The complete dependency and license inventory is unfinished.** See
`app/release/LICENSES/DLL-MSYS2-NOTA.txt` and [NOTICE.md](NOTICE.md); do not
interpret available notices as a completed per-component audit.

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
