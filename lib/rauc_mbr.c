#include "rauc_mbr.h"

#include <stddef.h>
#include <string.h>

#define MBR_DISK_SIGNATURE_OFFSET 440U
#define MBR_PARTITION_TABLE_OFFSET 446U
#define MBR_PARTITION_ENTRY_SIZE   16U
#define MBR_SIGNATURE_OFFSET       510U

#define PARTITION_BOOT_FLAG_OFFSET 0U
#define PARTITION_TYPE_OFFSET      4U
#define PARTITION_START_OFFSET     8U
#define PARTITION_SIZE_OFFSET      12U

#define MBR_TYPE_FAT32_LBA 0x0cU
#define MBR_TYPE_BOOTSTATE 0xdaU
#define MBR_TYPE_LINUX     0x83U
#define MBR_TYPE_GPT       0xeeU

struct mbr_entry {
	uint8_t  boot_flag;
	uint8_t  type;
	uint32_t start_lba;
	uint32_t sector_count;
};

static uint32_t read_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
		   ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static struct mbr_entry read_entry(const uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE], unsigned int index)
{
	const uint8_t *entry = sector + MBR_PARTITION_TABLE_OFFSET + index * MBR_PARTITION_ENTRY_SIZE;

	return (struct mbr_entry){
		.boot_flag   = entry[PARTITION_BOOT_FLAG_OFFSET],
		.type        = entry[PARTITION_TYPE_OFFSET],
		.start_lba   = read_le32(entry + PARTITION_START_OFFSET),
		.sector_count = read_le32(entry + PARTITION_SIZE_OFFSET),
	};
}

static bool has_sector_signature(const uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE])
{
	return sector[MBR_SIGNATURE_OFFSET] == 0x55U && sector[MBR_SIGNATURE_OFFSET + 1U] == 0xaaU;
}

static bool valid_boot_flag(uint8_t boot_flag)
{
	return boot_flag == 0x00U || boot_flag == 0x80U;
}

static bool extended_type(uint8_t type)
{
	return type == 0x05U || type == 0x0fU || type == 0x85U;
}

static bool entry_empty(const uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE], unsigned int index)
{
	const uint8_t *entry = sector + MBR_PARTITION_TABLE_OFFSET + index * MBR_PARTITION_ENTRY_SIZE;

	for (unsigned int offset = 0U; offset < MBR_PARTITION_ENTRY_SIZE; offset++) {
		if (entry[offset] != 0U) {
			return false;
		}
	}
	return true;
}

static bool valid_range(uint64_t start_lba, uint64_t sector_count, uint64_t limit)
{
	return sector_count != 0U && start_lba < limit && sector_count <= limit - start_lba;
}

static enum awboot_rauc_mbr_status parse_primaries(
	const uint8_t mbr[AWBOOT_RAUC_MBR_SECTOR_SIZE], uint64_t device_sector_count,
	struct awboot_rauc_mbr_layout *layout)
{
	static const uint8_t expected_type[] = {
		MBR_TYPE_FAT32_LBA,
		MBR_TYPE_FAT32_LBA,
		MBR_TYPE_BOOTSTATE,
	};
	uint64_t previous_end = 0U;

	if (!has_sector_signature(mbr)) {
		return AWBOOT_RAUC_MBR_BAD_SIGNATURE;
	}

	layout->disk_signature = read_le32(mbr + MBR_DISK_SIGNATURE_OFFSET);
	if (layout->disk_signature == 0U) {
		return AWBOOT_RAUC_MBR_ZERO_DISK_SIGNATURE;
	}

	for (unsigned int index = 0U; index < 4U; index++) {
		const struct mbr_entry entry = read_entry(mbr, index);
		struct awboot_mbr_partition *partition = &layout->partition[index];

		if (!valid_boot_flag(entry.boot_flag)) {
			return AWBOOT_RAUC_MBR_BAD_BOOT_FLAG;
		}
		if (index == 0U && entry.type == MBR_TYPE_GPT) {
			return AWBOOT_RAUC_MBR_GPT_PROTECTIVE;
		}
		if ((index < 3U && entry.type != expected_type[index]) ||
			(index == 3U && !extended_type(entry.type))) {
			return AWBOOT_RAUC_MBR_BAD_PRIMARY_TYPE;
		}
		if (entry.start_lba == 0U || entry.sector_count == 0U) {
			return AWBOOT_RAUC_MBR_EMPTY_PRIMARY;
		}
		if (!valid_range(entry.start_lba, entry.sector_count, device_sector_count)) {
			return AWBOOT_RAUC_MBR_PRIMARY_RANGE;
		}
		if (index != 0U && (uint64_t)entry.start_lba < previous_end) {
			return AWBOOT_RAUC_MBR_PRIMARY_ORDER;
		}

		partition->type         = entry.type;
		partition->start_lba    = entry.start_lba;
		partition->sector_count = entry.sector_count;
		previous_end            = partition->start_lba + partition->sector_count;
	}

	if (layout->partition[AWBOOT_RAUC_PART_BOOTSTATE - 1U].sector_count <
		AWBOOT_RAUC_BOOTSTATE_COPY_COUNT) {
		return AWBOOT_RAUC_MBR_BOOTSTATE_TOO_SMALL;
	}
	layout->bootstate_copy_lba[0] = layout->partition[AWBOOT_RAUC_PART_BOOTSTATE - 1U].start_lba;
	layout->bootstate_copy_lba[1] = layout->bootstate_copy_lba[0] + 1U;
	return AWBOOT_RAUC_MBR_OK;
}

