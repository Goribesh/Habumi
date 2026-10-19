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
- Latest candidate package: `Packages/Habumi-Console-Fix-20261019/distribution`.
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
