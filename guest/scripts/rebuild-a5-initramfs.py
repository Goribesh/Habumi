#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Rebuild the A5 initramfs from the pinned RC2 initramfs and tracked sources."""

import argparse
import gzip
import hashlib
import json
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


BASE = "c2b16486dc18115cf489c2a2430650a4a8b47ccf38497feceb9e150c2cd3f937"
OMX_OLD = "9bdf01dfd15160da62a0f7175219a84d197df25b72e8941b37b36b8b311b226c"
OMX_STOCK = "b343ab767e7511b7c3abd522184ff1db8bb4b2d0c4491ed8ac63ff63509e8a50"
OMX_A5 = "2402bdb295e97b48b493e4caa7891d4ff020a9eef01daa48a06cc4adf9ae81da"
OMX_MEMBER = b"winq/omx-nonblocking-probe.so"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(value):
    return (value + 3) & ~3


def rewrite(data, replacements):
    """Preserve metadata and untouched entries in a single newc archive."""
    output = bytearray()
    seen = set()
    position = 0
    while position + 110 <= len(data):
        header = data[position:position + 110]
        if header[:6] != b"070701":
            raise ValueError("expected newc header")
        size = int(header[54:62], 16)
        name_size = int(header[94:102], 16)
        name_end = position + 110 + name_size
        start = align(name_end)
        end = align(start + size)
        if not name_size or end > len(data) or data[name_end - 1] != 0:
            raise ValueError("invalid newc member bounds")
        name = data[position + 110:name_end - 1].removeprefix(b"./")
        if name == b"TRAILER!!!":
            if size or any(data[end:]) or seen != replacements.keys():
                raise ValueError("unexpected trailer or missing replacement member")
            output.extend(data[position:])
            return bytes(output)
        if name in replacements:
            if name in seen:
                raise ValueError(f"duplicate replacement member: {name!r}")
            seen.add(name)
            replacement = replacements[name](data[start:start + size])
            output.extend(header[:54] + f"{len(replacement):08x}".encode() + header[62:])
            output.extend(data[position + 110:start])
            output.extend(replacement)
            output.extend(bytes(align(len(replacement)) - len(replacement)))
        else:
            output.extend(data[position:end])
        position = end
    raise ValueError("newc archive has no trailer")


def playback_omx(old, recipe, work):
    if digest(old) != OMX_OLD:
        raise ValueError("RC2 OMX probe hash mismatch")
    # Recover the stock input by reversing the single verified RC2 change.
    before = bytes.fromhex("e203012a e1031f2a 04140014")
    after = bytes.fromhex("e203012a 21008052 04140014")
    stock = old.replace(before, after, 1)
    if digest(stock) != OMX_STOCK:
        raise ValueError("stock OMX reconstruction hash mismatch")
    source, candidate, receipt = (work / name for name in ("stock.so", "a5.so", "omx.json"))
    source.write_bytes(stock)
    subprocess.run([sys.executable, str(recipe), "--input", str(source), "--output",
                    str(candidate), "--metadata", str(receipt), "--playback"], check=True)
    result = candidate.read_bytes()
    if digest(result) != OMX_A5:
        raise ValueError("playback OMX output hash mismatch")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    receipt = args.output.with_suffix(args.output.suffix + ".json")
    if args.output.exists() or receipt.exists():
        parser.error("output and its receipt must be new paths")
    source_dir = Path(__file__).resolve().parent
    recipe = source_dir / "prepare-omx-nonblocking-probe.py"
    init = (source_dir / "android-initramfs-init.sh").read_bytes().replace(b"\r\n", b"\n")
    base = args.base.read_bytes()
    if digest(base) != BASE:
        parser.error("expected the pinned RC2 initramfs")
    with tempfile.TemporaryDirectory(dir=args.output.parent) as work:
        patched = rewrite(gzip.decompress(base), {
            b"init": lambda _: init,
            OMX_MEMBER: lambda old: playback_omx(old, recipe, Path(work)),
        })
    # GzipFile fixes both the timestamp and original filename in the header.
    with args.output.open("xb") as stream:
        with gzip.GzipFile(filename="", fileobj=stream, mode="wb", mtime=0, compresslevel=6) as archive:
            archive.write(patched)
    metadata = {
        "base_sha256": BASE, "init_sha256": digest(init),
        "omx_recipe_sha256": digest(recipe.read_bytes()), "omx_sha256": OMX_A5,
        "output_sha256": digest(args.output.read_bytes()),
        "python": sys.version.split()[0], "zlib": zlib.ZLIB_RUNTIME_VERSION,
        "changed_members": ["init", OMX_MEMBER.decode()],
    }
    with receipt.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(metadata, stream, indent=2, sort_keys=True)
        stream.write("\n")


if __name__ == "__main__":
    main()
