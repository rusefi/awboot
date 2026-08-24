#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
START="$ROOT/arch/arm32/mach-t113s3/start.S"
MEMCPY="$ROOT/arch/arm32/mach-t113s3/memcpy.S"
PREPROCESSED=$(mktemp)
trap 'rm -f "$PREPROCESSED"' EXIT HUP INT TERM

grep -q 'alignment fault enable' "$START"
"${CC:-cc}" -E -P -x assembler-with-cpp "$MEMCPY" > "$PREPROCESSED"
if grep -Eq 'ldrneh|strneh|ldrcs[[:space:]]+r3, \[r1\], #4|strcs[[:space:]]+r3, \[ip\], #4|ldrmi[[:space:]]+r3, \[r1\], #4|strmi[[:space:]]+r3, \[ip\], #4' "$PREPROCESSED"; then
	echo "T113S3 memcpy enables ARM unaligned accesses while alignment faults are enabled" >&2
	exit 1
fi
grep -q 'ldrcsb' "$PREPROCESSED"
grep -q 'ldrmib' "$PREPROCESSED"

echo "T113S3 memcpy alignment configuration passed"
