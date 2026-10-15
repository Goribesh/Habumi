# Native interface and variant integrity development

Development after 0.3.0-rc.1; the published release remains unchanged.

## Variant fingerprint correction

The download path verified an upstream ZIP and persisted its SHA-256 in
`runtime/variants.txt`. Startup subsequently hashed the extracted Android
image and compared it with that ZIP hash. These are different contents, so a
correct image could be classified as a wrong hash and downloaded again.

After extraction, the corrected path hashes the image before deleting the
archive or committing state. The persisted variant record uses the image
fingerprint, retaining the archive filename/build identity and compressed byte
count used for download estimates. An image read/hash failure keeps the archive
and leaves state uncommitted. Upstream ZIP verification remains mandatory.

Existing records containing ZIP fingerprints are not silently converted into
trusted image records. They may require one verified download through the normal
flow. No user-data image is reset or migrated by this correction. Vendor records
are unchanged; they are not used by the extracted-image classification path.

The focused regression uses real BCrypt/filesystem hashing through the production
helper, reloads persisted state, distinguishes ZIP and image fingerprints,
detects a modified image and checks output preservation on read failure.

## Compact Windows interface

The Win32 shell now follows the Windows app light/dark preference, prioritizes
high-contrast system colors and refreshes colors when Windows announces theme
changes. The native title bar uses the documented DWM attribute; see
[Microsoft's theme guidance](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes).
Menus and non-client scrollbars retain their OS-managed rendering.

The toolbar retains all nine command IDs and their existing enable/disable
ownership. Standard BUTTON controls retain accessible names and use restrained
owner drawing for readable pressed, disabled and focused states. The status line
shows the latest phase. An Activity log label separates the selectable, bounded
monospace log. DPI layout and a scaled minimum window size retain reachable
controls in compact windows.

Both shell message loops route Tab/Shift+Tab between controls and Enter to the
focused enabled button. Ctrl/Alt shortcuts and unrelated windows are excluded;
native button Space behavior remains intact. Commands are still queued through
the existing asynchronous handlers.

## Validation

The complete shell compiled with `-Wall -Werror -O2`. Existing asynchronous
control checks passed (150), readiness checks passed (187), and shutdown/VM
checks passed (100). The candidate is installed only in Habumi-Startup-Test,
with executable and manifest backups.

The user handled the Windows Firewall prompt before further UI automation.
Visual inspection confirmed the dark theme, current readiness, all nine named
buttons, and toolbar wrapping into two rows at a compact window size of about
451x300 logical pixels on the 200% display. Tab reached Power; Shift+Tab reached
the log. Mouse, Enter and Space on Home each produced a separate delivered-command
log entry. The latest candidate reached Android readiness at 36.625 seconds;
this is an observed boot time, not a performance comparison.

Light/high-contrast appearance and live theme switching have not been visually
verified. These are not broad hardware compatibility or Windows standby guarantees.

## Rotation: development correction and live checks

The live portrait-natural 1600x2560 panel test exposed an assumption in the
Rotate queue: its requested host orientation assumed a landscape-natural panel.
The target now uses the physical panel aspect and current Android rotation.
The focused queue tests cover both natural aspects and both directions.

The first sizing-only correction left Android's raw scanout sideways. The
completed development fix adds an optional clockwise quarter-turn count to the
existing resolution pipe command. Android rotation 1 is compensated by host
rotation 3. Viewport fitting swaps the displayed axes; mouse and pointer input
use the same inverse transform as shader texture coordinates. Pixel-centre
mapping keeps inverted edges in bounds. Legacy two-number commands remain valid.

Unrotated scanout keeps the existing framebuffer blit. Rotated scanout samples
the shared texture directly with a cached shader, quad and sampler, without an
intermediate image or CPU readback. The 2D presentation path also supports the
transform. On readiness, an asynchronous read of panel size/current rotation
sets the host presentation without changing Android settings. Host window sizing
fits the nearest monitor's work area while preserving aspect and guest resolution.

Focused tests passed: 52 coordinate checks covering all four rotations, corners,
centres and letterboxing; 180 control checks including boot synchronization for
all four states. The full shell compiled with `-Wall -Werror -O2`. All nine WinQ
modules were rebuilt because the geometry structure changed. The isolated link
retained the validated host-power and resource-error objects from the previous
candidate; the original QEMU build tree was not modified.

Live checks used `Habumi-Startup-Test` on the Qualcomm WGL/Zink path:

- Portrait 1600x2560 -> logical landscape 2560x1600 -> portrait rendered upright.
- Calendar icon, top-left menu and About Calendar clicks reached the intended
  targets; the final fit build also passed Calendar/menu and Android Home clicks.
- The final portrait window fit the desktop with Android navigation visible;
  landscape filled its correctly proportioned client area.
- A normal close/reopen passed; the guest reported rotation 0 after reboot and
  startup synchronized to it. Persisted rotation 1 across reboot was **not**
  observed, so that case has only focused control-test coverage.
- Final boot readiness was 37.641 seconds; captured crash/ANR buffers were empty
  at guest uptime 140.30 seconds, boot ID `6f516f0e-8e9a-4365-90b4-6aceb3872e64`.

Final development executable hashes:

| File | SHA-256 |
| --- | --- |
| Habumi.exe | C07300BBA212E4082CE2F10B0079BC59840BE8C7C68CD0391F78531CED00F41E |
| qemu-nostro.exe | CB6C0C7E7A26DBF4F1CDAB82A3D0F94B48265C450B84A4322F3BE3041F4BBEF0 |

Local build provenance, copied inputs and response files are under
`research/results/rotation-renderer-fit-20261015`; display evidence is in
`native-ui-display-fit-*.txt`, with health in `systemui-probe-rotation-final.txt`.
These artifacts belong to the private development checkout. Backups and updated
manifest hashes were retained in the isolated profile. The published release
and personal installation were not replaced.

Manual Rotate is the validated path. Automatic orientation changes requested by
apps are not tracked continuously. ANGLE and rotated 2D fallback have not been
visually validated, and mouse checks do not establish physical touchscreen
coverage. This is not a GPU overhead or battery comparison.
