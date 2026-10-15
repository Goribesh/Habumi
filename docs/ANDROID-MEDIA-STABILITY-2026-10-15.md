# Android media startup: missing OMX service

Development after 0.3.0-rc.1, on the isolated startup test profile. The
published release and official Android images remain unchanged.

## Diagnosis

The 20260927 `waydroid_arm64_only` GApps image repeatedly recorded a
MediaProvider ANR during `ExternalStorageServiceImpl` startup. Exported
Android DropBox traces show its main thread blocked at
`BpMediaPlayerService::getCodecList`, called by
`TranscodeHelperImpl.hasHDRPlugin` during provider initialization. It is not
waiting inside a FUSE read in that trace.

The media server repeatedly waits for
`android.hardware.media.omx@1.0::IOmxStore/default`. The vendor manifest
declares it, but the image has no corresponding HAL executable/service.
`dumpsys -t 5 media.player` times out and `/storage/emulated/0` is not mounted.
The kernel does have FUSE support; changing `/dev/fuse` permissions would not
address the observed codec-list wait.

The upstream [Waydroid report #2397](https://github.com/waydroid/waydroid/issues/2397)
describes the same chain. Source inspection also shows
[OMX enumeration](https://github.com/LineageOS/android_frameworks_av/blob/lineage-20.0/media/libstagefright/OmxInfoBuilder.cpp)
using a blocking service lookup before its existing null check, and
[MediaCodecList](https://github.com/LineageOS/android_frameworks_av/blob/lineage-20.0/media/libstagefright/MediaCodecList.cpp)
invoking the OMX builder independently of the Codec2 setting. Changing the
Codec2 property alone is therefore not a demonstrated solution here.

## Experimental nonblocking lookup

`guest/scripts/prepare-omx-nonblocking-probe.py` accepts only the verified
original library SHA-256
`b343ab767e7511b7c3abd522184ff1db8bb4b2d0c4491ed8ac63ff63509e8a50`.
It validates ELF64/AArch64 program headers, both lookup thunks and their shared
helper branch before replacing the retry flag instruction at virtual address
`0x6243c`. This changes the lookup to use the same nonblocking flag as
`tryGetService`. Exactly one four-byte instruction changes; the input is never
modified. The output SHA-256 is
`9bdf01dfd15160da62a0f7175219a84d197df25b72e8941b37b36b8b311b226c`.

The initramfs builder includes it only with
`WINQ_OMX_NONBLOCKING_PROBE_LIB` and an explicit separate
`WINQ_INITRAMFS_OUTPUT`. Startup checks the original system library hash before
placing the candidate in the existing read-only system overlay. A different
Android image is refused. No system/vendor image is rewritten.

This is a binary-level experiment, not a rebuilt Android component or a new
release. It changes every call to this library's `IOmxStore::getService`, not
only the codec enumeration call. A source fix limited to enumeration and a
matching Android rebuild remain preferable for broader distribution.

## Observed result and limits

- The experimental boot loaded the expected candidate hash.
- `media.player` returned a nonempty Codec2 codec list within the bounded query.
- `/storage/emulated/0` mounted through FUSE; the Download directory was present.
- No new crash or ANR appeared in the collected buffers through guest uptime
  1324.00 seconds. SystemUI and MediaProvider PIDs stayed 645 and 1273 between
  uptime 88.25 and 1324.00 seconds on the same guest boot ID. This included idle
  observation, bounded codec probes and three Settings/Display/Recents/Home
  navigation cycles, not a representative third-party app workload.
- The NDK/API 33 probe created `c2.android.avc.decoder`, configured a 64x64
  byte-buffer AVC decoder, started, stopped and deleted it successfully. It
  supplies no compressed data: this verifies initialization, not video decode,
  playback, hardware acceleration or performance.

These observations do not establish all application compatibility, GApps
login, audio/video playback or long-term reliability. Raw traces and the
unmodified input library are retained privately; the probe scripts are the
reviewable recipe.

## Supporting host and navigation checks

`qemu/scripts/capture-guest-health.ps1` records a guest boot ID, uptime,
boot-completed property and the compositor, SystemUI and system-server PIDs.
Its seven offline cases distinguish continuity, reboot, service restart,
missing data, non-finite uptime and uptime regression. Continuity is an
observation between snapshots, not a guarantee that no intervening fault occurred.

`test-development-navigation.ps1` completed three navigation cycles on the
dedicated Startup-Test profile. Before/after guest boot ID and all three PIDs
matched; the final resumed activity was the launcher. Home and Settings
screenshots were visually inspected and showed readable UI. The helper retains
raw log buffers without clearing them and does not declare all UI conformance.

`test-development-vm-pause.ps1` completed three bounded simulated pause/resume
cycles using the existing host-power command path. Each matched its pause and
resume log acknowledgements and retained guest/PID continuity. This does not
exercise Windows standby or hibernation delivery. Normal Android shutdown
after the tests completed without forced termination (4.500 seconds observed).

Separately, `app/scripts/build-download-test.ps1` passed valid-hash download,
wrong-hash cleanup, interrupted read and cancellation cases using the production
downloader with WinHTTP stubs and real BCrypt/filesystem operations. It does not
exercise real TLS, extraction, updates or a full first-install GUI flow.
