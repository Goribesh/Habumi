# Roadmap

What would make this better, roughly in the order that the effort pays off.
Nothing here is promised and nothing has a date. Items marked **help wanted**
are the ones where someone else's machine or expertise matters more than more
hours on this one.

Read [KNOWN-ISSUES.md](KNOWN-ISSUES.md) first: most of what follows is the other
half of something written there.

## Worth doing first

**Run it on hardware that is not ours.** — *help wanted*
Every "it works" in this repository has a sample size of one machine. Two or
three reports from different Snapdragon generations, different Windows builds and
different GPU driver versions would either raise confidence a lot or find the
first real bug. This is the single highest-value contribution right now, and it
costs a download.

**Finish the license inventory of the runtime DLLs.**
107 libraries from MSYS2, licenses collected as "a mix". Mechanical work, no
special knowledge needed, and it is what stands between this and a distribution
someone can redistribute without reading tea leaves.

**Decide what happens when the host sleeps.**
Today the VM freezes with the machine and Android takes a watchdog hit on resume.
Two honest options: hold the machine awake while the VM runs, or detect the
resume and tell the user what happened. The first costs battery, the second costs
nothing but does not fix anything. Neither has been chosen.

## Graphics

**Get Vulkan through to the guest.**
The Venus path is the reason the guest still runs GL on top of a translation
layer. Making it work would remove a whole layer for applications that ask for
Vulkan, and would open the door to replacing the guest's GL driver with a current
one. It is also the hardest item on this page: the blocker is on the host side,
in instance creation, and it is not obviously ours.

**Update the guest's GL driver.**
Only worth doing together with the item above, or the newer driver has nothing
better to talk to.

**Reduce the drawing cost per frame.**
At 120 Hz the budget is 8.3 ms and drawing uses most of it. The delivery of input
to the application adds nothing measurable, so the gain has to come from the
frame itself, not from the plumbing around it.

## Product

**Sign the binaries.**
A certificate turns "Windows protected your PC" into a normal install. Costs
money, not time.

**An update mechanism.**
Today a new version is a new download and a manual copy. Anything better needs a
decision about where the data directory lives across versions.

**A way to install applications that is not `adb install`.** — *help wanted*
Dragging an APK onto the window, or a file association, would remove the single
most common piece of friction for anyone who is not a developer.

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
