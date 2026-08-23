#include "rauc_mbr.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define TEST_DEVICE_SECTORS 3325952U
#define MBR_TABLE_OFFSET    446U
#define MBR_ENTRY_SIZE      16U

struct test_disk {
	uint8_t mbr[AWBOOT_RAUC_MBR_SECTOR_SIZE];
	uint8_t ebr[AWBOOT_RAUC_MBR_LOGICAL_COUNT][AWBOOT_RAUC_MBR_SECTOR_SIZE];
	uint64_t ebr_lba[AWBOOT_RAUC_MBR_LOGICAL_COUNT];
	bool read_ok[AWBOOT_RAUC_MBR_LOGICAL_COUNT];
};

static void write_le32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8U);
	bytes[2] = (uint8_t)(value >> 16U);
	bytes[3] = (uint8_t)(value >> 24U);
}

static void set_signature(uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE])
{
	sector[510] = 0x55U;
	sector[511] = 0xaaU;
}

static void set_entry(uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE], unsigned int index,
					  uint8_t boot_flag, uint8_t type, uint32_t start_lba, uint32_t sector_count)
{
	uint8_t *entry = sector + MBR_TABLE_OFFSET + index * MBR_ENTRY_SIZE;

	memset(entry, 0, MBR_ENTRY_SIZE);
	entry[0] = boot_flag;
	entry[4] = type;
	write_le32(entry + 8U, start_lba);
	write_le32(entry + 12U, sector_count);
}

static bool read_sector(void *context, uint64_t lba, uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE])
{
	struct test_disk *disk = context;

	for (unsigned int index = 0U; index < AWBOOT_RAUC_MBR_LOGICAL_COUNT; index++) {
		if (disk->ebr_lba[index] == lba) {
			if (!disk->read_ok[index]) {
				return false;
			}
			memcpy(sector, disk->ebr[index], AWBOOT_RAUC_MBR_SECTOR_SIZE);
			return true;
		}
	}
	return false;
}

static struct test_disk valid_disk(void)
{
	struct test_disk disk = {0};
	static const uint32_t ebr_lba[] = {
		147454U,
		941952U,
		1736576U,
		2006912U,
		2277248U,
	};
	static const uint32_t logical_start[] = {
		147456U,
		942080U,
		1736704U,
		2007040U,
		2277376U,
	};
	static const uint32_t logical_size[] = {
		786432U,
		786432U,
		262144U,
		262144U,
		1048576U,
	};
	const uint32_t extended_start = 147454U;
	const uint32_t extended_size = TEST_DEVICE_SECTORS - extended_start;

	set_signature(disk.mbr);
	write_le32(disk.mbr + 440U, 0x12345678U);
	set_entry(disk.mbr, 0U, 0x80U, 0x0cU, 8192U, 65536U);
	set_entry(disk.mbr, 1U, 0x00U, 0x0cU, 73728U, 65536U);
	set_entry(disk.mbr, 2U, 0x00U, 0xdaU, 139264U, 256U);
	set_entry(disk.mbr, 3U, 0x00U, 0x0fU, extended_start, extended_size);

	for (unsigned int index = 0U; index < AWBOOT_RAUC_MBR_LOGICAL_COUNT; index++) {
		disk.ebr_lba[index] = ebr_lba[index];
		disk.read_ok[index] = true;
		set_signature(disk.ebr[index]);
		set_entry(disk.ebr[index], 0U, 0x00U, 0x83U,
				  logical_start[index] - ebr_lba[index], logical_size[index]);
		if (index + 1U < AWBOOT_RAUC_MBR_LOGICAL_COUNT) {
			const uint32_t next_relative_ebr = ebr_lba[index + 1U] - extended_start;
			set_entry(disk.ebr[index], 1U, 0x00U, 0x0fU,
					  next_relative_ebr,
					  logical_start[index + 1U] + logical_size[index + 1U] - ebr_lba[index + 1U]);
		}
	}
	return disk;
}

static enum awboot_rauc_mbr_status parse(struct test_disk *disk, struct awboot_rauc_mbr_layout *layout)
{
	return awboot_rauc_mbr_parse(disk->mbr, TEST_DEVICE_SECTORS, read_sector, disk, layout);
}

