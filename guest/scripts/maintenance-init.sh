#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# One unmounted virtio data device, no Android, networking or interactive shell.
export PATH=/sbin:/bin
mount -t devtmpfs devtmpfs /dev
exec </dev/console >/dev/console 2>&1
mount -t proc proc /proc
mount -t sysfs sysfs /sys
token=''
for arg in $(cat /proc/cmdline); do
    case "$arg" in habumi.maintenance=*) token=${arg#*=};; esac
done
finish() {
    sync
    echo "HABUMI-MAINT v1 $token $1"
    poweroff -f
    while :; do sleep 1; done
}
case "$token" in ''|*[!a-f0-9]*) finish FAIL-TOKEN;; esac
[ "${#token}" = 32 ] || finish FAIL-TOKEN
attempt=0
while [ ! -b /dev/vda ] && [ "$attempt" -lt 100 ]; do sleep .1; attempt=$((attempt+1)); done
[ -b /dev/vda ] && [ ! -b /dev/vdb ] || finish FAIL-DEVICE
echo "HABUMI-MAINT v1 $token BEGIN"
e2fsck -f -p /dev/vda
status=$?
[ "$status" -le 3 ] || finish FAIL-PRECHECK
resize2fs /dev/vda || finish FAIL-RESIZE
e2fsck -f -n /dev/vda || finish FAIL-POSTCHECK
finish OK
