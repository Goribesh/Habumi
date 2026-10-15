#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Prepare an experimental, single-instruction OMX nonblocking probe.

This is a byte-level probe for the supplied Waydroid 20260927 arm64_only
OMX library. It does not rebuild or patch upstream source. The candidate
instruction replaces the getService retry-count move with the matching
tryGetService nonblocking value, after confirming the original ELF layout,
instruction context, and shared helper branch.

The HIDL OMX interface provenance is Android Open Source Project
hardware/interfaces/media/omx/1.0 (Apache-2.0):
https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/android13-release/media/omx/1.0/
The reported Waydroid MediaProvider OMX timeout is tracked at:
https://github.com/waydroid/waydroid/issues/2397

The output is experimental probe material, not a release artifact. The
input is never modified, and output/metadata paths must be new files.
"""

import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path


EXPECTED_INPUT_SHA256 = "b343ab767e7511b7c3abd522184ff1db8bb4b2d0c4491ed8ac63ff63509e8a50"
EXPECTED_SIZE = 473368
EXPECTED_MACHINE_AARCH64 = 183
PT_LOAD = 1
ET_DYN = 3

GET_SERVICE_VA = 0x62438
GET_SERVICE_CONTEXT = bytes.fromhex("e203012a 21008052 04140014")
PATCH_VA = 0x6243C
OLD_WORD = 0x52800021
NEW_WORD = 0x2A1F03E1
NEW_BYTES = struct.pack("<I", NEW_WORD)

TRY_GET_SERVICE_VA = 0x621C0
TRY_GET_SERVICE_PREFIX = bytes.fromhex("e203012a e1031f2a")
SHARED_HELPER_VA = 0x67450


class ProbeError(Exception):
    """An input or output did not satisfy the probe's safety checks."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fail(message):
    raise ProbeError(message)


def elf_load_segments(data):
    if len(data) < 64 or data[:4] != b"\x7fELF":
        fail("input is not a complete ELF file")
    if data[4] != 2:
        fail("ELF class is not ELF64")
    if data[5] != 1:
        fail("ELF data encoding is not little-endian")
    if data[6] != 1:
        fail("unsupported ELF identification version")

    fields = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    e_type, machine, version = fields[0:3]
    phoff, ehsize, phentsize, phnum = fields[4], fields[7], fields[8], fields[9]
    if machine != EXPECTED_MACHINE_AARCH64:
        fail("ELF machine is not AArch64")
    if e_type != ET_DYN:
        fail("ELF is not a shared object (ET_DYN)")
    if version != 1 or ehsize < 64:
        fail("invalid ELF64 header version or size")
    if phentsize < 56 or phnum == 0:
        fail("invalid or missing ELF program-header table")
    if phoff > len(data) or phnum > (len(data) - phoff) // phentsize:
        fail("ELF program-header table exceeds input bounds")

    segments = []
    for index in range(phnum):
        start = phoff + index * phentsize
        p_type, p_flags, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, _p_align = struct.unpack_from(
            "<IIQQQQQQ", data, start
        )
        if p_type != PT_LOAD:
            continue
        if p_filesz > p_memsz or p_offset > len(data) or p_filesz > len(data) - p_offset:
            fail("invalid PT_LOAD file bounds")
        if p_vaddr + p_filesz > 0xFFFFFFFFFFFFFFFF:
            fail("PT_LOAD virtual-address range overflows")
        segments.append((p_vaddr, p_vaddr + p_filesz, p_offset))
    if not segments:
        fail("ELF contains no PT_LOAD segments")
    return segments


def va_to_offset(segments, virtual_address, length):
    matches = []
    end = virtual_address + length
    for start, segment_end, file_offset in segments:
        if start <= virtual_address and end <= segment_end:
            matches.append(file_offset + (virtual_address - start))
    if len(matches) != 1:
        fail("virtual address does not map uniquely to file-backed PT_LOAD data")
    return matches[0]


def branch_target_aarch64(word, pc):
    if word & 0xFC000000 != 0x14000000:
        fail("expected an AArch64 unconditional B instruction in tryGetService")
    immediate = word & 0x03FFFFFF
    if immediate & (1 << 25):
        immediate -= 1 << 26
    return pc + (immediate << 2)