static void test_valid_layout(void)
{
	struct test_disk disk = valid_disk();
	struct awboot_rauc_mbr_layout layout;
	char partuuid[AWBOOT_RAUC_MBR_PARTUUID_SIZE];

	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_OK);
	assert(layout.disk_signature == 0x12345678U);
	assert(layout.partition[AWBOOT_RAUC_PART_BOOT_A - 1U].start_lba == 8192U);
	assert(layout.partition[AWBOOT_RAUC_PART_BOOT_B - 1U].start_lba == 73728U);
	assert(layout.bootstate_copy_lba[0] == 139264U);
	assert(layout.bootstate_copy_lba[1] == 139265U);

	for (unsigned int logical = 0U; logical < AWBOOT_RAUC_MBR_LOGICAL_COUNT; logical++) {
		const struct awboot_mbr_partition *partition =
			&layout.partition[AWBOOT_RAUC_PART_ROOTFS_A - 1U + logical];
		assert(layout.ebr_lba[logical] == disk.ebr_lba[logical]);
		assert(partition->type == 0x83U);
	}
	assert(layout.partition[AWBOOT_RAUC_PART_ROOTFS_A - 1U].start_lba == 147456U);
	assert(layout.partition[AWBOOT_RAUC_PART_ROOTFS_B - 1U].start_lba == 942080U);
	assert(layout.partition[AWBOOT_RAUC_PART_APPFS_A - 1U].start_lba == 1736704U);
	assert(layout.partition[AWBOOT_RAUC_PART_APPFS_B - 1U].start_lba == 2007040U);
	assert(layout.partition[AWBOOT_RAUC_PART_DATA - 1U].start_lba == 2277376U);
	assert(layout.partition[AWBOOT_RAUC_PART_DATA - 1U].start_lba +
		   layout.partition[AWBOOT_RAUC_PART_DATA - 1U].sector_count == TEST_DEVICE_SECTORS);

	assert(awboot_rauc_mbr_format_partuuid(layout.disk_signature,
										AWBOOT_RAUC_PART_ROOTFS_A, partuuid));
	assert(strcmp(partuuid, "12345678-05") == 0);
	assert(awboot_rauc_mbr_format_partuuid(layout.disk_signature,
										AWBOOT_RAUC_PART_ROOTFS_B, partuuid));
	assert(strcmp(partuuid, "12345678-06") == 0);
}

static void test_primary_rejections(void)
{
	struct test_disk disk = valid_disk();
	struct awboot_rauc_mbr_layout layout;

	assert(awboot_rauc_mbr_parse(NULL, TEST_DEVICE_SECTORS, read_sector, &disk, &layout) ==
		   AWBOOT_RAUC_MBR_INVALID_ARGUMENT);
	assert(awboot_rauc_mbr_parse(disk.mbr, 0U, read_sector, &disk, &layout) ==
		   AWBOOT_RAUC_MBR_INVALID_ARGUMENT);
	assert(awboot_rauc_mbr_parse(disk.mbr, TEST_DEVICE_SECTORS, NULL, &disk, &layout) ==
		   AWBOOT_RAUC_MBR_INVALID_ARGUMENT);
	assert(awboot_rauc_mbr_parse(disk.mbr, TEST_DEVICE_SECTORS, read_sector, &disk, NULL) ==
		   AWBOOT_RAUC_MBR_INVALID_ARGUMENT);

	disk.mbr[510] = 0U;
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_SIGNATURE);
	disk = valid_disk();
	write_le32(disk.mbr + 440U, 0U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_ZERO_DISK_SIGNATURE);
	disk = valid_disk();
	set_entry(disk.mbr, 0U, 0x00U, 0xeeU, 1U, TEST_DEVICE_SECTORS - 1U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_GPT_PROTECTIVE);
	disk = valid_disk();
	disk.mbr[MBR_TABLE_OFFSET] = 0x01U;
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_BOOT_FLAG);
	disk = valid_disk();
	disk.mbr[MBR_TABLE_OFFSET + 4U] = 0x83U;
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_PRIMARY_TYPE);
	disk = valid_disk();
	set_entry(disk.mbr, 2U, 0x00U, 0xdaU, 139264U, 1U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BOOTSTATE_TOO_SMALL);
	disk = valid_disk();
	set_entry(disk.mbr, 1U, 0x00U, 0x0cU, 70000U, 65536U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_PRIMARY_ORDER);
	disk = valid_disk();
	set_entry(disk.mbr, 3U, 0x00U, 0x0fU, 150000U, TEST_DEVICE_SECTORS);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_PRIMARY_RANGE);
	disk = valid_disk();
	assert(awboot_rauc_mbr_parse(disk.mbr, TEST_DEVICE_SECTORS - 1U,
									 read_sector, &disk, &layout) == AWBOOT_RAUC_MBR_PRIMARY_RANGE);
}

