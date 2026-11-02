# BlueStacks comparison and Habumi feature backlog

Reviewed 15 October 2026 against Habumi development commit `628db63`; this
catalog is an evolving source and feature audit, not a compatibility or
performance guarantee. Status below reflects the 0.3.0-rc.2 package checked on
one development PC. Stable release remains 0.2.3. RC2 validation is recorded in
[the release-readiness report](RELEASE-READINESS-0.3.0-rc.2.md).

The RC2 package includes the A2 offline data panel and A3 compact interface.
Manual acceptance covers settings save/restart, fullscreen entry/exit,
screenshots, manual rotation, and physical touch and keyboard in both
orientations. App-requested orientation, other hardware, multiple monitors,
overnight use and hibernation remain open. This update changes Habumi status;
it does not change the BlueStacks reference claims.

## Comparison boundary

The reference is **BlueStacks 5 for Windows**, using its official support pages
linked below. Its [published requirements][bs-requirements] list Intel/AMD CPUs;
Habumi targets Windows 11 ARM64 with ARM64 guest execution through WHPX and the
native Qualcomm Vulkan path. [BlueStacks Air][bs-air] targets Apple Silicon/macOS
and is a separate product. Air features and BlueStacks X cloud services must not
be silently attributed to the Windows App Player.

This comparison does not establish relative speed, battery life, game coverage,
or a percentage of feature parity. Habumi's historical synthetic overhead estimate
is not a BlueStacks comparison or a demonstrated <=10% product-wide bound.

## Status and priorities

- **Available:** implementation and/or recorded feature evidence exists. This
  does not imply every app, hardware configuration or development profile passes.
- **Partial:** a working component, text setting or development script exists,
  but the comparable user workflow is incomplete.
- **Missing:** no Habumi-owned implementation was found in the audited shell,
  display/input backend and guest helpers. An Android app, generic QEMU feature
  or an ADB command is not counted as a shipped Habumi feature.
- **Validate:** coverage, integration or packaging work; not an absent feature.
- **Scope:** a documented product difference needing a decision, not an approved
  implementation commitment.

**P1** = next everyday-use/reliability work; **P2** = broader gaming/product tools;
**P3** = optional expansion. Priorities are recommendations, without dates.
IDs identify independently reviewable work areas, not equal-sized units of work.

## Existing foundations: do not reimplement these

| Capability | Habumi evidence | Remaining boundary |
| --- | --- | --- |
| Android 13 applications | [Runtime guide](NATIVE-VULKAN.md), [VM launch](../app/guscio/vm.c) | ARM64-focused; no claim that every Android app runs. BlueStacks also documents [Android 13][bs-android13]. |
| APK drag/drop install | [APK detection/install](../app/guscio/apk.c), [drop orchestration](../app/guscio/rilascio.c) | Ordinary APKs; archive/split-package management is separate below. |
| File and folder import | [Download-folder transfer](../app/guscio/file.c), [drop orchestration](../app/guscio/rilascio.c) | Windows -> Android exists; a bidirectional media browser does not. |
| Text clipboard in both directions | [Host bridge](../app/guscio/appunti.c), [guest service](../guest/appunti-guest/AndroidManifest.xml) | Profile ports and installed guest helper must match; not image/file clipboard parity. |
| Mouse, touch and multi-touch | [Input backend](../qemu/ui-winq/winq-input.c) | Physical touch after the new rotation transform still needs a live check. |
| Keyboard-to-touch, WASD and click-to-learn | [Profile menu](../app/guscio/main.c), [mapping grammar](../qemu/ui-winq/winq-mappa.c), [sample profile](../runtime/keymaps/generico.txt) | This is a real mapper, but not the full graphical/control suite below. |
| Home, Back, Recents, Power, volume | [Asynchronous controls](../app/guscio/comandi.c), [toolbar](../app/guscio/finestra.c) | More hotkeys, mute and device choices remain separate work. |
| Manual portrait/landscape rotation | [Live development record](NATIVE-INTERFACE-2026-10-15.md) | New shared-texture/input transform is not in the published archive. |
| CPU/RAM, resolution/DPI, refresh and audio configuration | [Config parser](../app/guscio/config.c), [documented keys](../runtime/bin/config.txt) | Most settings require editing text; refresh advertisement is not proof of an app FPS limit. |
| GApps/vanilla selection with separate data | [Variants](../app/guscio/varianti.c), [switch flow](../app/guscio/main.c) | One running VM, two variants of the pinned image family; not multi-instance or multi-version Android. |
| Playback and capture audio paths | [VM audio wiring](../app/guscio/vm.c), [known audio limits](../KNOWN-ISSUES.md) | Default host routing; wider app, latency and device-selection validation remains. |
| Boot/shutdown checks and session logs | [VM lifecycle](../app/guscio/vm.c), [logging](../app/guscio/registro.c), [log archives](../app/guscio/archivio.c) | Log archiving is not a backup of Android apps/data. |

