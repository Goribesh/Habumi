# A2 offline data growth

This extends the [data foundation](A2-DATA-FOUNDATION.md). It is development
functionality, not a published release or completion of A2's native data UI.

## Contract

`Manage-Data.ps1 -Operation Grow -Image data-gapps.img -SizeGiB 16` explicitly
selects the GApps disk; `data.img` selects the other variant. The profile must
be closed. Only growth to an integer 4-16 GiB is accepted; the raw ext4 filesystem
must occupy the existing image. Partitioned disks and shrink requests are refused.

The operation holds the same exclusive profile guard as startup/backup/restore.
It reserves space conservatively for the complete requested image plus 16 MiB,
copies and hashes the original into `maintenance/<transaction>/new`, extends
only that copy and starts a dedicated maintenance guest. There is no WSL runtime
dependency, Android boot, GPU, network, ADB server or interactive shell in this
guest. QEMU has one CPU, 512 MiB and only the staged disk. Its process has no
console window; stdout/stderr drain asynchronously, with a bounded timeout.

The init script runs offline `e2fsck -f -p`, `resize2fs`, `e2fsck -f -n` and sync,
then emits a versioned completion marker containing the transaction nonce and
powers off. A zero exit code without the matching marker is insufficient.
Before publication, the host checks full filesystem/image size, UUID, block size
and all three ext4 feature words. Journal-recovery and orphan-present are
transient state bits, recorded separately and required to be clear afterward.
It hashes the new image and rechecks the original, including exclusive access.

Publication reuses the existing restore journal/marker and rollback code, with
`Operation=Grow` and a single image entry. The original is moved into `original`,
the verified replacement is moved into place, then the transaction is committed.
An interrupted replacement blocks startup until `Recover`. A failure during
staging needs no rollback: the original was never passed to QEMU. Partial output,
serial/QEMU logs, `growth.json` and old disks are retained for inspection.
No automatic deletion/reclaim or physical power-loss guarantee is introduced.

## Build inputs

Run `guest/scripts/build-maintenance-initramfs.sh SOURCE_ARCHIVE NEW_OUTPUT` on
an aarch64 Linux development builder. It verifies the pinned e2fsprogs archive
and static BusyBox before compilation. The source archive is
[e2fsprogs 1.47.2](https://mirrors.edge.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.2/),
SHA-256 `08242e64ca0e8194d9c1caad49762b19209a06318199b63ce74ae4ef2d74e63c`,
as listed in the upstream signed checksum document. Signature authentication
was not performed in this session; the pinned SHA was checked.

BusyBox is Ubuntu `busybox-static` `1:1.37.0-7ubuntu1`, aarch64 static binary
SHA-256 `c2f279d1d5640a0f327890d41cad594c0f059f3fed3f96dd72fdcc4f5e18ec02`.
The builder records compiler, libc, input script hashes, source patch and image
hash. It uses bundled UUID/blkid libraries and builds only the needed static
tools. The explicit patch makes two read-only parsing pointers const and expands
the optional e2fsck logfile `%u` to `root` in this root-only appliance, removing
glibc NSS loading from the static binary. Filesystem algorithms are unchanged.
Build warnings fail the recipe. Fixed archive metadata helps reconstruction;
bit-identical output across different compilers/libc is not claimed.

Pass the output as `-MaintenanceBuildRoot` to
`app/scripts/prepare-development-baseline.ps1`. Packaging verifies the receipt,
image/source/patch hashes and both current script inputs. The package contains
the image and receipt, e2fsprogs source archive/NOTICE/patch and build/init scripts.
The complete package inventory hashes every file, with runtime critical hashes
for the image/receipt. BusyBox source/license inventory completion remains part
of the existing A5 redistribution gate; no new release is published here.

## Verification

Focused suites passed **56 data, 40 growth and 25 recovery checks** (121 total).
The native child probe tests the actual ProcessStartInfo path without a VM:
no console HWND, both pipes larger than a pipe buffer, exit failure, missing or
wrong nonce, and timeout termination. Other checks cover tools/hash mismatch,
invalid geometry, retained feature identity, unclean final state, busy profile,
equal-size/shrink/over-limit requests, staged guest failure and interruption
between moving the original and installing the replacement. Existing shell
sources were unchanged; the complete shell/launcher compiled in the final package.

The isolated `Profiles/Habumi-A2-Data-Test` GApps disk grew from 4,294,967,296 to
17,179,869,184 bytes in **234.530 seconds**, including copying and hashes. Both
offline fsck passes, exact filesystem/image geometry and UUID/capability checks
passed. UUID remains `26-0D-3E-48-2A-13-41-8D-AC-CC-A5-EF-6A-99-03-09`.
Configuration bytes stayed unchanged. The retained original matches the recorded
pre-operation identity. Transaction: `a0a9a080ba98427796cf06ea3201b190`.

The proof file `/data/local/tmp/habumi-a2-proof.txt` matched before growth,
after offline extraction and again after Android boot:
`d9db90bc59f9a33694710c946ca38ffa0cf2960ec303ad275c16b12a5aa01aa7`.
Android reached readiness in **38.688 seconds**, with a ready compositor/SystemUI/
system-server snapshot. The guest reported 16,425,940 KiB filesystem capacity
(14,696,492 KiB available at that observation). Its normal shutdown took
**7.226 seconds**; the disposable profile is closed.

The first sandboxed run was denied WHPX access and left only a staging copy.
The next run successfully resized but the host rejected the cleared ext4
`orphan_present` bit as a feature change. Reviewing the pinned ext4 header/checker
identified it as transient state; the final implementation requires both state
bits to be clear rather than treating their removal as a capability change.
Neither rejected run replaced the original. Their logs and staged images remain.

The accepted development package is
`Packages/Habumi-A2-Growth-Accepted-20261019/distribution` under
`Desktop/Habumi-Workspace`; all **345 ZIP entries** were reopened and checked.
Explicit forward-slash entry names fix the .NET Framework ZIP path difference;
the accepted package was built/verified using Windows PowerShell 5.1. Previous
Growth/Ready/Verified/Final candidate directories are superseded.

| Artifact | SHA-256 |
|---|---|
| ZIP | `1133FD5F2BB241B225C3782CED74750CA63898AAF2F20071661EB3193FF88B97` |
| Maintenance initramfs | `DBABE4F0075604E5E7F0FC67A1D2A72639877E12E1907C1DBFFDE39153A40401` |
| Shell | `00FAD29874322A07F8A3606E159BC5AC2C1897163A930464A60620F508FF01AC` |
| Growth script | `B572A47328BB1FF74094EC0ED6515AA3A83AB3DDBA066C09472DA5EFF9613B4C` |

Evidence: `Builds/Checks/a2-growth-live` includes the transaction, guest logs,
offline proof extraction, guest proof/capacity, boot health and test reports.
The compact profile received only software/tools/metadata from the accepted
package, retaining previous software in `before-a2-growth`; configuration and
data-image metadata were preserved. The personal installation and published
releases remain unchanged. Native data controls and fuller diagnostics remain
the next A2 work; no broad hardware, physical power-loss or new GPU claim is made.
The compact profile reopened in **39.218 seconds**, with ready guest health.
An actual Grow command while it was running was refused by the profile guard
before reading or staging data. It is left open with the accepted software.
