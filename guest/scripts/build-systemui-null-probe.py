#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build an isolated experimental SystemUI null guard; never install it.

Requires explicit paths and the pinned APK/apktool inputs. Reassembles only
classes.dex; preserves all other non-signature ZIP entry contents. Signing
uses an explicitly supplied existing AOSP platform test key without reading
or copying its bytes. Failed output directories are retained for inspection.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

APK_SHA = "2f8fe1b0d88a33b26d3c7ea09dedf56248002ec00b86ba8431ed257573da6960"
TOOL_SHA = "dbf930b076c6b9be08d57c449cacefc3bdd6b71ebd59b3066fc0e1f5b14f9423"
SIGNER_SHA = "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8"
TARGET = Path("com/android/systemui/appops/AppOpsControllerImpl.smali")
HEADER = ".method public setListening(Z)V"
GET_OPS = "invoke-virtual {p1, v0}, Landroid/app/AppOpsManager;->getPackagesForOps([I)Ljava/util/List;"
ITERATOR = "invoke-interface {p1}, Ljava/util/List;->iterator()Ljava/util/Iterator;"
GUARD = "    if-eqz p1, :cond_3\n\n"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def signature(name):
    return bool(re.fullmatch(r"META-INF/(?:MANIFEST\.MF|[^/]+\.(?:SF|RSA|DSA|EC))", name, re.I))


def tree_hashes(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(root.rglob("*.smali"))}


def normalized(text):
    # I nomi delle etichette cambiano durante il disassemblaggio: conta la destinazione.
    result = []
    for block in re.split(r"(?m)(?=^\.method )", text):
        labels = {name: f":label_{i}" for i, name in enumerate(re.findall(r"(?m)^\s*(:[\w]+)\s*$", block))}
        for line in block.splitlines():
            line = line.strip()
            if not line or line.startswith(("#", ".line ")):
                continue
            # Il writer DEX omette i valori booleani statici falsi impliciti.
            if line.startswith(".field ") and " static " in line:
                line = re.sub(r"(:Z) = false$", r"\1", line)
            line = re.sub(r'"(?:\\.|[^"\\])*"|:[\w]+',
                          lambda m: labels.get(m[0], m[0]) if not m[0].startswith('"') else m[0], line)
            result.append(line)
    return result


def compare_trees(expected, actual):
    left, right = tree_hashes(expected), tree_hashes(actual)
    require(left.keys() == right.keys(), "Primary DEX class inventory changed")
    for name in left:
        if left[name] == right[name]:
            continue
        require(normalized((expected / name).read_text("utf-8")) == normalized((actual / name).read_text("utf-8")),
                f"Unexpected disassembled class change: {name}")
    return len(left)


def patch(text):
    require(text.count(HEADER) == 1, "Unexpected setListening declaration")
    start = text.index(HEADER)
    end = text.index(".end method", start)
    method = text[start:end]
    require("    .locals 14\n" in method, "Unexpected register count")
    require(method.count(GET_OPS) == 1 and method.count(":cond_3\n") >= 1, "Unexpected AppOps control flow")
    before, after = method.split(GET_OPS)
    match = re.match(r"(\s*(?:\.line \d+\s*)*move-result-object p1\n)", after)
    require(match is not None, "Missing AppOps result register")
    insertion = len(match[1])
    require(after[insertion:].lstrip().startswith(".line 12\n    " + ITERATOR), "Unexpected first List iterator")
    require(re.search(r":cond_3\n\s*iget-object p1, p0, .*->mAppOps:Landroid/app/AppOpsManager;", method),
            "Null branch does not resume AppOps registration")
    for token in ("startWatchingActive", "startWatchingNoted", "registerAudioRecordingCallback", "->addCallback",
                  "registerReceiverWithHandler", "stopWatchingActive", "stopWatchingNoted", "if-eqz p1, :cond_6"):
        require(token in method, f"Missing expected listener operation: {token}")
    method = before + GET_OPS + after[:insertion] + "\n" + GUARD + after[insertion:]
    return text[:start] + method + text[end:]


