Habumi - README
=======================


WHAT THIS IS
------------

An Android emulator for Windows 11 ARM64: it runs NATIVE ARM64 Android,
with no instruction translation at all. Your PC's processor executes
directly the same ARM64 instructions a real phone would run, instead of
translating them from another architecture: that is why it runs well on
a Windows PC with an ARM64 processor.


WHAT YOU NEED
-------------

- Windows 11 ARM64. The program uses the "tar.exe" included in
  Windows 11 to extract the images it downloads: that is a real
  dependency, not a detail.
- About 8 GB of free disk space.
- About 805 MB of network traffic on the first run (see below).
- The Windows feature "Windows Hypervisor Platform" enabled.
  If it is missing, the program tells you at startup and shows you the
  command to enable it. It is the only thing that needs administrator
  rights, it is done once, and it wants a reboot.


HOW TO START IT
---------------

Unpack this archive into whatever folder you like - any path works -
and double-click:

    Start.cmd

which sits in the main folder, the one you see right after unpacking.
A black command-prompt window flashes for an instant: that is normal,
it is only launching the real program, which is

    runtime\bin\Habumi.exe

and which you can start directly instead, if you prefer.

There is nothing to install: no Windows registry entries and no
administrator rights to USE the program. The only thing that needs them
is enabling Windows virtualization, if it is not already on - see WHAT
YOU NEED above. To uninstall, see further down: you delete the folder.


THE FIRST TIME, WINDOWS WILL WARN YOU
--------------------------------------

This program is not signed with a certificate: signing costs money, and
for now it is not signed. On the first run Windows will therefore show
the blue "Windows protected your PC" screen. To go ahead: "More info",
then "Run anyway".

It is a warning about where the file CAME FROM, not about what is in it:
Windows is saying it does not know who wrote it, not that it found
something.


WHAT HAPPENS ON THE FIRST RUN
------------------------------

On the first run the program finds the Android images missing and
downloads them by itself, from the official images of the Waydroid
project hosted on SourceForge:

- "vendor.img" (an archive of about 74 MB)
- "system.img" (an archive of about 728 MB)

For each of them it verifies the sha256 the Waydroid project publishes
before using it, so an interrupted or corrupted download does not pass
unnoticed. At the end it also creates the Android "/data" by itself
(starting from an empty image included in the package).

So an Internet connection is needed on the first run. On later runs the
images are already on disk and are not downloaded again.


THE TWO VARIANTS: VANILLA AND GAPPS
------------------------------------

The program can run Android in two variants:

- VANILLA: without the Google apps (Play Store, Gmail, and so on).
- GAPPS: with the Google apps included.

You choose with the "variant" button in the program window. The first
time you choose GAPPS, the program downloads its image (about 1.17 GB)
and asks you before doing it, telling you how many bytes it will
download.

The two variants live side by side: each has its own separate "/data",
that is, its own installed apps and its own data. Switching from one to
the other does NOT erase anything: you can go back to the previous
variant and find everything as you left it.


THE KEY MAP
-----------

For games that play better with a keyboard than with fingers, the "keys"
button opens a menu with the available mapping profiles, in
"runtime\keymaps\" (.txt files): each profile assigns a key to a point
on the Android screen, and the "WASD" keys can become a virtual stick.

There is also a quick shortcut, "Ctrl+Alt+T", which turns the last
chosen profile on or off even when the program window is not the active
one.

WARNING: while the program is open, the "Ctrl+Alt+T" combination is
taken away from ALL of Windows, not just from the program. If another
application uses that same shortcut, it will not work while
Habumi.exe is running.


SETTINGS
--------

"runtime\bin\config.txt" holds the settings: number of virtual CPUs,
memory, resolution, refresh rate, and a few more. Every key carries,
right next to it, the measurement that decided its value - so you can
tell what you are trading before you change one.

A key that cannot be read, or that is out of range, does NOT stop the
program: it is ignored and the reason goes into the log.


HOW TO UNINSTALL
----------------

Delete the folder where you unpacked the archive. The program leaves
nothing anywhere else: no registry entries, no files outside its own
folder.


IF SOMETHING GOES WRONG
------------------------

