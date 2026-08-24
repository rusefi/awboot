#ifndef AWBOOT_RAUC_BOOT_H
#define AWBOOT_RAUC_BOOT_H

#include "bootstate_storage.h"
#include "rauc_mbr.h"

#define AWBOOT_RAUC_CMDLINE_SIZE 320U

struct awboot_rauc_boot_io {
	void *context;
	uint64_t sector_count;
	awboot_bootstate_read_sector_fn read_sector;
	awboot_bootstate_write_sector_fn write_sector;
	awboot_bootstate_sync_fn sync;
};

struct awboot_rauc_boot_result {
	struct awboot_rauc_mbr_layout layout;
	struct awboot_bootstate state;
	enum awboot_bootstate_selection selection;
	enum awboot_rauc_mbr_status mbr_status;
	enum awboot_bootstate_storage_status storage_status;
	uint8_t selected_slot;
	bool fallback;
	char root_partuuid[AWBOOT_RAUC_MBR_PARTUUID_SIZE];
};

enum awboot_rauc_boot_status {
	AWBOOT_RAUC_BOOT_OK = 0,
	AWBOOT_RAUC_BOOT_INVALID_ARGUMENT,
	AWBOOT_RAUC_BOOT_MBR_READ_FAILED,
	AWBOOT_RAUC_BOOT_MBR_INVALID,
	AWBOOT_RAUC_BOOTSTATE_LOAD_FAILED,
	AWBOOT_RAUC_BOOT_NO_BOOTABLE_SLOT,
	AWBOOT_RAUC_BOOTSTATE_COMMIT_FAILED,
	AWBOOT_RAUC_BOOT_PARTUUID_FAILED,
};

enum awboot_rauc_boot_status awboot_rauc_boot_prepare(
	const struct awboot_rauc_boot_io *io, struct awboot_rauc_boot_result *result);

bool awboot_rauc_boot_watchdog_required(const struct awboot_rauc_boot_result *result);

bool awboot_rauc_boot_format_cmdline(const struct awboot_rauc_boot_result *result,
									 char output[AWBOOT_RAUC_CMDLINE_SIZE]);

#endif