## Settings, power and window tools

BlueStacks references: [settings][bs-settings], [performance modes][bs-performance],
[Eco mode][bs-eco], [memory trimming][bs-trim], [toolbar][bs-toolbar],
[shortcuts][bs-shortcuts] and [pinning/mini mode][bs-pin]. Habumi evidence:
[configuration](../app/guscio/config.c), [toolbar](../app/guscio/finestra.c),
[startup refresh selection](../app/guscio/vm.c), [display loop](../qemu/ui-winq/winq-ciclo.c),
[window backend](../qemu/ui-winq/winq-window.c).

| ID | Priority | Capability | Status | Work remaining in Habumi |
| --- | --- | --- | --- | --- |
| G01 | P1 | Graphical settings | Partial | More > Settings edits CPU/RAM, dimensions, refresh, density, audio and guest scaling, with atomic saves that preserve comments/unknown keys. Restart is required; applied GPU values are unavailable and graphics-backend selection is not exposed. |
| G02 | P1 | Eco/background mode | Partial | Existing idle heartbeat and startup `battery_hz` are useful foundations. Add user controls and measure guest work reduction; do not equate fewer host polls with fewer rendered frames. |
| G03 | P1 | FPS/performance overlay | Missing | Present an optional app/guest frame-rate view with clearly defined counters; diagnostic logs alone are not an overlay. |
| G04 | P2 | Memory trimming | Missing | Add measured host/guest memory reclamation with evidence of usable RAM recovery; not just changing a working-set number. |
| G05 | P2 | Performance presets | Partial | Build named presets from existing settings, document tradeoffs and test representative apps. |
| G06 | P2 | Graphics/GPU selection | Partial | WGL/ANGLE selection exists in text and native Vulkan uses a launch environment. Expose supported choices and report actual GPU/API; do not offer incompatible drivers. |
| G07 | P2 | ASTC decoding preference | Missing | No product setting or supported decoding-policy workflow found; first audit actual native-driver capabilities. This is not a claim that ASTC is universally absent. |
| G08 | P2 | Audio device selection/mute | Partial | Existing playback/capture, volume controls and startup audio switch need host speaker/mic selection and an immediate mute workflow. |
| G09 | P1 | Fullscreen | Available | More toggles fullscreen and remains available for Exit full screen. There is no F11 promise; additional DPI, multi-monitor and hardware checks remain. |
| G10 | P1 | Cursor confinement | Missing | Add explicit lock/unlock and release on focus loss; ordinary mouse-as-touch does not provide confinement. |
| G11 | P2 | Fullscreen preference per app | Missing | Remember app-specific launch/display choices after a package-aware launcher exists. |
| G12 | P2 | Configurable shortcuts | Partial | Tab navigation and the key-map hotkey exist. Add a conflict-aware shortcut editor for product actions. |
| G13 | P3 | Desktop notification integration | Missing | Add explicit notification preferences and a guest-to-Windows bridge if desired. Android's notification shade already exists. |
| G14 | P3 | UI language selection | Partial | English product strings exist; localization resources and a selectable UI language do not. |
| G15 | P3 | Hide/collapse controls, quick hide | Partial | The shell can already be minimized separately. Add an intentional compact/hide-controls workflow and optional quick-hide shortcut. |
| G16 | P3 | Pin-to-top/mini player | Missing | Add opt-in topmost/compact playback without trapping focus or input. |