The program's logs are in the "guest\logs\" folder: in particular
"registro-guscio.log" (the program's own decisions) and
"sessione-viva.log" (what Android says while booting). The last ten
sessions are archived in "guest\logs\archivio\", so a problem that
happened on the previous run is not lost when you restart.

In the log, lines are tagged by who wrote them: [shell] is this program,
[qemu] is the emulator, [guest] is Android itself.

One thing in the log is expected and is not a fault. Android keeps
looking for an old media component called OMX, about once a second and
forever, because this system image does not ship it: it uses the newer
Codec2 path instead, and video and audio decoding work through that.
Those lines would fill the log, so the program hides them and keeps
count, writing "OMX HAL lookups suppressed so far: N" every hundred.
Seeing that number grow is normal.


LICENSES AND SOURCE CODE
=========================

This program includes third-party components under different licenses.
The full texts are in the "LICENSES\" folder, next to this file.

QEMU, AND IT IS MODIFIED
-------------------------

The heart of the emulation is QEMU, which is free software under the
GNU General Public License version 2 (GPLv2). Our copy of QEMU is NOT
the original one: it contains "winq", a display backend written by us,
integrated into QEMU's source tree. The GPL requires anyone who
redistributes a modified binary to offer the corresponding source - and
this is that offer, made precisely rather than generically:

- Starting point: QEMU version 11.0.3, downloadable from the project's
  official site, https://www.qemu.org (sources also at
  https://download.qemu.org/qemu-11.0.3.tar.xz).
- Our changes: the patches included in "qemu/patches/" in this
  project's repository. Applied on top of exactly the version named
  above, they reproduce the code running in this package.
- The full text of the license: "LICENSES\QEMU-COPYING.txt", copied
  verbatim from our own copy of QEMU.

Anyone who wants the corresponding source can write to the address
given on the page you obtained this archive from.

MESA, AND IT IS MODIFIED TOO
----------------------------

"opengl32.dll" in "runtime\bin" is Mesa, primarily under the MIT
license. It is NOT the opengl32.dll that comes with Windows, and it is
not a stock Mesa build either: it is the "d3d12" driver, built by us for
Windows ARM64, with five patches on top. Without it this package would
fall back on the system's OpenGL and lose what those patches do - one of
them is the difference between starting and not starting at all.

Starting point, the patches one by one, the MIT text and the fact that
Mesa is NOT uniformly MIT: "LICENSES\MESA-NOTA.txt".


VIRGLRENDERER, ALSO MODIFIED
-----------------------------

"libvirglrenderer-1.dll" is virglrenderer, MIT: it turns the guest's
graphics commands into commands for the host's GPU. Our copy carries a
fork's worth of changes, and the patches are in "research/patches/" in
this project's repository. The license is MIT; the upstream project is
at https://gitlab.freedesktop.org/virgl/virglrenderer.


The third-party libraries in runtime\bin
-----------------------------------------

The 107 DLLs in "runtime\bin" - "opengl32.dll" is ours and is
counted separately, above - come from the MSYS2 project (built for
ARM64 with clang) and are all needed for QEMU to work: a census measured
against the running program verified that nearly all of them are loaded
directly by QEMU, and the two remaining ones are needed by "adb.exe".
See "LICENSES\DLL-MSYS2-NOTA.txt" for the details, where they come from,
and - stated openly - what is still missing: the exact license text of
each of the 107 DLLs has not been collected individually. That is work
to be completed before a release aimed at a wider audience than one
internal test, not an oversight being hidden.

adb and its two DLLs
---------------------

"adb.exe", "AdbWinApi.dll" and "AdbWinUsbApi.dll" come from Google's
Android SDK Platform-Tools, distributed under the Apache License 2.0.
The official license text, as Google distributes it together with those
files, is in "LICENSES\ANDROID-PLATFORM-TOOLS-NOTICE.txt".

The audio HAL derived from Waydroid
------------------------------------

A small 19 KB file ("audio.primary.waydroid.so", included in this
project's initramfs) is derived from the audio HAL of the Waydroid
project, itself based on the Android Open Source Project, under the
Apache License 2.0 - a license that allows redistributing a modified
binary, provided its origin is attributed. The license text is in
"LICENSES\APACHE-2.0.txt", and the detailed attribution (what was
changed and why) is in "LICENSES\HAL-AUDIO-ATTRIBUZIONE.txt".

The Android images
-------------------

"vendor.img" and "system.img" (see "WHAT HAPPENS ON THE FIRST RUN") are
not redistributed by this package: they download themselves, unmodified,
from the Waydroid project's official sources on the first run, so their
licenses do not apply to the archive you unpacked.
