# A4 desktop integration

Local development, based on 0.3.0-rc.2. These additions are not part of the
published rc.2 archive. The personal installation and published comparison
package remain unchanged. Native UI acceptance is a separate gate.

## Apps and Windows shortcuts

More > Apps reads launcher activities and their Android labels on a worker.
The bundled dex helper is built from `guest/app-list/AppList.java` with
`guest/scripts/build-app-list.ps1`; its source, SDK, compiler and output hashes
are recorded. It runs on request through `app_process`, without installing a
service or APK. The list is bounded to 256 activities / 128 KiB. Refresh is
explicit; installed apps are not monitored in the background.

Open uses an asynchronous `am start -W` request after Android readiness.
Components are validated separately from display labels. No shell syntax is
accepted as an app identifier. A session change cancels pending launches and
discards stale list results. A bounded in-flight read may finish while Android
is shutting down; it cannot start another query after cancellation.

Create shortcut asks for a Windows folder and saves a named `.lnk`. It targets
the profile's top-level `Habumi.exe --app COMPONENT`, with the profile as its
working directory. The normal Vulkan preparation and readiness gate remain in
the path. An existing runtime receives a versioned, bounded request only when
its canonical profile root matches; an occupied request or another profile
is refused. Existing shortcuts are never overwritten. Moving a portable
installation requires recreating its absolute-path shortcuts.

## File export

More > Export Android file accepts one file below `/sdcard/` or
`/storage/emulated/0/`, then a new Windows destination. It requires the existing
ADB connection; it does not enable debugging automatically. The separate worker
checks remote SHA-256 and size before and after pulling, compares local bytes,
then publishes by a same-directory move. An existing destination is refused.
Failure removes only that operation's temporary file. Unicode, spaces and
quotes are supported. The initial limit is 2 GiB per file; folder export and a
graphical Android file browser are not included. Hide keeps the export running;
normal Habumi closure waits for it to finish.

## Manual software updates

More > Updates closes Android normally and opens a management panel. Check is
an explicit GitHub request; stable and prerelease channels are separate. No
background check or installation is scheduled. Install rechecks the selected
release, downloads into staging, verifies size/SHA-256 and the embedded complete
inventory, then applies software through a helper copied outside the install.
The launcher exits before its executable is replaced. Older packages lacking
an embedded inventory require the portable installation workflow.

The updater holds the same exclusive profile lock as the runtime and data
tools. It rejects links, archive traversal, duplicate/case-colliding paths,
unsupported entries, insufficient free space, downgrades and unsupported image
or data-schema migration. Config, keymaps, Android disks and logs are not update
targets. The package's config/keymaps are verified but not applied.

Transactions and verified originals remain under `.habumi-updates/<id>`.
A durable `software-update.lock` prevents normal startup during a partial
replacement. Recover resumes a rollback or verifies a completed transaction;
Rollback restores the latest retained matching version. Changed installed files
or changed backups cause a refusal with recovery files retained. Do not remove
the update directory while a marker exists. Keep external backups: retained
software versions do not replace Android data backups. Power-loss and broader
hardware validation belong to A5; fixtures simulate interruption only.

## Verification

- Full shell build and 15 suites: 746 counted checks plus the app request
  lifecycle/IPC suite. Parser: 44 checks, including UTF-8 labels and bounds.
- Native ShellLink target/arguments/CWD/icon, readable names, Unicode paths,
  no overwrite and exhausted temporary-name cleanup.
- Export: 24 integrity/error cases; 12 worker protocol cases. Real Android
  export: 65,539 binary bytes, SHA-256
  `6D2BC5995AFCE9F9DF7D385B3DC9345E9C02D6F2748DFD6CB5DCDF621FE1D589`.
- App-list helper returned 15 named launcher activities in the isolated guest.
- A real Windows Calculator shortcut resumed the app in 2.47 s, keeping the
  same QEMU process (34452). Receipt: `Builds/A4/warm-shortcut-launch.json`.
- Update: 30 fixture checks, including committed rollback, interruption
  recovery, incompatible migration, downgrade and a locked mid-apply file.
  Five external-worker rejection cases preserve the entire fixture profile.
- Existing data tools: profile backup/restore checks and 43 native-worker checks
  pass. The worker test needs a shared Windows temporary directory; its first
  sandbox-only run could not expose that directory to the child process.
- A real public Check verified rc.2's runtime size and SHA-256. Download/install
  of a future GitHub release is not implied by that check.
- A complete 356-entry candidate was applied, rolled back, and applied again
  to the isolated 16-GiB profile. All 353 software targets were verified.
  Configuration and the entire Android data image retained their SHA-256;
  data SHA-256: `3DB18C810507981019773E6F3919026BBC8C01BF1DF9F44288026AF3C293C7AB`.
  Five offline Windows PowerShell 5.1 cases also cover both release channels,
  binary checksum responses, duplicate checksums, wrong digests and wrong URLs.

The acceptance package is `Packages/Habumi-A4-20261102-acceptance` under the
workspace. Its archive SHA-256 is
`F571F765D3C7D44EC20F669AA5680E704A3FAA2533033A7FBD767B1B080D75EF`.
It contains the embedded inventory and working-source input hashes. It is an
unpublished A4 candidate, not a replacement for the public rc.2 archive.

Evidence lives under `Habumi-Workspace/Builds/Checks/A4-*` and the A4 package
directory. Live app-launch evidence is recorded in the development checkpoint
after execution. Manual acceptance of the new
dialogs remains open until the user confirms it.
