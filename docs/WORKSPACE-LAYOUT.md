# Local workspace layout

On 19 October 2026, 19 inactive Desktop directories were moved into
`Desktop/Habumi-Workspace`, reducing related top-level directories from 22 to 4.
No contents were deleted. Existing source checkouts and the current profile keep
their original paths so project bindings and the running ADB server stay valid.

| Desktop directory | Purpose |
|---|---|
| `habumi-pubblico` | Public source checkout |
| `AndroidRuntimeARM64` | Private source and personal installation |
| `Habumi-A1-Test` | Current compact development profile |
| `Habumi-Workspace` | All other development artifacts |

Inside `Habumi-Workspace`, use `Builds` for build trees and checks, `Packages` for
packaged candidates/releases, `Profiles` for older runtime profiles, and
`Archives` for previous experiments and screenshots. Put new outputs under these
directories rather than creating more Desktop siblings. Preserve existing
artifact names within their group.

Current inputs and outputs, relative to `Desktop/Habumi-Workspace`:

- QEMU baseline: `Builds/Habumi-A1-Qemu-20261019`.
- Pinned runtime ZIP: `Packages/Habumi-dist/0.3.0-rc.1-publish/Habumi-0.3.0-rc.1.zip`.
- Pinned initramfs: `Profiles/Habumi-Startup-Test/guest/images/android/initramfs.img`.
- Latest candidate package: `Packages/Habumi-A2-Growth-Accepted-20261019/distribution`.
- Maintenance guest build: `Builds/A2-Maintenance-20261019/output-verified`.
- Growth acceptance evidence: `Builds/Checks/a2-growth-live`.
- Older Vulkan comparison profile: `Profiles/Habumi-Native-Vulkan`.
- Compact window checks: `Builds/Checks/compact-window`.

The workspace's `desktop-moves-20261019.json` records every old/new absolute path.
Historical logs, source receipts and earlier documents retain their recorded
paths; apply this mapping when retrieving them. Moved generated build systems
may retain absolute paths and require reconfiguration before an incremental
build. Continue using clean build recipes for baseline reconstruction.

The Desktop shortcut **Habumi Compact (Test)** still targets the current profile.
**Habumi Native Vulkan (Test)** was updated to the moved comparison profile; its
previous shortcut is saved in `Archives/Desktop-shortcuts`. Both launcher
validations passed after relocation without starting Android. Every planned
source directory was absent and destination present after the moves.

Later on 19 October, a separate cleanup removed 17 superseded generated package
directories and six synthetic growth-test directories, releasing about 36.09
GiB. Their source snapshots, manifests and text logs were archived and hash
verified before removal. See the local `cleanup-20261019.json` and
`Archives/obsolete-receipts-20261019.zip` for the exact inventory. Historical
paths for these removed outputs now identify archived evidence, not runnable
packages. No runtime profile or source checkout was deleted.

`Packages` retains `Habumi-dist`, `Habumi-A2-Growth-Accepted-20261019` and
`Habumi-A2-Data-UI-20261019`. The last is an unaccepted development candidate:
its real maintenance trial coincided with an unexpected host restart. The
interrupted profile, staged disk and logs remain available for investigation;
further VM trials are stopped.
