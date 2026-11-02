# Habumi 0.3.0-rc.2 validation and build record

Recorded 2 November 2026. This prerelease delivers the compact native window,
A2 data tools and A3 settings/display actions. It does not complete A4 or A5.
Install in a new directory and retain older installations/data separately.

## Completed checks

- The shell builds with `-Wall -Werror -O2`; 13 suites pass 702 checks.
  The focused A3 recipe passes 19 text-edit, 25 atomic-save and 13 fullscreen
  checks. Screenshot tests cover Unicode/spaced paths, existing files, malformed
  PNG, ADB errors, cancellation, timeout and oversized data.
- The launcher probe verifies the GUI subsystem, no child console, Unicode/space
  paths, working directory, compact-window mode, validation and exit propagation.
- A2 acceptance on 22 October used TCG-only 4-to-16-GiB growth, real backup and
  restore, a preserved proof-file hash and user-confirmed native data-panel
  handoff/restart. Originals remain available. See [A2 evidence](A2-NATIVE-DATA-UI.md).
- A3 acceptance on 2 November includes user-confirmed fullscreen entry/exit,
  screenshot capture, rotation and touch/physical keyboard in both orientations.
  Settings saved 90 Hz; the next launch applied 90 Hz and reached readiness in
  36.000 s. Normal closes took 5.547 s before that launch and 4.500 s afterward.
  The native save routine restored 120 Hz offline, preserving the unknown key
  and comment. See [A3 evidence](A3-SETTINGS-DISPLAY.md).
- The first normal-release-recipe candidate verifies all 347 archive entries
  against SHA-256 and length. Its runtime archive hash is
  `B016F52B9E173085C41EB9BEB16E3A20E20B61CD0B1922607329D6634005E037`.
  This identifies the tested candidate, not the final metadata/documentation
  package. Final asset hashes are supplied separately in `SHA256SUMS.txt`.
- The final 347-entry runtime archive is
  `C75C3A5920345C938968798A4BA718CA9842FDF6FE1BFC9504F3C3F66C3D9C92`.
  Its binaries/manifest were installed into the clean test profile and checked
  against the final inventory. The final packaged reboot reached readiness in
  28.516 s and shut down normally in 4.500 s. The profile was left closed.
- The extracted candidate created a new 4-GiB data image. Pinned official
  read-only GApps/vendor images were reused; no existing writable Android data
  was copied. Readiness took 51.578 s, the 1600x2560 launcher/app drawer was
  readable, density 426 applied, and navigation completed. Normal close took
  5.500 s. No FATAL exception, RCU-stall or kernel-panic signature was found in
  this run. These boot times are observations, not a performance comparison.
- Android's deprecated `cmd gpu vkjson` enumerated one device. Its device
  properties were serialized as `{}`, so this check does not establish the
  device API version/name. SurfaceFlinger identified the Adreno-backed Zink
  path. A declared loader version alone is not GPU validation.

Evidence is retained outside the repository under
`Habumi-Workspace/Builds/Checks/A3-20261102` and `RC2-20261102`. The release
folder contains the final package inventories and validation record.

## Component provenance

| Component | Input |
|---|---|
| QEMU | Clean isolated 11.0.3 build, SHA-256 `4FC0695D5428E29F25F714953884F675D6C354286F107D7DDA0028AFC46742DF`; WHPX runtime and TCG maintenance available |
| QEMU source | Full build-tree snapshot `400B71F60FC91BE38B678FE13819BDEAD647101AD517C14B1108902D54D77368`; all 19 WinQ/protocol overlay hashes checked against the compiled-input receipt |
| Native renderer | Unchanged rc.1 binary `2AA9097D05B954ED6A509666C8BEC31BDE6426EAB5224C0F441CDCCC8328C091`; reused without recompilation, with its existing corresponding source |
| Host Mesa | Pinned 26.2.4 binary `36C6BEACBD7059F088CB15FC05E460C61BF27A3981AF59DB1233A7E079ED8DEA` |
| Kernel | Pinned 6.18.35 binary `BA6DB93AF147EA5435B8EBD7E47E5086E4E7727924BE0159B3D14C4A7912C1A0`; existing full patched source retained |
| Initramfs | Development image `C2B16486DC18115CF489C2A2430650A4A8B47CCF38497FECEB9E150C2CD3F937`, including the OMX/SystemUI compatibility probes |
| Maintenance | Image `DBABE4F0075604E5E7F0FC67A1D2A72639877E12E1907C1DBFFDE39153A40401`; e2fsprogs 1.47.2, TCG/single CPU/512 MiB |

The maintenance source offer includes e2fsprogs source/patch, the initramfs
recipe, Ubuntu BusyBox `1:1.37.0-7ubuntu1` source and local build-host glibc
`2.43-2ubuntu2.3` source. Package metadata and dpkg history show that libc version
installed on 10 August, before the 19 October build, and still present on
2 November. Source-file sizes/SHA-256 match their `.dsc` manifests. Detached
signatures are included without claiming cryptographic signature verification.
The receipts record actual download locations, including a retained Ubuntu mirror.

## Rebuilding and packaging

The source offer includes the project at the release commit, full patched QEMU
and Linux snapshots, renderer source, and maintenance source inputs. It does not
depend on manually edited files in an older external QEMU checkout.

Use MSYS2 CLANGARM64 for Windows builds. Build the supplied QEMU source in a
separate output directory using `qemu/scripts/build-development-baseline.sh`;
the packaged receipt records compiler/packages and source hashes. The older
`build-development-baseline.ps1` additionally supports reconstruction from the
pinned rc.1 base archive plus the current project overlays. Preserve TCG support.

For the unchanged renderer, `app/scripts/prepare-pinned-renderer.ps1` verifies
the pinned published runtime/source archives and produces an explicit reuse
receipt. A fresh renderer build can instead use `build-native-renderer.ps1`.
`prepare-native-release.ps1` compiles the launcher/shell, verifies the renderer
and QEMU receipts, includes maintenance tooling and packages only its allowlist.
`prepare-release-sources.ps1` assembles the corresponding-source offer and
checks all 19 QEMU overlays and every archived source file. Generated manifests
identify the exact project commit; toolchain/timestamp-independent binary output
is not promised. Maintenance rebuilds use `guest/scripts/build-maintenance-initramfs.sh`
in the recorded ARM64 Ubuntu environment; end users do not require WSL.

## Remaining limits

- [Issue #3](ISSUE-3-BOOT-TRIAGE.md) remains open for the reported Surface Pro 12
  slowdown/stall. The shell recovery correction is not a kernel/WHPX fix.
- The original hardware-watchdog restart trigger is unconfirmed. TCG removes
  WHPX from maintenance; no general host stability guarantee follows.
- Other hardware, multiple physical monitors, repeated overnight standby and
  hibernation, physical power-loss recovery, Google sign-in, media coverage and
  bidirectional variant switching remain outside this acceptance.
- First-run download dialogs were not driven: verified official image files
  were prepared before the clean-data boot. No new benchmark or energy claim
  is derived from the UI/data checks.
- Settings label launch requests explicitly; they do not query current applied
  GPU settings. Screenshot destination is fixed to Pictures/Habumi. App-requested
  automatic orientation, broader graphics fallback and A4 updates remain open.
- Binaries are unsigned. The complete third-party copyright/dependency audit
  remains open; consult the runtime inventory and notices. This release does
  not mark signing or A5 complete and remains a prerelease.
