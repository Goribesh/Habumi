# Known issues

This page describes the remaining limits of the product and the prepared 0.2.4
candidate. Historical observations are identified as such; a documentation
update is not a new runtime test.

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
the 0.2.4 cleanup fixes.

## Android stability and audio

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

**Applications requiring Vulkan are unsupported.** The shipped Android graphics
path is OpenGL ES 3.1 through virgl, host Mesa and D3D12. Experimental Venus
probes have enumerated devices, but that is not usable Vulkan in Android apps.
Recorded obstacles include external-memory and Android native-buffer support
and a crash in the Qualcomm D3D12 driver on the Dozen path. Venus remains off
in the product; updating the guest GL driver alone has not shown a benefit.

**Guest rotation does not resize the host window.** Android and its framebuffer
alternate between portrait and landscape, but the QEMU host client keeps its
fixed shape. A portrait framebuffer can consequently be squeezed into a
landscape window. The rotation command itself was corrected; the window-shape
work remains outstanding.

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

**Variant selection has a persistence mismatch in the sources.** The config
reader accepts `variant`, but the shell's variant writer searches and writes
`variante`. A UI selection can therefore leave the existing `variant=vanilla`
entry controlling the next launch while adding an ignored `variante=gapps`
entry. This was identified by source inspection; a runtime reproduction and
fix remain pending. For a manual selection, the accepted key is
`variant=vanilla` or `variant=gapps`.

**Windows sleep suspends the VM.** The guest clock and virtual CPUs stop while
wall-clock time continues. On resume Android can enter watchdog recovery;
`adb shell` may answer while framework commands such as `pm` and `dumpsys` do
not. A VM suspended during startup may simply not have completed boot.

**Nothing keeps the host awake for the VM.** Sleep handling remains a product
decision because preventing sleep changes battery use.

**One VM runs at a time.** Vanilla and Google-app variants have separate data
images, but they are alternatives selected at startup. They are not simultaneous
instances. Multi-instance support needs separate mutexes, command pipes, ports
and writable data images; see [ROADMAP.md](ROADMAP.md).

## Packaging and licensing

**The binaries are unsigned and there is no updater.** SmartScreen can warn or
block execution under some policies. New versions require a manual download;
there is no automatic notification or data migration.

**The runtime DLL license inventory is incomplete.** The release ships 107
MSYS2 DLLs; a per-library inventory remains outstanding. See
`app/release/LICENSES/DLL-MSYS2-NOTA.txt` and [NOTICE.md](NOTICE.md).

**The first run depends on Waydroid's image servers.** Android system and vendor
images are downloaded rather than redistributed, with their published hashes
checked before use. Network availability is required on first run.

**The audio HAL build is not reproducible from this repository alone.** The
4096-frame `audio.primary.waydroid.so` is supplied as an overlay binary tied to
the pinned vendor build. There is no complete public rebuild recipe for that
artifact. This is distinct from the documented kernel and QEMU builds.

**SELinux labelling remains unfinished.** The compositor and audio HAL overlays
do not yet carry proper SELinux labels.

## Reporting a problem

Include the version, expected and observed behaviour, SoC, Windows build, GPU
driver and `guest\logs\registro-guscio.log`. Earlier sessions are kept in
`guest\logs\archivio\`; `guest\logs\sessione-viva.log` contains guest serial
output. For boot failures, include the configured `vcpu`, the retry counts seen
in the log and whether `vcpu=1` changed the result. Full feature coverage and
successful boot on another machine are separate things worth reporting.
