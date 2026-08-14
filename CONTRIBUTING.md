# Contributing to Habumi

Contributions are welcome. This document exists because several of this
project's rules are **not** the ones you would guess, and each of them is here
because breaking it cost real time.

## Before anything else: the language

Source comments and design documents are in **Italian**. The user interface and
the documents in this folder are in English. Translating the interior is planned
work — see the README — but it has not happened yet, so expect Italian when you
open a `.c` file.

Write new comments in the language of the file you are editing. A file half in
each is worse than either.

## Building

You need MSYS2 with the **CLANGARM64** toolchain, and a WSL distribution for the
initramfs — that build needs a real aarch64 Linux, and MSYS2's POSIX layer
reports `x86_64` even under CLANGARM64.

```bash
bash app/scripts/build-guscio.sh    # builds runtime/bin/Habumi.exe
bash app/scripts/test-guscio.sh     # 12 suites, no virtual machine needed
```

Both must be run from MSYS2 CLANGARM64, with an **absolute path** to the script.

**Green tests do not mean the product links.** `test-guscio.sh` compiles one
suite at a time; a missing source file in the product's own link step passes
every suite and then fails. Run the build too, always.

## The rules

**ASCII only, LF line endings, no tabs** in `.c`, `.h`, `.ps1` and
`runtime/bin/config.txt`. Write `perche'`, never `perché`. The `.sh`
scripts run inside WSL, where bash rejects a trailing CR.

**Verify line endings on the committed blob, not the working tree.**
`core.autocrlf` is commonly enabled and will lie to you:

```bash
git show HEAD:path/to/file.c | tr -dc '\r' | wc -c    # must print 0
```

**Zero compiler warnings.** The build uses `-Wall -Werror`. Do not silence a
warning — fix what it points at.

**`git add` with explicit file paths.** Never `git add -A`, and be careful with
directory paths too: the test suites create scratch files under
`app/guscio/`, and adding the directory sweeps them in.

**One commit per logical change**, and write *why* in the message, not what. The
diff already says what.

**`qemu/ui-winq/winq-coord.c` and `test-winq-coord.c` are off limits.** The
coordinate transform between window pixels and guest touch space is settled and
verified; changes there have a way of being subtly wrong in a manner no test
catches.

## Measurements over opinions

This project has a habit worth keeping: when a change is justified by a claim
about behaviour, **measure it and put the number in the commit message**. Several
of the fixes here exist because a plausible explanation turned out to be wrong
under measurement, and the record of that is more useful than the fix.

Two traps that have caught us more than once, in case they save you an hour:

- `initramfs.img` is **gzip**. Searching it with `strings` finds nothing even
  when the thing you're looking for is there. Decompress first.
- MSYS2's `tar` **cannot read zip files** and reports the archive as corrupt.
  Use `/c/Windows/System32/tar.exe`.

## Copyright and licensing of contributions

There is **no CLA**. You keep the copyright to what you write; it is accepted
into the project under **GPLv2**, the same license as everything else.

One consequence, stated plainly: this means the project's license can never be
changed without every contributor agreeing. That is intentional.

## Reporting a problem

Include `guest\logs\registro-guscio.log`. The last ten sessions are archived in
`guest\logs\archivio\`, so a failure from the previous run is not lost when you
restart. `guest\logs\sessione-viva.log` has what Android itself printed while
booting.

Say which Windows build and which CPU. "Windows 11 ARM64" covers machines that
behave differently.