def verify_guard(text):
    method = text.split(HEADER, 1)[1].split(".end method", 1)[0]
    lines = [line for line in normalized(method)]
    idx = lines.index(GET_OPS)
    require(lines[idx + 1] == "move-result-object p1", "Roundtrip result register changed")
    match = re.fullmatch(r"if-eqz p1, (:label_\d+)", lines[idx + 2])
    require(match is not None and lines[idx + 3] == ITERATOR, "Roundtrip null guard missing")
    target = lines.index(match[1])
    require("->mAppOps:Landroid/app/AppOpsManager;" in lines[target + 1], "Guard bypasses listener continuation")
    require("->startWatchingActive(" in lines[target + 3], "Guard does not lead to active watcher")


def compare_zip(original, result):
    with zipfile.ZipFile(original) as old, zipfile.ZipFile(result) as new:
        a, b = old.namelist(), new.namelist()
        require(len(a) == len(set(a)) and len(b) == len(set(b)), "Duplicate ZIP entries")
        expected = {n for n in a if not signature(n)}
        actual = {n for n in b if not signature(n)}
        require(expected == actual, "Non-signature ZIP entry inventory changed")
        preserved = []
        for name in sorted(expected - {"classes.dex"}):
            require(old.read(name) == new.read(name), f"Non-primary ZIP entry changed: {name}")
            preserved.append(name)
        require(old.read("classes.dex") != new.read("classes.dex"), "Primary DEX did not change")
        return preserved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original-apk", "decoded-tree", "apktool-jar", "java", "javac", "build-tools", "test-key", "test-cert", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    paths = {name: value.resolve() for name, value in vars(args).items()}
    out = paths["output"]
    require(not out.exists(), "Output directory must not exist")
    for name, path in paths.items():
        if name != "output":
            require(path.exists(), f"Missing explicit input/tool: {name}")
            require(out != path and out not in path.parents, "Output may not contain an input/tool")
            require(not path.is_dir() or path not in out.parents, "Output may not be inside an input/tool directory")
    require(digest(paths["original_apk"]) == APK_SHA, "Original APK hash mismatch")
    require(digest(paths["apktool_jar"]) == TOOL_SHA, "apktool jar hash mismatch")
    smali = paths["decoded_tree"] / "smali"
    require(smali.is_dir(), "Decoded primary smali directory missing")
    inputs = tree_hashes(smali)
    source = Path(__file__).resolve().parent.parent / "probe" / "AssembleSystemUiDex.java"
    require(source.is_file(), "Assembly helper missing")
    out.mkdir(parents=False)
    metadata = {"classification": "experimental-not-release", "status": "building", "original_apk_sha256": APK_SHA,
                "apktool_sha256": TOOL_SHA, "expected_signer_sha256": SIGNER_SHA,
                "helper_sha256": digest(source), "script_sha256": digest(Path(__file__).resolve()),
                "semantic_comparison": "Preserves labels by destination; ignores line directives and implicit static boolean false defaults",
                "limitations": ["No ART runtime verification or guest boot test", "No installation performed",
                                "Primary DEX is reassembled in full; non-code debug/layout bytes may change",
                                "Public AOSP platform test signature is unsuitable as a production trust boundary"]}
    report = out / "build-metadata.json"

    def run(label, command):
        process = subprocess.run([str(c) for c in command], capture_output=True, text=True, encoding="utf-8", errors="replace")
        # Non si registrano argomenti di firma o contenuti delle chiavi.
        (out / (label + ".log")).write_text(process.stdout + process.stderr, "utf-8")
        require(process.returncode == 0, f"Tool failed: {label}; inspect retained log")
        return process.stdout + process.stderr

    def signer(apk, label):
        log = run(label, [paths["java"], "-jar", paths["build_tools"] / "lib/apksigner.jar", "verify", "--verbose", "--print-certs", apk])
        found = re.findall(r"Signer #\d+ certificate SHA-256 digest:\s*([a-fA-F0-9]+)", log)
        require(found == [SIGNER_SHA], "APK signer certificate mismatch")

    classes = out / "helper-classes"
    classes.mkdir()
    cp = str(classes) + os.pathsep + str(paths["apktool_jar"])

    def helper(label, *arguments):
        return run(label, [paths["java"], "-Xmx3g", "-cp", cp, "AssembleSystemUiDex", *arguments])

    try:
        signer(paths["original_apk"], "original-signature")
        run("javac", [paths["javac"], "--release", "17", "-cp", paths["apktool_jar"], "-d", classes, source])
        old_dex = out / "classes.original.dex"
        with zipfile.ZipFile(paths["original_apk"]) as archive:
            old_dex.write_bytes(archive.read("classes.dex"))
        baseline = out / "primary-original-roundtrip"
        helper("decode-original", "decode", paths["original_apk"], old_dex, baseline)
        metadata["primary_class_count"] = compare_trees(smali, baseline)
        copied = out / "primary-patched-smali"
        shutil.copytree(smali, copied)
        target = copied / TARGET
        original_text = target.read_text("utf-8")
        target.write_text(patch(original_text), "utf-8", newline="\n")
        require([name for name, sha in tree_hashes(copied).items() if inputs[name] != sha] == [TARGET.as_posix()],
                "Patch changed more than the intended source class")
        dex = out / "classes.dex"
        helper("assemble", "assemble", copied, dex)
        decoded = out / "primary-patched-roundtrip"
        helper("decode-patched", "decode", paths["original_apk"], dex, decoded)
        compare_trees(copied, decoded)
        verify_guard((decoded / TARGET).read_text("utf-8"))
        roundtrip = out / "classes.roundtrip.dex"
        helper("assemble-roundtrip", "assemble", decoded, roundtrip)
        require(digest(roundtrip) == digest(dex), "DEX assembly roundtrip is not byte-stable")
        unsigned = out / "SystemUI.null-probe.unsigned.apk"
        with zipfile.ZipFile(paths["original_apk"]) as old, zipfile.ZipFile(unsigned, "x") as new:
            new.comment = old.comment
            for entry in old.infolist():
                if not signature(entry.filename):
                    new.writestr(entry, dex.read_bytes() if entry.filename == "classes.dex" else old.read(entry.filename))
        compare_zip(paths["original_apk"], unsigned)
        aligned = out / "SystemUI.null-probe.aligned.apk"
        run("zipalign", [paths["build_tools"] / "zipalign.exe", "-p", "4", unsigned, aligned])
        signed = out / "SystemUI.null-probe.experimental.apk"
        run("sign", [paths["java"], "-jar", paths["build_tools"] / "lib/apksigner.jar", "sign", "--key", paths["test_key"],
                     "--cert", paths["test_cert"], "--v4-signing-enabled", "false", "--out", signed, aligned])
        signer(signed, "signature-verification")
        run("alignment-verification", [paths["build_tools"] / "zipalign.exe", "-c", "-p", "4", signed])
        metadata["preserved_zip_entries"] = compare_zip(paths["original_apk"], signed)
        with zipfile.ZipFile(signed) as archive:
            require(hashlib.sha256(archive.read("classes.dex")).hexdigest() == digest(dex), "Signing altered primary DEX")
        require(tree_hashes(smali) == inputs and digest(paths["original_apk"]) == APK_SHA and digest(paths["apktool_jar"]) == TOOL_SHA,
                "An original input changed during build")
        metadata.update(status="validated-static-only", artifact=signed.name, artifact_sha256=digest(signed),
                        primary_dex_sha256=digest(dex), roundtrip_dex_byte_identical=True,
                        guard="null package list skips initial iteration and continues watcher registration",
                        signer_verified=True, original_inputs_unchanged=True)
        print(json.dumps({key: metadata[key] for key in ("classification", "status", "artifact", "artifact_sha256", "primary_class_count")}, indent=2))
    except Exception as error:
        metadata.update(status="failed", error=str(error))
        raise
    finally:
        report.write_text(json.dumps(metadata, indent=2) + "\n", "utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile) as error:
        print(f"Build refused or failed: {error}", file=sys.stderr)
        sys.exit(1)
