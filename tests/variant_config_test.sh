#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

macro_value() {
	awk -v name="$2" '$1 == "#define" && $2 == name { print $3 }' "$1"
}

preprocess_variant() {
	name=$1
	rauc=$2
	config="$TMP/$name-board.h"
	macros="$TMP/$name-macros.txt"

	cp "$ROOT/board.h" "$config"
	sed -i \
		-e 's/^#define CONFIG_BOOT_SPINAND.*/#define CONFIG_BOOT_SPINAND 0/' \
		-e 's/^#define CONFIG_BOOT_SDCARD.*/#define CONFIG_BOOT_SDCARD 0/' \
		-e 's/^#define CONFIG_BOOT_MMC.*/#define CONFIG_BOOT_MMC 1/' \
		-e "s/^#define CONFIG_RAUC_EMMC.*/#define CONFIG_RAUC_EMMC $rauc/" \
		"$config"

	"${CC:-cc}" -include "$config" \
		-I "$ROOT" -I "$ROOT/include" -I "$ROOT/lib" -I "$ROOT/lib/fatfs" \
		-I "$ROOT/arch/arm32/include" -I "$ROOT/arch/arm32/mach-t113s3/include" \
		-I "$ROOT/arch/arm32/mach-t113s3" -I "$ROOT/arch/arm32/mach-t113s3/mmc" \
		-dM -E "$ROOT/lib/loaders.c" > "$macros"

	test "$(macro_value "$macros" CONFIG_RAUC_EMMC)" = "$rauc"
	if test "$rauc" = 1; then
		test "$(macro_value "$macros" FF_VOLUMES)" = 2
		test "$(macro_value "$macros" FF_MULTI_PARTITION)" = 1
	else
		test "$(macro_value "$macros" FF_VOLUMES)" = 1
		test "$(macro_value "$macros" FF_MULTI_PARTITION)" = 0
	fi
}

preprocess_variant emmc-rauc 1
preprocess_variant emmc 0
echo "variant config tests passed"