## Applications, files, capture and maintenance

BlueStacks references: [media manager][bs-media], [toolbar APK/XAPK and capture][bs-toolbar],
[recording][bs-recording], [desktop app shortcuts][bs-app-shortcuts],
[backup/restore][bs-backup], [disk cleanup][bs-cleanup], [updates/settings][bs-settings],
[problem reporting][bs-report]. Habumi evidence: [APK](../app/guscio/apk.c),
[file transfer](../app/guscio/file.c), [initial data creation](../app/guscio/dati.c),
[logging](../app/guscio/registro.c), [release validation](RELEASE-READINESS-0.3.0-rc.2.md).

| ID | Priority | Capability | Status | Work remaining in Habumi |
| --- | --- | --- | --- | --- |
| G17 | P2 | Integrated app library | Partial | Android's launcher already lists and launches apps. A profile-aware library with app-info/uninstall access in the Habumi shell is absent; this is a desktop-integration gap, not missing Android app launching. |
| G18 | P1 | Windows app shortcuts | Missing | Create shortcuts that start the correct profile/environment, wait for readiness and launch the selected package. |
| G19 | P1 | Media browser/export | Partial | File import exists. Add guest file selection and export to Windows with clear destinations and filename handling. |
| G20 | P2 | XAPK/split APK installation | Missing | Ordinary APK installation is present. Add archive/split validation and OBB/data handling where applicable. |
| G21 | P1 | Screenshot action | Partial | More captures an on-request validated PNG to Pictures/Habumi and can open that folder. A destination chooser and video recording are not included. |
| G22 | P2 | Video recording | Missing | Add start/stop, output path, audio policy and a validated encoder path without forcing unnecessary extra copies. |
| G23 | P3 | Rolling highlight capture | Missing | Optional recent-gameplay buffer with an explicit memory/power budget; vendor documentation gives differing durations, so none is used as an acceptance target. |
| G24 | P3 | Capture gallery/video editing | Missing | Optional gallery and basic clip management; editing is a separate cost from recording. |
| G25 | P1 | In-app update checks/install | Missing | There is no automatic updater. Add version checks and an update workflow with rollback and preserved user data; manual GitHub downloads remain available. |
| G26 | P1 | Upgrade/data migration | Partial | Version/reference-image metadata and journaled recovery support maintenance. Cross-version Android image migration is not supported; backup/restore requires matching pinned system/vendor images. |
| G27 | P1 | Instance backup/restore | Available | The offline data panel and CLI back up and restore profiles when matching pinned images are present. This does not provide simultaneous instances or cross-image migration. |
| G28 | P2 | Disk reclaim/cleanup | Missing | Assess the raw-image format, guest discard and host space recovery. Removing an Android file alone does not prove Windows disk space was reclaimed. |
| G29 | P1 | Diagnostic export/report | Partial | The data panel exports bounded logs, host/driver details and requested config. Applied settings remain unavailable; there is no automatic upload. |

## Instances and gaming controls

BlueStacks references: [instance manager][bs-instances], [synchronized actions][bs-sync],
[controls editor][bs-controls], [advanced controls][bs-advanced-controls],
[control overlay/menu][bs-controls-menu], [scheme import/export][bs-control-transfer],
[gamepads][bs-gamepad], [gamepad modes/vibration][bs-gamepad-settings],
[macros][bs-macros], [an example of game-specific smart controls][bs-smart].
Habumi evidence: [single-instance mutex and map menu](../app/guscio/main.c),
[command pipe](../app/guscio/tubo.c), [VM drives](../app/guscio/vm.c),
[mapper](../qemu/ui-winq/winq-mappa.c), [learn mode](../qemu/ui-winq/winq-window.c).

