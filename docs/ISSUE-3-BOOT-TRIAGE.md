# Issue #3: Surface Pro 12 boot triage

Issue [#3](https://github.com/Goribesh/Habumi/issues/3), opened by rexchan7 on
24 October 2026, reports slow or stalled Android boot on a Surface Pro 12 using
the vanilla 0.3.0-rc.1 release.

## Reported observations

The reporter identifies the machine as Snapdragon X with Adreno X1-45. With
six and three vCPUs, boot reportedly stops at `chr_dev_init` for more than 290
seconds; with one vCPU, it continues. Setting
`transparent_hugepage=never` reportedly reduces the zygote interval from 647
seconds to 315 seconds, but boot remains very slow. These figures and hardware
details are user-reported. No raw serial or shell logs were attached, and this
machine has not been reproduced locally.

## What the repository establishes

The rc.1 launcher retries by halving the configured vCPU count when a boot
produces no serial lines, and it also degrades after repeated hangs at the same
line count. Equal line counts do not establish equal stopping points. Its
quiet-boot watchdog judges serial progress. The older report that
`vcpu=1` was reliable concerned a different machine and does not establish a
guarantee here.

The Hyper-V synthetic-feature grant is already enabled in the shipped runtime.
The guest Hyper-V synthetic timer patch exists in the source tree but is not
shipped. This report alone does not establish grounds to enable that patch
universally.

## Development correction, 2 November 2026

The shell now compares the final complete serial message, stripping a printk
timestamp and initcall-debug offsets/PIDs. Two silence timeouts at the same
vCPU count and normalized message permit the fallback; the absolute line count
does not. Empty/truncated messages do not match. An immediate QEMU exit or a
live boot reaching the time ceiling does not justify lowering the CPU count.
No-output silence timeouts still permit halving. Launch resets the signature,
and incomplete lines wait for completion before becoming a comparison point.

This remains a recovery heuristic. It neither detects lost SGIs nor makes a
slow boot fast. The 60-second grace, 45-second silence, five-minute kernel
ceiling, retry budget and kernel command line are unchanged. With three
attempts, a nonempty repeated stop can still exhaust the budget at three vCPUs;
this change does not promise an automatic one-vCPU boot.

Validation: the 13 shell suites pass 702 checks, including 28 added checks for
normalization, differing line counts, different stop messages, CPU changes,
silence versus other exits, incomplete/overlong serial input and launch reset.
The complete development shell links with `-Wall -Werror`. No VM was launched,
no personal installation was modified, and no boot fix on the reported Surface
is established. The shell recovery correction is included in
[0.3.0-rc.2](https://github.com/Goribesh/Habumi/releases/tag/0.3.0-rc.2);
the underlying hardware issue remains open.

## Evidence needed

Please collect full serial and shell logs for each of these runs: six vCPUs,
one vCPU, and one vCPU with `transparent_hugepage=never`. Include the exact SoC,
Windows build, GPU driver version, host power state, image/package hashes, and
the Hyper-V feature-grant log. Preserve timestamps so serial progress and the
watchdog decision can be compared. Until these are available and reproduced,
the `chr_dev_init` stall and THP effect remain unconfirmed reports.
