# Console-free startup - 19 October 2026

Use the **root `Habumi.exe`**, or a shortcut to it. The `.cmd` entry point created
a command-prompt window before PowerShell could hide itself. The new native GUI
bootstrap starts the existing Vulkan preparation script with `CREATE_NO_WINDOW`,
an explicit Windows PowerShell path and the installation working directory.
The script retains its startup error dialog and log. No Vulkan preparation is
bypassed. The launcher waits and preserves the child exit code.

A1 packages build the bootstrap with single-window mode enabled by default;
ordinary native packages retain the separate-window default. `--single-window`
selects the integrated view, and `--validate` runs launcher validation without
starting Android. The compatibility `.cmd` files now start the GUI bootstrap and
return immediately; Windows can still briefly display a console when opening a
`.cmd` file. Double-clicking the GUI executable avoids that console entirely.

Both build modes passed the isolated launcher probe: GUI PE subsystem, no child
console, Unicode/space paths, correct working directory, mode and validation
forwarding, propagated exit code 23, and headless missing-script failure. The
probe uses a fake script and never starts QEMU. Full package compilation passed
with warnings as errors; all **335 archive entries** were reopened and verified.

The final package is under `Desktop/Habumi-NoConsole-Final-20261019/distribution`.
ZIP SHA-256: `9C27F2CC25529E85AFE00BCD1F384C70F38E562F08BF7C8016DE3872ABFDBFE7`.
GUI launcher SHA-256:
`FAC8EC45E1A9E7CDAE9730A3C247AE88B3B76756B5F54BDC1A3D155BF860B118`.
The release manifest hashes the bootstrap; development provenance additionally
records `app/release/launcher.c` as `LauncherInput`.

The isolated `Habumi-A1-Test` profile received only the bootstrap, compatibility
scripts and README, with old files in `before-no-console-launcher`. Its existing
shell, release manifest, provenance and Android data were preserved. A separate
`graphical-launcher.json` records the added bootstrap. Real `Habumi.exe --validate`
passed against that profile. The current Android session was not restarted;
normal cold startup through the new shortcut remains a user acceptance check.
The desktop shortcut **Habumi Compact (Test)** targets the root executable
directly. The personal installation and published releases were not changed.

## Follow-up: complete runtime chain

The first check covered the bootstrap alone. The user still reported a black
window while Android ran. Source inspection found that QEMU's console-subsystem
executable was launched with only `CREATE_SUSPENDED`. Runtime startup now also
uses `CREATE_NO_WINDOW`, while preserving the job assignment before resume and
the inherited stdout/stderr pipe. The PowerShell wrapper starts the GUI shell
with `UseShellExecute=false` and `CreateNoWindow=true`.

A native regression invokes the real `vm_lancia` with the test executable in
place of QEMU: it reports no console window, checks stderr is a pipe and verifies
the received log marker and child exit code. No guest runs in this test. VM
checks passed 126/126; all 13 shell suites passed (674 checks). Full package
compilation and all 337 archive entries passed. The startup script is now hashed
in both the release inventory and development provenance.

Installed into `Desktop/Habumi-A1-Test` with previous software retained in
`before-console-chain-fix`, preserving configuration/data. Real launcher
validation passed; Android reached readiness in 36.062 seconds, guest health was
ready and the native window displayed Home. The window inventory exposed only
Habumi for this profile. Console-host processes still exist in the process tree;
their count is not treated as proof of visible windows. Confirmation of the
specific user-reported window remains pending its title or user observation.

Evidence: `Habumi-Workspace/Builds/Checks/console-fix-20261019`. Package:
`Habumi-Workspace/Packages/Habumi-Console-Fix-20261019/distribution`.

| Artifact | SHA-256 |
|---|---|
| ZIP | `A9BA4AB3890F0CA176CFCCA1B1563B542F3D4B9AC755020A256F73E0499897BE` |
| Shell | `D1B98AFBB0F40A95FE30F8989A14852008FE8C5B01410F6EF5A0EBAC2C7B47E9` |
| Startup script | `DB057AB74E39105EAD964773D497FF5E60B6C05FF290751E86ABBC38BBFB723C` |
