# Known issues

What this does not do yet, what it does badly, and what it does on exactly one
machine. Nothing here is a surprise to the authors — it is written down so it is
not a surprise to you either.

Severity is about what it costs *you*, not how hard it is to fix.

## Blocking for most people

**It has only ever run on one machine.** A Surface Pro 11: Snapdragon X Plus
X1P64100, 10 cores, Adreno X1-85, Windows 11 Home on ARM. Every claim in this
repository about "it works" means "it works there".
A system DLL that happens to be present here and absent elsewhere would not have
been caught by any test that exists. Treat the first run on new hardware as an
experiment, and please report what happens.

**The second machine it was tried on did not boot.** A Surface Pro 12" with a
Snapdragon X Plus and an Adreno X1-45. The graphics half came up correctly —
WGL context, Mesa 26.2.0 on D3D12, a frame presented — but the guest kernel
produced no serial output at all on the first run, and on the second -- with
vcpu=1 -- it produced 97 and then 119 lines and was still going when the shell
killed it. That turned out to be our bug, not the machine's: see the next
entry. One success and one failure is the honest score.

**On Surface Pro 12" (X1P-42), multi-vCPU boots stay a lottery.** *Mostly
fixed in 0.2.3; the remainder is the hypervisor's.* Without the Hyper-V
synthetic-feature grant that 0.2.3 turns on by default, that machine lost
timer interrupts (boots of 81-130 minutes) and inter-processor interrupts
(any vcpu above 1 hung within seconds, deterministically). With the grant,
single-vCPU boots take about two minutes reliably, and six-vCPU boots have
been seen to work -- and also seen to hang three runs in a row at the same
kernel line, on the same machine, on a different day. The interrupt loss is
inside Hyper-V's virtualization of this SoC and no guest or VMM setting we
know of removes it entirely. If a multi-vCPU boot hangs repeatedly on this
hardware, set `vcpu=1` in `runtime\bin\config.txt`: measured on the
reporting machine, it is the configuration that always boots. The retry logic
also halves the vCPU count by itself when two attempts in a row hang at the
same serial line.

**"boot hung" often meant "your machine is slower than mine".** *Fixed after
0.2.1; if you are running 0.2.1 or earlier, this is probably what you are
hitting.*

The shell used to give the kernel sixteen seconds to produce two hundred lines
of serial output and, failing that, declare the boot hung and blame a race in
bringing up the secondary vCPUs under WHPX. On the Surface Pro 12" above the
kernel was producing 119 lines in those sixteen seconds **and still climbing** —
each retry got further than the last. Nothing was hung; the watchdog was killing
a healthy boot and then naming the wrong cause, which sent everyone looking for
a non-deterministic fault that was not there.

The watchdog now judges **progress**: as long as serial lines keep arriving the
boot is alive and it waits, up to two minutes. A hang is silence — no new line
for eight seconds. The message says which of the two happened, because they send
you to different places.

The WHPX race is real and separate: QEMU stays alive and the serial produces
**nothing at all**, not a slow trickle. If you see zero lines across every
attempt, `vcpu=1` in `runtime\bin\config.txt` is worth trying — with a single
vCPU there are no secondary vCPUs to race.

The shipped default is `vcpu=6`, tuned for the machine this was developed on.
There is no evidence yet that it is a good default anywhere else.

**The binaries are not signed.** Windows SmartScreen will warn, and on some
policies will refuse. There is no certificate and no plan to buy one yet.

**There is no updater.** New versions are new downloads. Nothing checks, nothing
notifies, nothing migrates your data directory.

## Graphics

**Vulkan does not reach the guest.** The guest gets OpenGL ES through virgl on
top of a Direct3D 12 backend. The Vulkan path (Venus) is not working: the host
side refuses to create an instance on the drivers available here, so anything
that needs Vulkan in the guest falls back or fails. Applications that require
Vulkan will not run.

**The guest's GL driver is older than the host's.** The host renders through a
Mesa 26.2.0 build shipped with the release; the guest uses the GLES driver that
comes inside the Android image, which is older. Replacing it is possible but has
not been done: the version in the image and the one on the host do not have to
match, and updating only one side has repeatedly proven not to be the win it
looks like.

**High resolutions cost more than they look.** Rendering the guest at the scaled
resolution of a 200% display means four times the pixels. The cost is measured
and written next to the `scala_guest` key in `runtime/bin/config.txt`: median GPU
time roughly doubles. The default is off for that reason.

**Touch-to-pixel latency is about 16 ms**, roughly two frames at 120 Hz. About
half of that is the frame drawing itself, which at 120 Hz has an 8.3 ms budget
and uses most of it; the rest is the sampling phase and the wait for the host to
present. Delivering the touch to the application adds nothing measurable.

## Host and lifecycle

**The virtual machine freezes when Windows sleeps.** Modern standby suspends the
whole VM: the guest's monotonic clock stops with its virtual CPUs while the wall
clock keeps going. On resume Android's own watchdog notices that hours passed
without its handlers running and spends the next few seconds recovering. During
that window `adb shell` answers but anything going through the Android framework
— `pm`, `dumpsys`, `am` — returns nothing. It recovers on its own. A VM left
running overnight on a laptop that sleeps may not have booted at all.

**Nothing keeps the machine awake while the VM runs.** Whether it should is an
open design question — holding a laptop awake for a background VM is a battery
decision, not obviously the right default.

## Packaging and licensing

**The license inventory of the runtime libraries is incomplete.** The release
ships 107 DLLs from MSYS2 under a mix of permissive and copyleft licenses. "A
mix" is not good enough for a distribution meant for other people, and finishing
that inventory is outstanding work. See `app/release/LICENSES/DLL-MSYS2-NOTA.txt`.

**The Android images are not redistributed.** They download themselves from
Waydroid's official sources on first run. This is deliberate — it keeps their
licenses off this repository and off the release archive — but it means the
first run needs a network connection and depends on someone else's server.

**The audio HAL cannot be rebuilt from this repository.** `audio.primary.waydroid.so`
was extracted from an already-modified vendor image and is committed as a binary.
It is also tied to the vendor build it came from, and the product checks that tie
on first run.

**SELinux debt.** Two components — the DRM compositor and the audio HAL — are
installed in a way that does not carry proper SELinux labelling. It works, and it
is not how it should end.

## Reporting something

Open an issue with: what you ran, what you expected, what happened, the contents
of `guest/logs/` if the product got far enough to write them, and the machine —
SoC, Windows build, GPU driver version. A report that names the hardware is worth
several that do not.

If it did not boot, add the `vcpu` value you used and whether `vcpu=1` changed
anything. And say so even when the answer is "still nothing": a workaround that
does not work is as informative as one that does, and right now the sample size
for every one of these questions is one.
