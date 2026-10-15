# Isolated SystemUI null-list prototype

This is post-release development for the 20260927 `waydroid_arm64_only`
GApps image. It does not modify the published 0.3.0-rc.1 runtime or the
official system/vendor images.

## Target and control flow

The original SystemUI APK SHA-256 is
`2f8fe1b0d88a33b26d3c7ea09dedf56248002ec00b86ba8431ed257573da6960`.
Its AppOps initialization is inlined into `AppOpsControllerImpl.setListening`:
the result of `AppOpsManager.getPackagesForOps` is iterated without a null
check. A fresh-data boot has produced a null-pointer exception at this point.

The proposed Dex guard branches past only the initial list iteration when
the result is null. It continues into `startWatchingActive`,
`startWatchingNoted`, audio recording callbacks, sensor-privacy listeners and
broadcast registration. Returning from `setListening` would skip those
registrations and is not the proposed fix. The false/listener-cleanup branch
is preserved.

## Reproducible experimental build

`guest/scripts/build-systemui-null-probe.py` uses explicitly supplied original
APK, decoded primary-Dex tree, JDK, Android build tools and Apktool 3.0.3.
Apktool is pinned to SHA-256
`dbf930b076c6b9be08d57c449cacefc3bdd6b71ebd59b3066fc0e1f5b14f9423`.
`guest/probe/AssembleSystemUiDex.java` is the minimal adapter to its assembler
and disassembler APIs. Neither helper installs anything or modifies an input.

The original and candidate must use the existing AOSP platform test signer:
certificate SHA-256
`c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8`.
This publicly known test identity is needed to match the guest's existing
SystemUI identity; it is not a production trust boundary or Windows code-signing
identity. Key contents are not copied into build results or logged.

Validation gates are the complete primary class inventory, re-disassembly of
the original, the sole intended source-class change, semantic comparison after
assembly, explicit listener-continuation checks, a byte-identical Dex assembly
roundtrip, unchanged contents of every non-signature ZIP entry other than
`classes.dex`, ZIP alignment and APK signature verification. Normalization
ignores line-number directives and label spelling and treats explicit static
boolean `false` initializers as equivalent to their implicit Dex defaults.
Other semantic changes are refused. Output directories must be new; failed
build outputs are retained for inspection.

This reassembles the entire primary Dex, so its binary layout and debug
representation can change. It is not a matching Android source build. Successful
static gates alone do not prove ART verification, startup, UI operation or
freedom from future crashes. Guest testing must verify the loaded APK hash,
SystemUI's AppOps `Listening: true` state, process continuity and crash/ANR logs.

## Current validation status

The signed APK passed all static gates: 14,341 primary classes, a byte-identical
Dex assembly roundtrip and 4,835 preserved other ZIP entries. Candidate SHA-256:
`40f473924907f993d2eb73f81e6c31112eb06b411682edb0f319a6f21afcf888`.
Its primary Dex SHA-256 is
`ea0a09a81dd6766d3d8364691bb660a7e280f3e88b37a318e39274e09ad87b9f`.

The opt-in `WINQ_SYSTEMUI_NULL_PROBE_APK` requires a separate initramfs output.
The builder pins the candidate hash; guest startup pins both original and
candidate before overlaying `/system/system_ext/priv-app/SystemUI/SystemUI.apk`.
`/system_ext` resolves to that directory in this image. The default build does
not include the probe, and no official Android image is rewritten.

The combined SystemUI/OMX prototype booted with newly created test data and
reached shell readiness at 42.688 seconds. Its loaded APK hash matched the
candidate, AppOps reported `Listening: true`, and SystemUI/MediaProvider PIDs
remained 758/1419 from guest uptime 96.83 through 414.15 seconds. Captured crash
and ANR buffers were empty at the end. Three navigation cycles and three
simulated VM pause/resume cycles preserved core guest process identity. Normal
shutdown completed without forced termination (5.500 seconds observed).

The permission-free `org.habumi.probe.startup` test APK installed successfully
and passed twice with distinct run IDs. It enumerated 33 decoders, created,
configured, started, stopped and released an AVC decoder, and inserted, wrote,
read back and deleted its own MediaStore Downloads item. These are application
API checks, not actual video decode. The APK has a separate ephemeral test key,
no platform identity and no shared UID. Its recipe is
`guest/scripts/build-startup-smoke.ps1`; source and manifest hashes are in the
private build report. Result screenshots were inspected and showed PASS.

One fresh-data boot and this short workload do not establish long-term
reliability, all third-party apps, GApps login, video playback or the absence
of every background-service ANR. Broader validation and a matching Android
source rebuild remain outstanding. This candidate stays in the isolated test
profile and is not part of the published release.
