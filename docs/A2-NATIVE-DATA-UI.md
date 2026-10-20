# A2 native offline data management

Implemented in the local development candidate on 20 October 2026. This is
implementation and targeted test evidence, not A2 runtime acceptance or a
published release.

## Behavior

The More menu's **Data management...** action requests normal Android shutdown,
then opens the native offline panel. `Habumi.exe --data` opens it directly;
a pending replacement-recovery marker also routes startup to this panel.
Backup, restore, recovery, grow-only capacity selection and local diagnostics
reuse the existing guarded storage operations. Backup and diagnostics suggest
a new directory under Documents. Restore explicitly confirms replacement and
retains previous files. Disk growth selects the actual data image and 8, 12 or
16 GiB; shrinking is unavailable.

A hidden inbox Windows PowerShell worker performs disk operations outside the
UI thread. Requests are JSON data, with a version and session nonce. Atomic
UTF-16 status messages show phases; success requires a matching final status
and zero child exit code. The final status is read after process exit, avoiding
a stale-progress race. Closing during work waits; errors retain the panel and
operation details. A per-profile guard and manager identity prevent competing
operations and duplicate managers. Temporary worker directories retain request,
status, result and output for diagnosis.

Diagnostics includes bounded Windows build, CPU, display-driver versions and
known configured settings. Unknown setting values and Android data-image bytes
are excluded. Applied settings are explicitly unavailable without runtime
evidence. Pending recovery permits diagnostic export but still blocks startup.

## Growth exclusion after the host restart

Growth now creates the same session-local named mutex object used by existing
Habumi shells, `Local\AndroidRuntimeGuscio.istanza`, and keeps its handle until
the operation ends. Another runtime or growth operation is refused before
staging. A process query also rejects existing `qemu-nostro.exe` and
`qemu-system-aarch64.exe` processes; a failed query fails closed. The shell now
refuses startup if its singleton guard cannot be created and reports a busy
startup/maintenance state when no runtime window is available.

This coordinates normal Habumi launches within the interactive session. A
manually launched QEMU or another Windows session starting after the process
query does not participate in that mutex protocol. Multi-session runtime
isolation remains outside this milestone. No unrelated process is terminated.

The 19 October trial started at 16:39:35 Rome time and coincided with an
unexpected host restart; the next boot was at 16:40:53. Kernel-Power 41 recorded
BugcheckCode 0. A follow-up read on 20 October also found Kernel-Power event
142: Windows reports that the system was unresponsive and a hardware watchdog
triggered the reset. This identifies the reset mechanism, not the original
cause. No Windows restart command was issued. The compact Android VM was
running when maintenance QEMU started.
The maintenance log ends during resize2fs on the staged 8-GiB copy. The active
image still has its original 4-GiB length. The staging image and logs remain
untouched; no completion or recovery is claimed. The new exclusion reduces
overlap risk but is not proof that the host-restart problem is fixed.

## Verification and remaining gates

On 20 October: all 13 shell suites (674 checks), 56 data checks, 25 recovery
checks, 40 growth contract/error checks, 28 diagnostic checks, 43 native-worker
checks and 14 runtime-gate checks passed. The gate tests include a separate
shell-like process, old/new mutex-name compatibility, query failure and an
injected orphan QEMU. Worker tests include Unicode/space paths, broken inherited
module paths, stale nonce, missing status, premature exit and success with a
nonzero process exit. These tests start no VM. Full shell and native launcher
compilation passed with warnings treated as errors. Launcher regressions passed
using a fake startup script; there was no new Android boot.

The rebuilt package has 347 verified ZIP entries. Workspace-relative paths:

- Candidate: `Packages/Habumi-A2-Data-UI-20261020/distribution`.
- ZIP SHA-256: `9E36999D16734159AD18F4F326127509C5EF036D4E67F6BCFE35C024F7FC653E`.
- Test reports: `Builds/Checks/a2-data-ui-20261020`.
- Host event evidence: `Builds/Checks/a2-data-ui-20261020/host-restart-events.json`.
- Interrupted profile: `Profiles/Habumi-A2-Native-Data-Test`.
- Interrupted transaction: `bea0333a3f174577a2be65ac1be0de8b`.

The earlier fake-profile GUI backup/restore passed on 19 October, including
byte-for-byte restoration and retained originals. Runtime-to-panel handoff,
panel-to-runtime restart, closing during real long-running work and real growth
with the new exclusion still require acceptance. Investigate the host restart
before further VM trials. The compact profile retains the previous accepted
software; the personal installation and published release were not updated.

## Follow-up: interrupted-maintenance diagnostics

Source follow-up to `9ebc0d3`, 20 October: diagnostics now inventories maintenance
directories independently of the recovery marker. A staged image with no valid
matching marker is labelled `OrphanStage`; this is diagnostic evidence, not an
instruction to delete it or repair live data. Images contribute only presence
and length. A corrupt, oversized or unsupported marker/journal produces a
bounded status; arbitrary metadata fields are excluded.

The active marker's transaction is prioritized among at most 16 summaries.
Metadata reads are capped at 64 KiB. Each transaction contributes at most the
last 256 KiB of `serial.log` and `qemu.log`, under `maintenance/<id>/` in the
export. Guest log tails remain capped at 4 MiB each, up to 16 files. Log reads
are explicitly bounded even if a source changes. Reparse paths are not followed.
The free-space preflight now reserves 80 MiB plus the existing 16-MiB margin.

Fifteen maintenance-diagnostic checks, 56 data checks, 43 worker checks and the
native quoting probe passed. The reparse test ran successfully, without skips.
Read-only export from the actual interrupted profile identified transaction
`bea0333a3f174577a2be65ac1be0de8b`, no marker, an 8-GiB orphan stage and its
18,301-byte serial log. The export contains zero image files. No recovery,
deletion or VM startup was performed. Evidence is under
`Builds/Checks/a2-maintenance-diagnostics-20261020`.

This follow-up is source-tested, not included in the earlier packaged ZIP
identified above. Runtime installations and release status are unchanged.

## Abrupt worker exit verification

Follow-up to `2fbe5e3`: `test-worker-interruption.ps1` starts hidden inbox
PowerShell children on tiny synthetic files. One exits immediately during
backup copying; its incomplete staging manifest remains unpublished and live
file hashes are unchanged. Another exits after restore flushes its recovery
marker and journal, before the first original-file move. The marker blocks
new operations. A fresh process recovers the journal, preserves all three live
file hashes, clears the marker and releases the profile guard. Unused staged
data remains inspectable. No production fault-injection hook was added.

All 29 checks passed, together with the 25 existing recovery checks covering
partially replaced files and growth journals. This simulates process exit,
not physical power loss. Reports are under
`Builds/Checks/a2-worker-interruption-20261020`. No GUI or VM was started.
The updated offline candidate is recorded in the development checkpoint;
runtime handoff and real growth acceptance remain open.

The full native build and all 347 ZIP entries passed for
`Packages/Habumi-A2-Offline-20261020/distribution`. ZIP SHA-256:
`92442C2F14697247D1CF4BC38A538B4E52060DFFD81926501CA343EEF4009685`.
It includes the maintenance-diagnostic follow-up, remains uninstalled and is
not a runtime-accepted or distributed release.
