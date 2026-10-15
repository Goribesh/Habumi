# Android startup readiness and the first-boot SystemUI crash

This is development work after the published 0.3.0-rc.1 tag. The release
artifacts have not been replaced. The personal installation is separate from
the new-data test profile used here.

## Shell change

The old asynchronous startup check accepted `sys.boot_completed=1` alone.
That property can remain set while Android's UI process is absent or restarts.
The new check requires boot completion and positive, distinct PIDs for
`surfaceflinger` and `com.android.systemui`. Both PIDs must match a second
sample taken at least one second later. Missing, changed, overflowing or extra
PIDs, truncated output and failed queries do not enable the ready state.

The one-second gap is driven by the existing UI timer; no ADB client remains
alive during the gap and no sleep blocks the UI thread. The complete request
retains its 20-second deadline, optional reconnect, owned-client cancellation,
and the VM's existing retry spacing. There is one additional short ADB query
on a successful startup and no new polling after entering the ready state.

This is a process-liveness check over a short interval, not proof that every
Android service, application or rendered frame is healthy. It does not repair
the guest exception or detect every later crash.

Each PID must also occupy its own output line: an empty compositor line cannot
be filled by multiple SystemUI PIDs. The real ADB command and strict final
parser passed a final boot at 29.203 seconds and normal close in 4.516 seconds.
The SystemUI crash buffer was empty, while MediaProvider's service ANR recurred.

## Reproduced exception

A new 4 GiB data image with official 20260927 GApps/vendor images reproduced
the same SystemUI `AppOpsControllerImpl.setListening` null-list exception as
the release smoke test. PID 745 crashed, and SystemUI later recovered as PID
918. The candidate reached its new ready state at 68.687 seconds; the normal
close logged guest Power down and QEMU exiting on its own in 9.015 seconds.
These are single-run timings, not a performance comparison.

On the following boot the candidate reached ready at 37.141 seconds, with no
SystemUI entry in the collected crash buffer, and closed normally in 5.015
seconds. Event logs did record Google Play Services and MediaProvider ANRs;
these remain separate, unresolved guest issues. A ready UI process does not
establish that background services are healthy.

The corresponding upstream code fetches package operations and iterates the
returned list without checking for null. The integer-code overload of
`AppOpsManager.getPackagesForOps` forwards the service result directly; the
string-code overload normalizes null to an empty list. This is consistent
with the exception seen on fresh data, but the optimized APK's inlined stack
does not by itself prove which list was null.

Sources: [LineageOS AppOpsControllerImpl](https://github.com/LineageOS/android_frameworks_base/blob/3a5c19c7b673b0d5f1f811629750ae6d1526ce09/packages/SystemUI/src/com/android/systemui/appops/AppOpsControllerImpl.java),
[AppOpsManager](https://github.com/LineageOS/android_frameworks_base/blob/3a5c19c7b673b0d5f1f811629750ae6d1526ce09/core/java/android/app/AppOpsManager.java).

## Proposed guest fix and its limits

[`systemui-appops-null-list.patch`](../guest/patches/systemui-appops-null-list.patch)
returns from the initial active-operation fetch when its result is null. The
rest of listener registration continues; it does not change permission modes,
seed application history or hide crash logs.

The patch applies to framework revision
`3a5c19c7b673b0d5f1f811629750ae6d1526ce09`; the downloaded source file's SHA-256
is `03bf9f043672b90036497330fdfbe363aa0eec6e43bee234ac4bbd556f35ec39`.
That revision is a verified source target, not a recovered build manifest for
the shipped APK. The patch is **not built, installed, or an established crash
fix**. It needs a matching Waydroid/Lineage SystemUI build and a fresh-data
validation. The local ARM64 environment lacks the supported Linux x86_64
Android 13 build toolchain. No APK or installed system image was modified.

## Validation

- All 13 shell suites passed: 648 checks.
- The asynchronous ready-client suite passed (187 checks in the final run), including missing processes,
  PID changes between samples, failed confirmation, malformed/truncated output,
  cancellation in the gap, reconnect, deadline and owned-handle cleanup.
- The real VM state-machine test passed with fake owned clients: boot completion
  alone and a missing SystemUI do not leave the waiting state; cancellation
  during close or QEMU exit remains responsive.
- The full candidate shell compiled with `-Wall -Werror -O2`.
- A real fresh-data boot used the new quoted compound ADB command and entered
  ready only after its two samples. The SystemUI crash was preserved in logcat.
- The guest source patch passed an application check against the pinned file;
  this is not a SystemUI build or runtime test.

Raw logs stay in the private development results. No accounts or personal
Android data were used in these tests.