| ID | Priority | Capability | Status | Work remaining in Habumi |
| --- | --- | --- | --- | --- |
| G30 | P2 | Concurrent instances | Missing | Namespace mutex, close event, pipe, ports, clipboard helper and writable disks; retain per-instance process ownership. |
| G31 | P2 | Instance create/clone/manage | Missing | Add named profiles, creation/cloning and consistent data ownership; switching two variants is not instance management. |
| G32 | P3 | Batch start/arrange instances | Missing | Add selection, grouping and window layout after isolation is implemented. |
| G33 | P3 | Synchronized input | Missing | Replay scoped actions across selected instances with clear source/target state and an immediate stop. |
| G34 | P1 | Graphical keymap editor | Partial | Keep existing tap/WASD/click-to-learn; add visible control objects, move/delete/edit, save/cancel and orientation checks. |
| G35 | P2 | Per-game schemes/presets | Partial | Text profiles can be selected manually. Add package associations, automatic selection and tested preset distribution. |
| G36 | P2 | Scheme import/export manager | Partial | Text files are portable; add validated import/export, duplicate/rename/reset and clear format/version errors. |
| G37 | P2 | Repeated tap | Missing | Add repeat cadence/count and release/cancel semantics; current key-repeat suppression is not a repeated-tap feature. |
| G38 | P2 | Advanced mapped gestures | Missing | Add swipe, pinch/zoom, gesture rotation and tunable scrolling; WASD is not MOBA skill casting, tilt or edge-scroll. Native physical gestures are a different path. |
| G39 | P2 | Mouse aim/free look/MOBA | Missing | Add relative mouse capture, sensitivity, release behavior and game-specific modes. Basic absolute mouse touch already works. |
| G40 | P3 | Context-aware smart controls | Missing | Optional game-state-aware control switching; this requires per-game maintenance and should follow reliable manual controls. |
| G41 | P1 | Gamepad support/mapping | Missing | Add host controller detection, native guest delivery and/or controller-to-touch mapping, hotplug and calibration. WASD 'joystick' is keyboard input. |
| G42 | P3 | Gamepad vibration | Missing | Add optional rumble after a real controller path exists. |
| G43 | P2 | Macro recorder/player | Missing | Record/replay timed actions with stop/cancel, repeat, speed and portable files; unrelated VM test scripts are not a macro manager. |
| G44 | P3 | User control scripts | Missing | Define a bounded action language with visible execution state and cancellation. Existing build/diagnostic scripts do not provide this. |
| G45 | P2 | Visible key hints/opacity | Partial | Click-to-learn feedback exists. Add a persistent optional mapping overlay, visibility and opacity controls during play. |

## Devices and convenience tools

BlueStacks references: [device settings and ADB][bs-settings],
[location/shake/offline actions][bs-toolbar], [screen translation][bs-translation].
Habumi evidence: [VM device launch](../app/guscio/vm.c),
[configuration](../app/guscio/config.c), [host/guest helper inventory](../guest).

| ID | Priority | Capability | Status | Work remaining in Habumi |
| --- | --- | --- | --- | --- |
| G46 | P2 | Host camera bridge | Missing | No supported host-webcam-to-Android camera workflow found. A guest Camera app does not establish passthrough. |
| G47 | P3 | Location selector | Missing | Optional explicit simulated location control; existing roadmap excludes general sensor simulation, so scope approval comes first. |
| G48 | P3 | Shake/tilt simulation | Missing | Optional sensor events and calibration; keep distinct from touch-gesture rotation and Android screen orientation. Same scope decision as G47. |
| G49 | P3 | Screen text translation | Missing | Optional capture/OCR/translation workflow with a visible data destination; no such Habumi feature was found. |
| G50 | P2 | Offline/network toggle | Missing | Add a host-facing network control with clear persistence/state; Android settings alone are not an equivalent toolbar action. |
| G51 | P1 | Debug/ADB product preference | Partial | ADB and configurable host ports exist and underpin current controls. Add an explicit mode only with a clear account of which controls require it and a supported alternative lifecycle path. |

## Different scope and Habumi-specific work

These are recorded so they are not forgotten, but they are not automatically
added to the implementation plan.