static enum awboot_rauc_mbr_status parse_logicals(
	uint64_t device_sector_count, awboot_rauc_mbr_read_sector_fn read_sector,
	void *context, struct awboot_rauc_mbr_layout *layout)
{
	const struct awboot_mbr_partition *extended =
		&layout->partition[AWBOOT_RAUC_PART_EXTENDED - 1U];
	const uint64_t extended_end = extended->start_lba + extended->sector_count;
	uint64_t current_ebr = extended->start_lba;
	uint64_t previous_end = extended->start_lba;
	uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE];

	for (unsigned int logical = 0U; logical < AWBOOT_RAUC_MBR_LOGICAL_COUNT; logical++) {
		struct mbr_entry data;
		struct mbr_entry link;
		struct awboot_mbr_partition *partition;
		uint64_t absolute_start;

		if (!valid_range(current_ebr, 1U, device_sector_count) || current_ebr >= extended_end) {
			return AWBOOT_RAUC_MBR_LOGICAL_RANGE;
		}
		if (!read_sector(context, current_ebr, sector)) {
			return AWBOOT_RAUC_MBR_EBR_READ_FAILED;
		}
		if (!has_sector_signature(sector)) {
			return AWBOOT_RAUC_MBR_BAD_EBR_SIGNATURE;
		}

		data = read_entry(sector, 0U);
		link = read_entry(sector, 1U);
		if (!valid_boot_flag(data.boot_flag) || !valid_boot_flag(link.boot_flag)) {
			return AWBOOT_RAUC_MBR_BAD_BOOT_FLAG;
		}
		if (!entry_empty(sector, 2U) || !entry_empty(sector, 3U)) {
			return AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT;
		}
		if (data.type != MBR_TYPE_LINUX) {
			return AWBOOT_RAUC_MBR_BAD_LOGICAL_TYPE;
		}
		if (data.start_lba == 0U || data.sector_count == 0U) {
			return AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT;
		}

		absolute_start = current_ebr + (uint64_t)data.start_lba;
		if (absolute_start < current_ebr ||
			!valid_range(absolute_start, data.sector_count, device_sector_count) ||
			absolute_start < extended->start_lba ||
			data.sector_count > extended_end - absolute_start) {
			return AWBOOT_RAUC_MBR_LOGICAL_RANGE;
		}
		if (current_ebr < previous_end || absolute_start < previous_end) {
			return AWBOOT_RAUC_MBR_LOGICAL_ORDER;
		}

		layout->ebr_lba[logical] = current_ebr;
		partition = &layout->partition[AWBOOT_RAUC_PART_ROOTFS_A - 1U + logical];
		partition->type = data.type;
		partition->start_lba = absolute_start;
		partition->sector_count = data.sector_count;
		previous_end = absolute_start + data.sector_count;

		if (logical + 1U < AWBOOT_RAUC_MBR_LOGICAL_COUNT) {
			uint64_t next_ebr;

			if (!extended_type(link.type) || link.start_lba == 0U || link.sector_count == 0U) {
				return AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT;
			}
			next_ebr = extended->start_lba + (uint64_t)link.start_lba;
			if (next_ebr < extended->start_lba ||
				!valid_range(next_ebr, link.sector_count, extended_end) ||
				next_ebr < previous_end) {
				return AWBOOT_RAUC_MBR_LOGICAL_RANGE;
			}
			current_ebr = next_ebr;
		} else if (!entry_empty(sector, 1U)) {
			return AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT;
		}
	}

	return AWBOOT_RAUC_MBR_OK;
}

enum awboot_rauc_mbr_status awboot_rauc_mbr_parse(
	const uint8_t mbr[AWBOOT_RAUC_MBR_SECTOR_SIZE], uint64_t device_sector_count,
	awboot_rauc_mbr_read_sector_fn read_sector, void *context,
	struct awboot_rauc_mbr_layout *layout)
{
	enum awboot_rauc_mbr_status status;

	if (mbr == NULL || device_sector_count == 0U || read_sector == NULL || layout == NULL) {
		return AWBOOT_RAUC_MBR_INVALID_ARGUMENT;
	}

	memset(layout, 0, sizeof(*layout));
	status = parse_primaries(mbr, device_sector_count, layout);
	if (status != AWBOOT_RAUC_MBR_OK) {
		return status;
	}
	return parse_logicals(device_sector_count, read_sector, context, layout);
}

bool awboot_rauc_mbr_format_partuuid(uint32_t disk_signature, uint8_t partition_number,
									char output[AWBOOT_RAUC_MBR_PARTUUID_SIZE])
{
	static const char hex[] = "0123456789abcdef";

	if (disk_signature == 0U || partition_number == 0U || partition_number > 99U || output == NULL) {
		return false;
	}

	for (unsigned int index = 0U; index < 8U; index++) {
		const unsigned int shift = (7U - index) * 4U;
		output[index] = hex[(disk_signature >> shift) & 0x0fU];
	}
	output[8] = '-';
	output[9] = (char)('0' + partition_number / 10U);
	output[10] = (char)('0' + partition_number % 10U);
	output[11] = '\0';
	return true;
}
