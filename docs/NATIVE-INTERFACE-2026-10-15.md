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
control checks passed (133), readiness checks passed (187), and shutdown/VM
checks passed (100). The candidate is installed only in Habumi-Startup-Test,
with executable and manifest backups.

The new shell booted Android and displayed current readiness with all nine
accessible button names. Initial visual inspection confirmed the dark theme,
but a Windows Firewall permission prompt blocked further interaction. Keyboard,
rotation and compact-window visual checks remain pending until that prompt is
handled by the user. Light/high-contrast appearance and live theme switching
have not been visually verified. These are not broad hardware compatibility or
Windows standby guarantees.
