#ifndef AWBOOT_RAUC_MBR_H
#define AWBOOT_RAUC_MBR_H

#include <stdbool.h>
#include <stdint.h>

#define AWBOOT_RAUC_MBR_SECTOR_SIZE       512U
#define AWBOOT_RAUC_MBR_PARTITION_COUNT   9U
#define AWBOOT_RAUC_MBR_LOGICAL_COUNT     5U
#define AWBOOT_RAUC_BOOTSTATE_COPY_COUNT  2U
#define AWBOOT_RAUC_MBR_PARTUUID_SIZE     12U

enum awboot_rauc_partition_number {
	AWBOOT_RAUC_PART_BOOT_A = 1,
	AWBOOT_RAUC_PART_BOOT_B,
	AWBOOT_RAUC_PART_BOOTSTATE,
	AWBOOT_RAUC_PART_EXTENDED,
	AWBOOT_RAUC_PART_ROOTFS_A,
	AWBOOT_RAUC_PART_ROOTFS_B,
	AWBOOT_RAUC_PART_APPFS_A,
	AWBOOT_RAUC_PART_APPFS_B,
	AWBOOT_RAUC_PART_DATA,
};

struct awboot_mbr_partition {
	uint8_t  type;
	uint64_t start_lba;
	uint64_t sector_count;
};

struct awboot_rauc_mbr_layout {
	uint32_t disk_signature;
	struct awboot_mbr_partition partition[AWBOOT_RAUC_MBR_PARTITION_COUNT];
	uint64_t bootstate_copy_lba[AWBOOT_RAUC_BOOTSTATE_COPY_COUNT];
	uint64_t ebr_lba[AWBOOT_RAUC_MBR_LOGICAL_COUNT];
};

typedef bool (*awboot_rauc_mbr_read_sector_fn)(void *context, uint64_t lba,
												uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE]);

enum awboot_rauc_mbr_status {
	AWBOOT_RAUC_MBR_OK = 0,
	AWBOOT_RAUC_MBR_INVALID_ARGUMENT,
	AWBOOT_RAUC_MBR_BAD_SIGNATURE,
	AWBOOT_RAUC_MBR_GPT_PROTECTIVE,
	AWBOOT_RAUC_MBR_ZERO_DISK_SIGNATURE,
	AWBOOT_RAUC_MBR_BAD_BOOT_FLAG,
	AWBOOT_RAUC_MBR_BAD_PRIMARY_TYPE,
	AWBOOT_RAUC_MBR_EMPTY_PRIMARY,
	AWBOOT_RAUC_MBR_PRIMARY_RANGE,
	AWBOOT_RAUC_MBR_PRIMARY_ORDER,
	AWBOOT_RAUC_MBR_BOOTSTATE_TOO_SMALL,
	AWBOOT_RAUC_MBR_EBR_READ_FAILED,
	AWBOOT_RAUC_MBR_BAD_EBR_SIGNATURE,
	AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT,
	AWBOOT_RAUC_MBR_BAD_LOGICAL_TYPE,
	AWBOOT_RAUC_MBR_LOGICAL_RANGE,
	AWBOOT_RAUC_MBR_LOGICAL_ORDER,
};

/*
 * Validate the fixed RAUC DOS layout: p1/p2 boot FAT, p3 raw bootstate,
 * p4 extended, then rootfs A/B, appfs A/B, and data as logical p5-p9.
 */
enum awboot_rauc_mbr_status awboot_rauc_mbr_parse(
	const uint8_t mbr[AWBOOT_RAUC_MBR_SECTOR_SIZE], uint64_t device_sector_count,
	awboot_rauc_mbr_read_sector_fn read_sector, void *context,
	struct awboot_rauc_mbr_layout *layout);

bool awboot_rauc_mbr_format_partuuid(uint32_t disk_signature, uint8_t partition_number,
									char output[AWBOOT_RAUC_MBR_PARTUUID_SIZE]);

#endif
