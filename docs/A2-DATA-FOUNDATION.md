# A2 data protection foundation - 19 October 2026

This is the first A2 milestone, not completion of the package. The development
runtime now includes offline backup, staged restore, recovery and diagnostic
tools. The later [offline growth milestone](A2-DATA-GROWTH.md) adds the maintenance
guest; graphical data controls remain pending. Nothing has been published.

## Operation and recovery contract

- Runtime startup and maintenance hold the same `.habumi-profile.lock` file with
  exclusive Windows sharing. Ownership is an open handle, not file existence;
  process exit releases it. Startup acquires it before downloads or disk access.
- An interrupted replacement leaves `storage-maintenance.lock`, blocking startup
  until recovery. `runtime/bin/Habumi.exe --storage-status` returns 0 available,
  2 busy, or 3 recovery required, without opening a VM.
- `Manage-Data.ps1` provides Backup, Restore, Recover and Diagnostics. It uses
  inbox Windows PowerShell/.NET and needs no WSL. See the packaged README for
  commands. Operations are offline and existing destinations are refused.
- Backup includes both data images if present, exact configuration bytes and all
  keymap files. Schema/version metadata and hashes identify reference images;
  redistributable system images are not duplicated in the payload.
- A backup is published by directory rename only after all copies and hashes
  pass. Failed `.partial-*` output remains unpublished for inspection. Space
  checks conservatively count full logical file sizes, plus a reserve.
- Restore rejects incomplete/corrupt manifests, duplicate or escaping paths,
  links, file/directory topology conflicts and mismatched installed Android
  reference images. It verifies a staging
  copy before replacements and exclusively probes current files before mutation.
- A durable journal records the transaction, source manifest hash and original
  hashes. Previous files remain under `maintenance/<id>/original`. Recovery
  verifies all originals before rollback; a completed transaction is verified
  and finalized. Corrupt recovery evidence leaves startup blocked for inspection.
- Diagnostics writes a local directory containing version/reference metadata
  and up to 16 recent log tails, each capped at 4 MiB. It contains no data-image
  payload and uploads nothing. Full driver/config presentation remains pending.

These are file-level operations, not a filesystem resize or a generic Android
version migration. Retained original and partial directories consume disk space;
automatic cleanup/reclaim remains G28 work.

## Verification

All 13 existing shell suites (648 checks), the full shell build and archive
verification passed. Data tests passed 56 checks, including exact configuration
and both fake data images, nested keymaps, previous-file preservation, malformed
backups, reference mismatch, C/PowerShell cross-process exclusion, runtime status
codes and the packaged wrapper's default root. Another 21 checks covered
interrupted/partially recovered/committed restores, corrupt originals, real
sharing violations and insufficient-space preflight. The space check did not
physically fill the drive; no power-loss durability experiment is claimed.

A fresh isolated GApps profile used an independent 4 GiB data image. While it
ran, maintenance was refused. After normal shutdown, the tool backed up the
image, configuration and keymap (4,294,967,858 bytes total). A subsequent boot
modified a guest file. Restore reproduced every payload SHA-256 before boot;
Android then reached readiness and the guest file recovered its original hash:
`d9db90bc59f9a33694710c946ca38ffa0cf2960ec303ad275c16b12a5aa01aa7`.
Readiness was 61.218 seconds on fresh setup, 29.563 on the modification boot and
37.094 after restore. These are acceptance observations, not performance claims.

Evidence: `Habumi-Workspace/Builds/Checks/a2-live`, including backup manifest,
before/modified/restored proof hashes, boot logs and guest-health snapshots.
The disposable profile is `Habumi-Workspace/Profiles/Habumi-A2-Data-Test`.
Its three normal shutdowns took 5.594, 5.140 and 5.093 seconds.

The current compact profile `Desktop/Habumi-A1-Test` received the verified shell,
bootstrap, tools and manifests, with old software retained in
`before-a2-data-tools`. Configuration hashes were unchanged during installation;
keymaps and data images were not replaced. The former standalone bootstrap
receipt was archived because the release manifest now covers that executable.
Validation passed, the profile reopened in 37.157 seconds, and a ready health
snapshot plus storage status 2 confirmed the live guard. It is left open; the
disposable A2 profile is closed.

The final development package is under
`Habumi-Workspace/Packages/Habumi-A2-Data-Ready-20261019/distribution`.
All **337 entries** were reopened and verified. Provenance records 51 shell
source inputs plus the bootstrap and two data-tool hashes, matching current
source. Earlier `Habumi-A2-Data-*` candidates are superseded by this one.

| Artifact | SHA-256 |
|---|---|
| ZIP | `8BFCD9124A4A3C98DB73F68D7A268D6828D2A93BAA0102B4B54C15E029996A52` |
| Shell | `E668B654C22D1DF34E09E2C243143D75C376CDCF0CB850E48196B51C1CF170EB` |
| Data module | `BB3BCB484144CB6763F93B4CFE84D57DF68600FC89B12C7C2D9F2EC709447D7A` |

The package retains the A1 development-window identifier and adds
`OfflineDataTools=true`; this does not claim A2 completion or distribution.
The personal installation and published releases remain comparison references.
The live disk cycle used the preceding Verified candidate; the final Ready
package adds the tested early topology rejection with no change to the successful
restore path, and was installed with another normal compact-profile restart.
The final reopen reached readiness in 38.641 seconds, with a ready guest-health
snapshot and storage status 2 confirming the active guard.

## Follow-up A2 work

The [growth milestone](A2-DATA-GROWTH.md) records the static ARM64 tools, source
recipe, staged 4-to-16-GiB growth and recovery integration. Native management
controls and fuller diagnostic presentation remain. WSL is a development build
dependency, not an end-user maintenance requirement.