| ID | Type | Difference and decision |
| --- | --- | --- |
| S01 | Scope | BlueStacks offers [multiple Android versions][bs-instances]. Habumi pins one Android 13 image family; GApps/vanilla are not different Android versions. Decide whether maintaining more versions is worth the compatibility/build cost. |
| S02 | Scope | BlueStacks exposes [ARM/x86 and 32/64-bit ABI choices][bs-abi]. Habumi's native ARM64 focus is intentional; do not promise x86-only or ARM32-only app compatibility. An ABI/translation expansion would change the project scope. |
| S03 | Scope | BlueStacks has [phone profiles][bs-phone] and network-provider settings in its [preferences][bs-settings]. Habumi does not expose these; the existing roadmap excludes device/telephony simulation. |
| S04 | Scope | The [toolbar][bs-toolbar] documents Play Together remote sessions. No Habumi equivalent was found; remote input/streaming would be a separate product area. |
| S05 | Scope | The [feature overview][bs-overview] describes a proprietary store/cloud entry point. Habumi has the Android/GApps route, not that commercial catalog, rewards or vendor services. No matching ecosystem is proposed. |
| H01 | P1, Available | Offline TCG maintenance grows a selected image from 4 to 16 GiB, verifies the staged copy and retains the original. Growth does not support shrinking. |
| H02 | P1, Partial | `battery_hz` is selected on startup. Add live AC/battery/background policy changes only after measuring their effect; Eco branding alone would not establish an energy saving. |
| H03 | P1, Partial | Manual rotation and physical touch/keyboard passed in portrait and landscape. App-requested orientation, other graphics paths and additional hardware remain unverified. |
| H04 | P1, Missing | Sign distributed binaries and finish release provenance/dependency/license coverage. Portability without an installer remains a valid design choice. |

## Validation gates: completion is not just more buttons

| ID | Status | Required evidence |
| --- | --- | --- |
| V01 | Validate | Source-built, packaged SystemUI/media corrections; current [SystemUI](ANDROID-SYSTEMUI-NULL-PROBE-2026-10-15.md) and [OMX](ANDROID-MEDIA-STABILITY-2026-10-15.md) fixes are isolated experiments. |
| V02 | Validate | Fresh first-run download dialogs, both variant-switch directions, cancellation/retry, hash migration and failure recovery. |
| V03 | Validate | Google sign-in/Play Store download/update and representative app/game install-launch-resume workflows; installed GApps is not certified universal compatibility. |
| V04 | Validate | Real playback and decode workloads, microphone capture, latency and interaction/audio checks; AVC initialization alone is not successful video decoding. |
| V05 | Validate | Repeated real standby/hibernate and resume during boot/running/closing, plus additional Snapdragon hardware/drivers. |
| V06 | Validate | Clipboard helper/port agreement, bidirectional text and larger/Unicode content; current isolated profile's port mismatch is a profile issue, not a missing clipboard engine. |
| V07 | Validate | Windows light/dark/high-contrast/live theme changes, resizing, keyboard navigation, fullscreen and DPI across monitors as those features evolve. |
| V08 | Validate | Matched-resolution/API/workload native-Windows vs Android measurements, frame-time distributions and energy per useful workload. No universal <=10% overhead claim. This is separate from BlueStacks feature parity. |

## Remaining work

The RC2 implementation closes the settings, fullscreen, screenshot and manual
rotation portions of G01, G09, G21 and H03. Continue with app-requested
orientation, destination selection, applied-settings reporting, G18 shortcuts,
and release validation gates. The wider parity backlog remains open; these
entries do not imply that A5 or the broader catalog is complete.

Prioritize stability, data-recovery and hardware validation before expanding
storage/profile workflows. Keep performance and power comparisons tied to a
fixed workload, resolution, refresh, power source and warm-up condition.

Every implementation should name the IDs it closes, record what was tested,
and distinguish source completion, isolated-profile validation and release inclusion.
This catalog is broad coverage of the reviewed public feature surfaces, not a
claim to enumerate undocumented internals or every future vendor feature.

