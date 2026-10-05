# Release notes

## 0.2.4 — prepared candidate

The archive has been prepared and locally exercised. As of October 5, 2026,
0.2.4 has not been published as a GitHub release; the latest downloadable
release is [0.2.3](https://github.com/Goribesh/Habumi/releases/tag/0.2.3).
These notes describe the prepared candidate and the corresponding source fixes.

Abruptly terminating the shell could leave QEMU running, holding the clipboard
port and session logs. A detached ADB server could also inherit log handles and
prevent the next startup even after both Habumi and QEMU had exited.

The candidate addresses that chain:

- QEMU belongs to the shell's job object and is killed when that job closes.
- Clipboard sockets and long-lived log handles are made non-inheritable.
- Startup refuses a failed serial-log rotation rather than reading an old
  session as evidence of a healthy new boot.
- Android-readiness timeout messages distinguish missing ADB, continuing
  serial progress and a boot that has stopped making progress.

Recorded local validation includes consecutive boots of 27 and 22 seconds
with the same ADB server alive between them, and a packaged-binary boot of
28 seconds with clean shutdown. This does not establish new hardware coverage
or resolve the multi-vCPU limitation on Surface Pro 12".

Candidate archive: `Habumi-0.2.4.zip`.

```text
sha256 30943f7ba3f3f1cc20d2b6d60ab78ea86ff623adbc2b28ab2f7d360d1fdf4ae6
```

Documentation and source changes after packaging do not update an existing
archive. A future package must be checked against the sources it actually ships.

## 0.2.3

Enabled the Hyper-V synthetic-feature grant by default and retuned the boot
watchdog using external Surface Pro 12" measurements: 60 seconds of grace,
45 seconds of serial silence and a five-minute kernel-progress ceiling.
Diagnostic logging and a new boot animation were also included. Boot improved
substantially on the reporting hardware; intermittent multi-vCPU hangs remain.
