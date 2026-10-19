#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Build isolated, pinned ARM64 ext4 tools. WSL is a build dependency only.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
[ "$#" = 2 ] || { echo "Usage: $0 e2fsprogs-1.47.2.tar.xz NEW_OUTPUT_DIRECTORY"; exit 1; }
ARCHIVE="$(realpath "$1")"
OUTPUT="$(realpath -m "$2")"
[ ! -e "$OUTPUT" ] || { echo 'Output already exists'; exit 1; }
[ "$(uname -m)" = aarch64 ] || { echo 'An aarch64 Linux builder is required'; exit 1; }
SOURCE_HASH=08242e64ca0e8194d9c1caad49762b19209a06318199b63ce74ae4ef2d74e63c
BUSYBOX_HASH=c2f279d1d5640a0f327890d41cad594c0f059f3fed3f96dd72fdcc4f5e18ec02
echo "$SOURCE_HASH  $ARCHIVE" | sha256sum -c -
BB="$(command -v busybox)"
echo "$BUSYBOX_HASH  $BB" | sha256sum -c -
file "$BB" | grep -q 'ARM aarch64.*statically linked'
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUTPUT" "$WORK/build" "$WORK/root"/{bin,sbin,proc,sys,dev,tmp}
tar -xJf "$ARCHIVE" -C "$WORK"
# PID 1 and all tools run as root. Avoid glibc NSS module loading solely for
# the optional %u logfile-name expansion in this dedicated static appliance.
python3 - "$WORK/e2fsprogs-1.47.2" "$OUTPUT" <<'PY'
import difflib, pathlib, sys
source, out = map(pathlib.Path, sys.argv[1:])
path = source/'e2fsck/logfile.c'
before = path.read_text()
assert before.count("\tcase 'u':\n") == 1
start, end = before.index("\tcase 'u':\n"), before.index("\tcase 'U':\n")
after = before[:start] + "\tcase 'u':\n\t\tappend_string(s, \"root\", 0);\n\t\treturn;\n" + before[end:]
assert after.count('\tstruct passwd\t*pw = NULL, pw_struct;\n') == 1
after = after.replace('\tstruct passwd\t*pw = NULL, pw_struct;\n', '')
path.write_text(after)
patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
    fromfile='a/e2fsck/logfile.c', tofile='b/e2fsck/logfile.c'))
# Keep pointers into const input strings const with the newer builder headers.
for relative, old, new in (
    ('lib/blkid/tag.c', '\tchar *name, *value, *cp;\n', '\tchar *name, *value, *cp;\n\tconst char *separator;\n'),
    ('lib/blkid/tag.c', "if (!token || !(cp = strchr(token, '=')))", "if (!token || !(separator = strchr(token, '=')))"),
    ('lib/blkid/tag.c', 'value = name + (cp - token);', 'value = name + (separator - token);'),
    ('lib/support/profile.c', '\tchar\t\t\t*line, *p, *end;\n', '\tchar\t\t\t*line, *p;\n\tconst char\t\t*end;\n')):
    path = source/relative
    before = path.read_text()
    assert before.count(old) == 1
    after = before.replace(old, new)
    path.write_text(after)
    patch += ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        fromfile='a/'+relative, tofile='b/'+relative))
(out/'e2fsprogs-maintenance.patch').write_text(patch)
PY
cd "$WORK/build"
"$WORK/e2fsprogs-1.47.2/configure" --disable-nls --disable-fsck \
    --disable-uuidd --disable-fuse2fs --enable-libblkid \
    --enable-libuuid --without-udev-rules-dir --disable-defrag --disable-testio-debug \
    CFLAGS='-O2 -std=gnu11 -ffile-prefix-map='"$WORK"'=.' > "$OUTPUT/configure.log" 2>&1
if grep -i 'warning:' "$OUTPUT/configure.log"; then echo 'Configuration warnings require review'; exit 1; fi
make -j4 libs > "$OUTPUT/build.log" 2>&1
make -C e2fsck e2fsck.static >> "$OUTPUT/build.log" 2>&1
make -C resize resize2fs.static >> "$OUTPUT/build.log" 2>&1
if grep -i 'warning:' "$OUTPUT/build.log"; then echo 'Build warnings require review'; exit 1; fi
cp e2fsck/e2fsck.static "$WORK/root/sbin/e2fsck"
cp resize/resize2fs.static "$WORK/root/sbin/resize2fs"
cp "$BB" "$WORK/root/bin/busybox"
cp "$HERE/maintenance-init.sh" "$WORK/root/init"
chmod 0755 "$WORK/root/init"
for binary in "$WORK/root/sbin/"*; do
    file "$binary" | grep -q 'ARM aarch64.*statically linked'
done
for applet in sh mount cat grep sync poweroff sleep; do ln -s busybox "$WORK/root/bin/$applet"; done
# Stable archive metadata; compiler/libc versions remain recorded build inputs.
find "$WORK/root" -exec touch -h -d @0 {} +
(cd "$WORK/root"; find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --reproducible --owner=0:0 2> "$OUTPUT/cpio.log" | gzip -n -9 > "$OUTPUT/maintenance-initramfs.img")
cp "$WORK/e2fsprogs-1.47.2/NOTICE" "$OUTPUT/e2fsprogs-NOTICE"
cp "$ARCHIVE" "$OUTPUT/e2fsprogs-1.47.2.tar.xz"
cp "$BB" "$OUTPUT/busybox-aarch64"
python3 - "$OUTPUT" "$HERE" "$SOURCE_HASH" "$BUSYBOX_HASH" <<'PY'
import hashlib, json, pathlib, platform, subprocess, sys
out, scripts = map(pathlib.Path, sys.argv[1:3])
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
receipt = dict(Schema='habumi.maintenance-build.v1', Protocol=1, E2fsprogs='1.47.2',
    SourceURL='https://mirrors.edge.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.2/e2fsprogs-1.47.2.tar.xz',
    SourceSHA256=sys.argv[3], BusyboxSHA256=sys.argv[4], StaticLogUser='root',
    SourcePatchSHA256=sha(out/'e2fsprogs-maintenance.patch'),
    BusyboxPackage=subprocess.check_output(['dpkg-query','-W','busybox-static'], text=True).strip(),
    Compiler=subprocess.check_output(['gcc','--version'], text=True).splitlines()[0],
    Libc=platform.libc_ver(), Architecture=platform.machine(),
    Inputs=[dict(Path=p.name, SHA256=sha(p)) for p in (scripts/'build-maintenance-initramfs.sh', scripts/'maintenance-init.sh')],
    ImageSHA256=sha(out/'maintenance-initramfs.img'))
(out/'maintenance-build.json').write_text(json.dumps(receipt, indent=2)+'\n')
PY
sha256sum "$OUTPUT/maintenance-initramfs.img"
echo 'PASS: static ARM64 maintenance initramfs built; no Android image modified.'
