# Roadmap

What would make this better, roughly in the order that the effort pays off.
Nothing here is promised and nothing has a date. Items marked **help wanted**
are the ones where someone else's machine or expertise matters more than more
hours on this one.

The 0.3.0-rc.1 prerelease introduces the native Vulkan path. Its clean-data
boot, final reboot and normal close passed on the maintainer's PC. The Vulkan
and shell-control results below remain evidence from that machine, not
release-wide guarantees.

Read [KNOWN-ISSUES.md](KNOWN-ISSUES.md) first: most of what follows is the other
half of something written there.

## Worth doing first

**Complete installation and upgrade coverage.**
The prerelease now has pinned Android metadata, verified runtime contents,
corresponding source snapshots and a fresh-data smoke test using official
images. First-run download dialogs, both variant-switch directions and an
upgrade path still need testing. Improve rebuild documentation and complete
the dependency and copyright audit.

**Run it on hardware that is not ours.** — *help wanted*
Full feature and performance measurements still come from the development
Surface Pro 11. Surface Pro 12" reports helped repair the boot path, but
multi-vCPU reliability remains unresolved there. Reports from more Snapdragon
generations, Windows builds and GPU drivers would extend that coverage.

**Finish the license inventory of the runtime DLLs.**
The candidate contains 108 DLLs, with 104 matched to MSYS2 packages. Mechanical work, no
special knowledge needed, and it is what stands between this and a distribution
someone can redistribute without reading tea leaves.

**Repeat host standby and hibernation checks.**
The test profile logged Windows suspend/resume callbacks, paused the VM, and
returned to a boot-ready Android query after one overnight transition. Repeat
standby and hibernation cycles, including interrupted startup and recovery,
before treating this as reliable. No keep-awake policy or power benefit has
been established.

## Graphics

**Validate the native Vulkan path beyond the development machine.**
The test-only candidate uses native Qualcomm Vulkan through Venus and Zink.
Android booted, visible frames were presented, and Wild Life completed with
the user confirming visible 3D scenes. The UI marked the result “Maxed out,”
so its internal score is not an official comparative score. The archived
synthetic workload's 9.2% median frame-time estimate crossed the 10% bound,
showed no confirmed FPS gain and was not adopted as an optimization. Validate
the graphics path on other hardware and applications, then package it cleanly
before considering release use. See the [WSI report](docs/NATIVE-VULKAN.md)
and [control confirmation](docs/NATIVE-VULKAN.md).

**Evaluate GLES forwarding separately.**
The native Gfxstream backend has passed host-side GLES 3.1 compute/SSBO and
RGBA8 readback probes, but it is not the Android rendering path described
above. QEMU integration and guest-side validation remain separate work. See
[GPU translation options](docs/NATIVE-VULKAN.md) for the prototype
scope and evidence.

**Review the guest GLES driver for the standard graphics path.**
The Venus/Zink result is from an isolated test profile. Any guest GLES driver
update must be evaluated independently on the standard virgl path and packaged
with matching, reproducible host and guest components.

**Reduce the drawing cost per frame.**
At 120 Hz the budget is 8.3 ms and drawing uses most of it. The delivery of input
to the application adds nothing measurable, so the gain has to come from the
frame itself, not from the plumbing around it. The latest recorded A/B reduced
the UI's 90th-percentile frame time from 16-18 ms to 7-8 ms by reducing pixels;
it did not establish a reduction in the reported jank rate. An extra frame
buffer did not reduce the measured dequeue wait. Resolution and refresh rate
remain product choices, rather than defaults to change from these numbers alone.

## Product

**Package and validate the variant-restart corrections.**
The sources now close the shell's serial reader before rotating the log and
save the canonical `variant` key. Windows regression tests and a local GApps
boot passed. Rebuild the candidate and exercise UI switches in both directions
with pinned, verified images in a clean install before publishing a release.

**Finish a clean release build and artifact inventory.**
The candidate still needs reproducible kernel and audio build instructions, a
complete per-DLL license inventory, and a review of every packaged file. The
test-profile binaries are not a substitute for a clean package build.

**Complete interactive UI validation.**
Async startup, shutdown and controls passed fake checks and live boot,
density, rotation, navigation and close/reopen checks on the development
machine. Actual button clicks and modal visuals were not tested. Repeat these
checks from a fresh install and on additional hardware.

**Sign the binaries.**
A certificate turns "Windows protected your PC" into a normal install. Costs
money, not time.

**An update mechanism.**
Today a new version is a new download and a manual copy. Anything better needs a
decision about where the data directory lives across versions.

**Finish rotation of the host window.**
APK drag and drop already exists, as do file and folder transfers. The remaining
window defect is that the guest and framebuffer rotate while the fixed host
client keeps its old shape. Guest rotation controls work; make the host window
follow the framebuffer and verify both orientations visually.

**Size the boot animation for the display.**
The recorded issue is an animation occupying only part of the screen. Verify
whether Android crops or scales it before changing the animation dimensions.

**More than one instance at a time, and something to manage them.**
The shape people expect is a launcher that lists instances and starts them, the
way other Android-on-desktop products do it: the shell stays one VM in one
window, and a separate process keeps the register, hands out the ports and
clones a data image from a template. That division matters, because everything
the shell does today keeps working unchanged.

Today a single instance is enforced on purpose, and by five separate fixed
names rather than one switch. All five have to move before two instances can
coexist:

| where | what is fixed |
|---|---|
| `app/guscio/main.c` | the single-instance mutex, `AndroidRuntimeGuscio.istanza` |
| `app/guscio/tubo.c` | the command pipe, `\\.\pipe\AndroidRuntimeGuscio.comandi`, created with `nMaxInstances = 1` |
| `runtime/bin/config.txt` | `adb_port`, chosen in a file rather than assigned |
| `runtime/bin/config.txt` | `clipboard_port`, same |
| `app/guscio/vm.c` | one writable `data.img`, opened `cache=writeback` |

Two things make this cheaper than it looks. `system.img` and `vendor.img` are
passed to QEMU `readonly=on`, so instances can share them: a new instance costs
one 4 GB data image, not the whole set. And QEMU already dies with its own shell
rather than with any shell, because it is attached to a job object owned by that
process — which is the first thing a second instance would otherwise break.

One trap to design around rather than discover. The `-drive` paths are
**relative**, and the shell expects the project root as its working directory.
With several trees on one disk the same relative path already denotes different
files, and that has already produced one wrong reading of a guest's identity.
Per-instance working directories, or absolute paths, are part of the feature and
not a detail of it.

Not on the list next to this one: per-instance device profiles, sensors or
telephony. Several instances of *this* Android is a different thing from a
general-purpose emulator, and only the first is in scope.

## Things deliberately not on this list

**Turning this into a general-purpose Android emulator.** The goal is running
Android applications on Windows on ARM with the GPU that machine actually has.
Device profiles, sensor simulation and telephony are not part of it.

**Supporting x86 hosts.** The whole point is native ARM64 execution with no
instruction translation. On an x86 host there would be nothing left of the idea.