static void test_ebr_rejections(void)
{
	struct test_disk disk = valid_disk();
	struct awboot_rauc_mbr_layout layout;

	disk.read_ok[0] = false;
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_EBR_READ_FAILED);
	disk = valid_disk();
	disk.ebr[0][510] = 0U;
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_EBR_SIGNATURE);
	disk = valid_disk();
	disk.ebr[0][MBR_TABLE_OFFSET + 4U] = 0x0cU;
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_LOGICAL_TYPE);
	disk = valid_disk();
	memset(disk.ebr[1] + MBR_TABLE_OFFSET + MBR_ENTRY_SIZE, 0, MBR_ENTRY_SIZE);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT);
	disk = valid_disk();
	set_entry(disk.ebr[1], 0U, 0x00U, 0x83U, 128U, 900000U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_LOGICAL_RANGE);
	disk = valid_disk();
	set_entry(disk.ebr[0], 1U, 0x00U, 0x0fU, 100000U, 1600000U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_LOGICAL_RANGE);
	disk = valid_disk();
	set_entry(disk.ebr[AWBOOT_RAUC_MBR_LOGICAL_COUNT - 1U], 1U,
			  0x00U, 0x0fU, 1500000U, 200000U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT);
	disk = valid_disk();
	set_entry(disk.ebr[0], 2U, 0x00U, 0x83U, 1U, 1U);
	assert(parse(&disk, &layout) == AWBOOT_RAUC_MBR_BAD_EBR_LAYOUT);
}

static void test_partuuid_rejections(void)
{
	char partuuid[AWBOOT_RAUC_MBR_PARTUUID_SIZE];

	assert(!awboot_rauc_mbr_format_partuuid(0U, 5U, partuuid));
	assert(!awboot_rauc_mbr_format_partuuid(0x12345678U, 0U, partuuid));
	assert(!awboot_rauc_mbr_format_partuuid(0x12345678U, 100U, partuuid));
	assert(!awboot_rauc_mbr_format_partuuid(0x12345678U, 5U, NULL));
}

static bool read_image_sector(void *context, uint64_t lba,
							  uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE])
{
	FILE *image = context;
	const uint64_t offset = lba * AWBOOT_RAUC_MBR_SECTOR_SIZE;

	if (lba > (uint64_t)LONG_MAX / AWBOOT_RAUC_MBR_SECTOR_SIZE ||
		fseek(image, (long)offset, SEEK_SET) != 0) {
		return false;
	}
	return fread(sector, 1U, AWBOOT_RAUC_MBR_SECTOR_SIZE, image) == AWBOOT_RAUC_MBR_SECTOR_SIZE;
}

static bool validate_image(const char *path)
{
	struct awboot_rauc_mbr_layout layout;
	uint8_t mbr[AWBOOT_RAUC_MBR_SECTOR_SIZE];
	FILE *image = fopen(path, "rb");
	long image_size;
	enum awboot_rauc_mbr_status status;

	if (image == NULL || fseek(image, 0L, SEEK_END) != 0 || (image_size = ftell(image)) <= 0L ||
		(image_size % AWBOOT_RAUC_MBR_SECTOR_SIZE) != 0L || fseek(image, 0L, SEEK_SET) != 0 ||
		fread(mbr, 1U, sizeof(mbr), image) != sizeof(mbr)) {
		if (image != NULL) {
			fclose(image);
		}
		return false;
	}

	status = awboot_rauc_mbr_parse(mbr, (uint64_t)image_size / AWBOOT_RAUC_MBR_SECTOR_SIZE,
								 read_image_sector, image, &layout);
	fclose(image);
	if (status != AWBOOT_RAUC_MBR_OK) {
		fprintf(stderr, "RAUC MBR image validation failed: status %d\n", status);
		return false;
	}
	printf("RAUC MBR image valid: disk %08x, rootfs p5=%llu p6=%llu\n",
		   layout.disk_signature,
		   (unsigned long long)layout.partition[AWBOOT_RAUC_PART_ROOTFS_A - 1U].start_lba,
		   (unsigned long long)layout.partition[AWBOOT_RAUC_PART_ROOTFS_B - 1U].start_lba);
	return true;
}

int main(int argc, char **argv)
{
	test_valid_layout();
	test_primary_rejections();
	test_ebr_rejections();
	test_partuuid_rejections();
	assert(argc == 1 || argc == 2);
	if (argc == 2) {
		assert(validate_image(argv[1]));
	}
	puts("RAUC MBR tests passed");
	return 0;
}