[bs-requirements]: https://support.bluestacks.com/hc/en-us/articles/360056129211-System-requirements-for-BlueStacks-5
[bs-air]: https://support.bluestacks.com/hc/en-us/articles/32272913555597-System-specifications-for-installing-BlueStacks-Air
[bs-settings]: https://support.bluestacks.com/hc/en-us/articles/360053245871-How-to-use-the-BlueStacks-5-Settings-menu
[bs-overview]: https://support.bluestacks.com/hc/en-us/articles/360055360872-All-you-need-to-know-about-BlueStacks-5
[bs-android13]: https://support.bluestacks.com/hc/en-us/articles/34110431525261-How-to-play-games-with-Android-13-Beta-on-BlueStacks-5
[bs-performance]: https://support.bluestacks.com/hc/en-us/articles/4404389785997-How-to-use-Performance-mode-on-BlueStacks-5
[bs-eco]: https://support.bluestacks.com/hc/en-us/articles/360052834772-How-to-run-multiple-instances-of-BlueStacks-5-more-efficiently-using-Eco-mode
[bs-trim]: https://support.bluestacks.com/hc/en-us/articles/360055624731-How-to-trim-memory-on-BlueStacks-5
[bs-toolbar]: https://support.bluestacks.com/hc/en-us/articles/360055672212-How-to-use-the-Side-toolbar-in-BlueStacks-5
[bs-shortcuts]: https://support.bluestacks.com/hc/en-us/articles/4402768474381-How-to-view-or-edit-shortcut-keys-on-BlueStacks-5
[bs-pin]: https://support.bluestacks.com/hc/en-us/articles/4403065291661-How-to-pin-BlueStacks-5-above-all-other-applications-on-your-PC
[bs-media]: https://support.bluestacks.com/hc/en-us/articles/360055684992-How-to-use-the-Media-Manager-on-BlueStacks-5
[bs-recording]: https://support.bluestacks.com/hc/en-us/articles/360059539352-How-to-record-your-gameplay-in-BlueStacks-5
[bs-app-shortcuts]: https://support.bluestacks.com/hc/en-us/articles/4402959142541
[bs-backup]: https://support.bluestacks.com/hc/en-us/articles/360059480692-How-to-backup-and-restore-data-on-BlueStacks-5
[bs-cleanup]: https://support.bluestacks.com/hc/en-us/articles/4415101691277-How-to-free-up-disk-space-using-Disk-cleanup-on-BlueStacks-5
[bs-report]: https://support.bluestacks.com/hc/en-us/articles/360053307531-How-to-report-a-problem-on-BlueStacks-5
[bs-instances]: https://support.bluestacks.com/hc/en-us/articles/360052834092-How-to-create-and-manage-instances-using-the-Multi-instance-Manager-on-BlueStacks-5
[bs-sync]: https://support.bluestacks.com/hc/en-us/articles/4403071374861-Sync-operations-on-BlueStacks-5
[bs-controls]: https://support.bluestacks.com/hc/en-us/articles/360058928871-How-to-create-and-edit-controls-in-BlueStacks-5
[bs-advanced-controls]: https://support.bluestacks.com/hc/en-us/articles/360058638032-Advanced-settings-for-key-controls-in-BlueStacks-5
[bs-controls-menu]: https://support.bluestacks.com/hc/en-us/articles/360055522292-How-to-use-the-Game-controls-menu-on-BlueStacks-5
[bs-control-transfer]: https://support.bluestacks.com/hc/en-us/articles/24808506186509-How-to-import-or-export-game-controls-on-BlueStacks-5
[bs-gamepad]: https://support.bluestacks.com/hc/en-us/articles/360059482992-Gamepads-on-BlueStacks-5
[bs-gamepad-settings]: https://support.bluestacks.com/hc/en-us/articles/4405061625485-How-to-choose-your-preferred-gamepad-settings-on-BlueStacks-5
[bs-macros]: https://support.bluestacks.com/hc/en-us/articles/360056012412-How-to-record-and-manage-macros-using-the-Macro-manager-on-BlueStacks-5
[bs-smart]: https://support.bluestacks.com/hc/en-us/articles/360059239591-Free-Fire-on-PC-with-BlueStacks-5
[bs-translation]: https://support.bluestacks.com/hc/en-us/articles/4405387734157-How-to-translate-on-screen-text-for-a-regional-game-in-BlueStacks-5
[bs-abi]: https://support.bluestacks.com/hc/en-us/articles/360058929011-What-is-Application-Binary-Interface-ABI-in-BlueStacks-5
[bs-phone]: https://support.bluestacks.com/hc/en-us/articles/4410817985293-How-to-create-a-custom-device-profile-on-BlueStacks-5
