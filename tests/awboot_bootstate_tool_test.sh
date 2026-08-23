#!/bin/sh
set -eu

TOOL=${1:-../tools/awboot-bootstate}
STATE=$(mktemp)
trap 'rm -f "$STATE"' EXIT

dd if=/dev/zero of="$STATE" bs=512 count=2 status=none
export AWBOOT_BOOTSTATE_DEVICE="$STATE"

test "$("$TOOL" initialize rootfs.0)" = ""
test "$("$TOOL" get-primary)" = "rootfs.0"
test "$("$TOOL" get-current)" = "rootfs.0"
test "$("$TOOL" get-state rootfs.0)" = "good"
test "$("$TOOL" get-state rootfs.1)" = "good"

# Exact RAUC 1.15.2 install callbacks around the grouped image writes.
"$TOOL" set-state rootfs.1 bad
test "$("$TOOL" get-state rootfs.1)" = "bad"
"$TOOL" set-primary rootfs.1
test "$("$TOOL" get-primary)" = "rootfs.1"
test "$("$TOOL" get-state rootfs.0)" = "good"
test "$("$TOOL" get-state rootfs.1)" = "bad"
"$TOOL" set-state rootfs.1 good
test "$("$TOOL" get-state rootfs.1)" = "good"

if "$TOOL" initialize rootfs.0 2>/dev/null; then
	echo "second initialization unexpectedly succeeded" >&2
	exit 1
fi
if "$TOOL" set-state invalid good 2>/dev/null; then
	echo "invalid bootname unexpectedly succeeded" >&2
	exit 1
fi

echo "awboot bootstate tool tests passed"
