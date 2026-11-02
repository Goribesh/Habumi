Habumi 0.3.0-rc.2 prerelease
===========================

STATUS: experimental prerelease; not a stable release

Habumi runs Android 13 on Windows 11 ARM64. Graphics require a compatible
Qualcomm Vulkan driver. This package was checked on one development PC; other
GPUs and computers have not been validated. No general performance or battery
life guarantee is made.

BEFORE YOU START
----------------

Use a new, separate folder for Habumi. Do not overwrite an older installation
or reuse its Android data. Allow about 12 GB of free disk space and about
1.3 GB of first-run network traffic for the default GApps profile. The first
run downloads the pinned 20260927 GApps and vendor images and checks their
published hashes. Windows Hypervisor Platform must be enabled. Ordinary use
does not require administrator rights; enabling virtualization may require
them and a restart.

START AND STOP
--------------

Unpack the archive, then run Habumi.exe from the top-level folder. The launcher
prepares the graphics environment. Do not start runtime\bin\Habumi.exe
directly. Close Habumi normally and let Android shut down before moving,
backing up, or restoring profile files.

ANDROID DATA
------------

The More menu opens Data management while Android is stopped. It can back up,
restore, recover an interrupted operation, export diagnostics, and grow the
selected data image from 4 GiB to 16 GiB. Growth uses software emulation (TCG),
verifies the expanded copy and retains the original. Growth is one-way; shrinking
is not supported. Backup and restore require the matching pinned system and
vendor images; migrating between image versions is not provided. Backups include
profile data, configuration and key maps. Store backups outside this folder.
Diagnostics contain logs and system details, not data-image contents.
Manage-Data.ps1 is also included for command-line maintenance.

SETTINGS AND DISPLAY
--------------------

Choose More > Settings to change CPU cores, memory, guest width and height,
refresh limits, density, audio, and guest scaling. Saving is atomic and keeps
comments and unknown configuration entries. Close and reopen Habumi to apply
saved settings. Startup config shows requested values; it does not read back
applied display values from the GPU. This build uses its fixed native Vulkan
graphics backend.

Choose More > Full screen to enter fullscreen. More remains available; choose
Exit full screen to return to the previous window. No F11 shortcut is promised.
Use the Rotate control to change Android orientation. Check that apps and input
work as expected after rotation.

Choose More > Save screenshot to capture Android on request. Validated PNG
files are saved under Pictures\Habumi. Choose More > Open screenshots folder
to view them. Capture uses ADB when requested; it does not continuously copy
frames.

KEYS AND CONTROLS
-----------------

The compact right toolbar provides Android navigation and rotation. More
contains additional actions. Habumi supports touch and keyboard input, though
physical input on other hardware has not been established. Key-map profiles
are available under runtime\keymaps; Ctrl+Alt+T toggles the selected profile
while Habumi is open and reserves that shortcut from other Windows apps.

KNOWN LIMITS
------------

This prerelease has been exercised on one development computer. Manual
acceptance covered fullscreen entry and exit, screenshots, rotation, and
physical touch and keyboard in both orientations. Other hardware, multiple
physical monitors, overnight use, and sleep or hibernation need further testing.
The original hardware-watchdog trigger is unknown. Issue #3 retry logic was
corrected, but a slowdown around chr_dev_init/THP remains open and needs
target-hardware logs. No general stability or performance claim follows from
the local checks.

The program is unsigned, so Windows may show a publisher warning. Signing,
complete third-party license review, broader hardware checks, and overnight
and hibernation validation remain pending. There is no automatic updater.
Consult LICENSES for notices and source information. This is not a stable
release.

REPORTING A PROBLEM
-------------------

Include the Windows version, CPU model, and files from guest\logs, especially
registro-guscio.log and sessione-viva.log. Remove personal information before
sharing logs or diagnostics.