def prepare(input_path, output_path, metadata_path):
    input_path = Path(input_path).resolve(strict=True)
    output_path = Path(output_path).resolve(strict=False)
    metadata_path = Path(metadata_path).resolve(strict=False)
    if output_path == input_path:
        fail("output must not be the input file")
    if metadata_path in (input_path, output_path):
        fail("metadata path must differ from input and output paths")
    for path in (output_path, metadata_path):
        if os.path.lexists(path):
            fail("refusing to overwrite existing path: " + str(path))
        if not path.parent.is_dir():
            fail("output parent directory must already exist: " + str(path.parent))

    original = input_path.read_bytes()
    input_hash = sha256(original)
    if len(original) != EXPECTED_SIZE:
        fail("input length does not match the verified Waydroid source")
    if input_hash != EXPECTED_INPUT_SHA256:
        fail("input SHA-256 does not match the verified Waydroid source")

    segments = elf_load_segments(original)
    get_offset = va_to_offset(segments, GET_SERVICE_VA, len(GET_SERVICE_CONTEXT))
    if original[get_offset:get_offset + len(GET_SERVICE_CONTEXT)] != GET_SERVICE_CONTEXT:
        fail("getService 12-byte instruction context does not match")
    if struct.unpack_from("<I", original, get_offset + 4)[0] != OLD_WORD:
        fail("getService retry instruction does not match")
    get_branch_word = struct.unpack_from("<I", original, get_offset + 8)[0]
    get_branch_target = branch_target_aarch64(get_branch_word, GET_SERVICE_VA + 8)
    if get_branch_target != SHARED_HELPER_VA:
        fail("getService branch does not reach the expected shared helper")

    try_offset = va_to_offset(segments, TRY_GET_SERVICE_VA, 12)
    try_prefix = original[try_offset:try_offset + len(TRY_GET_SERVICE_PREFIX)]
    if try_prefix != TRY_GET_SERVICE_PREFIX:
        fail("tryGetService instruction prefix does not match")
    try_branch_word = struct.unpack_from("<I", original, try_offset + 8)[0]
    try_branch_target = branch_target_aarch64(try_branch_word, TRY_GET_SERVICE_VA + 8)
    if try_branch_target != SHARED_HELPER_VA:
        fail("tryGetService branch does not reach the expected shared helper")

    candidate = bytearray(original)
    patch_offset = va_to_offset(segments, PATCH_VA, len(NEW_BYTES))
    candidate[patch_offset:patch_offset + len(NEW_BYTES)] = NEW_BYTES
    differences = [i for i, (before, after) in enumerate(zip(original, candidate)) if before != after]
    expected_differences = [patch_offset + i for i in range(4) if original[patch_offset + i] != NEW_BYTES[i]]
    if differences != expected_differences or not differences:
        fail("candidate differs outside the targeted instruction")

    byte_changes = [
        {
            "file_offset": offset,
            "virtual_address": PATCH_VA + offset - patch_offset,
            "before_hex": original[offset:offset + 1].hex(),
            "after_hex": candidate[offset:offset + 1].hex(),
        }
        for offset in differences
    ]
    metadata = {
        "classification": "experimental probe not release",
        "input_name": input_path.name,
        "input_size": len(original),
        "input_sha256": input_hash,
        "output_name": output_path.name,
        "output_size": len(candidate),
        "output_sha256": sha256(candidate),
        "elf": {"class": 64, "endianness": "little", "machine": "AArch64"},
        "patch": {
            "virtual_address": PATCH_VA,
            "file_offset": patch_offset,
            "instruction_before": "0x%08x" % OLD_WORD,
            "instruction_after": "0x%08x" % NEW_WORD,
            "byte_changes": byte_changes,
        },
        "validation": {
            "get_service_context_hex": GET_SERVICE_CONTEXT.hex(),
            "get_service_branch_virtual_address": GET_SERVICE_VA + 8,
            "get_service_branch_word": "0x%08x" % get_branch_word,
            "get_service_branch_target": get_branch_target,
            "try_get_service_prefix_hex": TRY_GET_SERVICE_PREFIX.hex(),
            "try_get_service_branch_virtual_address": TRY_GET_SERVICE_VA + 8,
            "try_get_service_branch_word": "0x%08x" % try_branch_word,
            "shared_helper_virtual_address": SHARED_HELPER_VA,
            "try_get_service_branch_target": try_branch_target,
            "changed_file_offsets": differences,
        },
        "provenance": {
            "upstream_issue": "https://github.com/waydroid/waydroid/issues/2397",
            "source_project": "Android Open Source Project hardware/interfaces/media/omx/1.0",
            "source_url": "https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/android13-release/media/omx/1.0/",
            "license": "Apache-2.0; this probe contains no copied upstream source code",
        },
    }

    created = []
    try:
        with output_path.open("xb") as output_file:
            created.append(output_path)
            output_file.write(candidate)
            output_file.flush()
            os.fsync(output_file.fileno())
        with metadata_path.open("x", encoding="utf-8", newline="\n") as metadata_file:
            created.append(metadata_path)
            json.dump(metadata, metadata_file, indent=2, sort_keys=True)
            metadata_file.write("\n")
            metadata_file.flush()
            os.fsync(metadata_file.fileno())
    except BaseException:
        for path in reversed(created):
            try:
                path.unlink()
            except OSError:
                pass
        raise
    return metadata


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="verified original OMX ELF")
    parser.add_argument("--output", required=True, help="new probe ELF path; must not exist")
    parser.add_argument("--metadata", required=True, help="new JSON metadata path; must not exist")
    args = parser.parse_args(argv)
    try:
        result = prepare(args.input, args.output, args.metadata)
    except (ProbeError, OSError, ValueError, struct.error) as error:
        print("probe preparation failed: " + str(error), file=sys.stderr)
        return 2
    print(json.dumps({"status": result["classification"], "output_sha256": result["output_sha256"], "metadata": args.metadata}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
