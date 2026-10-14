# Measuring GPU overhead and battery use

Habumi runs ARM64 CPU code through WHPX. Its current GPU path is Android
GLES/Gallium -> virtio/virgl -> host OpenGL/Mesa -> D3D12 -> Adreno. D3D12
commands and shaders ultimately execute on the physical GPU, but the guest
does not submit directly to the Windows GPU driver.

The sampler reads Windows GPU engine running-time counters, host process CPU
time and battery telemetry. It does not stop applications, alter settings or
enable expensive per-command graphics tracing.

From the repository root, in PowerShell:

```powershell
./qemu/scripts/measure-host-overhead.ps1 -Label idle -DurationSeconds 60 -IntervalSeconds 60
```

Keep Android on its home screen without input during that run. Results are
saved under `guest/logs/host-overhead/<timestamp>-<label>/`: `summary.json`,
per-interval CSV files, raw `snapshots.json`, metadata, source copies and the
runtime config from the sampler's source tree. Check recorded live executable
paths/arguments when running the sampler from another checkout or with multiple
installations.
The GPU mean is `100 * delta RunningTime / delta Timestamp_Sys100NS`.
GPU engines are separate; adding 3D, compute and copy percentages would not
produce a valid whole-GPU percentage. Missing/reset instances are excluded;
check coverage and raw snapshots before comparing runs.

CPU `CorePercent` uses one logical core as 100%; `MachinePercent` divides by
the host logical processor count. These are host process accounting values,
not a measure of all guest execution attributed to Hyper-V. The sampler's own
CPU time is included so its disturbance is visible. Use sparse sampling for
energy runs, and compare identical sampling schedules.

## Battery protocol

1. Disconnect AC, fix screen brightness and keep the Windows power mode,
   panel refresh, window size and other applications unchanged.
2. Let boot activity finish. Record at least 180 seconds per condition with
   `-DurationSeconds 180 -IntervalSeconds 30`.
3. Compare VM stopped, Android idle, then a repeatable app workload. Alternate
   A/B order and repeat each condition at least three times.
4. Change one parameter at a time: refresh rate, render resolution, then
   implementation changes. Compare energy at equivalent work and frame rate,
   and verify frame times and input latency alongside energy.

Battery rate belongs to the whole tablet. `BatteryTelemetry.cs` uses read-only
Win32 battery queries and verifies `BATTERY_INFORMATION.Capabilities` before
converting rates. Relative capacity units are possible. Only a negative signed
rate, absolute units and a discharging battery with AC and charging flags clear
produce `DischargeWatts`; zero and the unknown rate (`-2147483648`) produce null.
The conversion is `-Rate / 1000`. Query failures remain unavailable readings.
This bypasses the `BatteryStaticData` WMI query, which fails on the measured
Surface, rather than assuming units from that failure.

The energy summary integrates valid endpoint pairs using a trapezoidal estimate
and reports coverage. It is a coarse estimate from battery telemetry. Keep the
power source constant: a transition between sparse samples may not be detected.
Use an external power meter when finer measurements are necessary.

The VM's incremental idle power is `P(Android idle) - P(VM stopped)` under
matched conditions. GPU utilization alone does not give watts. Under load,
compare joules per completed task or per frame; higher utilization can mean
more useful work rather than worse efficiency.

## What counts as translation overhead

For narrower host-call timing and the fixed glmark2 protocol, see
[Presentation profiling](../docs/NATIVE-VULKAN.md). Presentation wall time
must not be interpreted as GPU execution time. ARM WHPX statistic/PC probes
are now disabled by default; enable `WINQ_WHPX_STAT=1` for diagnostic runs.

QEMU's GPU time includes app rendering, Android composition and host
presentation as well as any extra work from the adapters. It is not all
overhead. A percentage for the adapters requires the same scene through a
native host path at matched resolution, frame rate, shaders and visual output.
Use a process-attributed GPU/CPU trace to separate command submission,
synchronization, resource copies and presentation. Existing GL timer-query
measurements have known coverage problems and are not an authoritative GPU
occupancy source.

Two current code facts guide the next experiments:

- Static guest scanout is not redrawn on every host tick. Keep this behavior.
- The safety timer uses 5 ms during activity and 30 ms after 250 ms without
  input, command-pipe traffic or new frames. Window events still wake QEMU's
  bottom half immediately; input, resize/expose events and new GL/2D frames
  restore the active cadence. A first pipe command after idle may wait up to
  30 ms. This reduces this timer's wakeups, not all QEMU/Android wakeups.
  `WINQ_IDLE_ADAPTIVE=0` retains the fixed timer for A/B comparisons; an
  explicit `WINQ_PERIODO_MS` also keeps its chosen interval fixed.
  `WINQ_IDLE_DIAG=1` reports actual timer ticks per elapsed window. Keep this
  diagnostic off during battery measurements.

`hz=120`, `guest_scale=yes` and `gl_flush_wait=no` remain the regular settings.
`battery_hz=60` selects 60 Hz when starting a VM on battery. Missing/zero
`battery_hz` preserves the regular setting. Values are 0 or 24..240; a lower
explicit `hz` wins. With `hz=0`, a nonzero battery setting replaces automatic
refresh while on battery. Failed/unknown power-source reads and machines
without a battery retain the regular setting. The source is checked once per
VM start: unplugging a running 120 Hz VM does not switch it to 60 Hz. Check
the shell's `display refresh at startup` log and QEMU's announced refresh.
Rendering fewer frames/pixels is a separate quality tradeoff, not proof of a
more direct graphics implementation. Leave the WGL wait fix enabled unless
a controlled correctness check requires otherwise.

The 5 October synthetic frontend A/B measured about 181-182 fixed safety
ticks/s versus 32.7 adaptive ticks/s in a paused VM without guest disks.
This establishes fewer timer callbacks. Short CPU samples varied and do not
establish CPU or energy savings. Android responsiveness, game frame pacing
and whole-device watts still need matched runs at the next safe restart.

References: [Microsoft GPU accounting](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/),
[battery status units](https://learn.microsoft.com/en-us/windows/win32/power/battery-status-str),
[Mesa D3D12 driver](https://docs.mesa3d.org/drivers/d3d12.html).
