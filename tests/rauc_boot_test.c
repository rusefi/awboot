#include "rauc_boot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_DEVICE_SECTORS 3325952U
#define TEST_SECTOR_COUNT   8U
#define MBR_TABLE_OFFSET    446U
#define MBR_ENTRY_SIZE      16U

struct test_disk {
	uint64_t lba[TEST_SECTOR_COUNT];
	uint8_t sector[TEST_SECTOR_COUNT][AWBOOT_RAUC_MBR_SECTOR_SIZE];
	bool read_ok[TEST_SECTOR_COUNT];
	bool write_ok;
	bool sync_ok;
	unsigned int writes;
	unsigned int syncs;
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
					  uint8_t type, uint32_t start_lba, uint32_t sector_count)
{
	uint8_t *entry = sector + MBR_TABLE_OFFSET + index * MBR_ENTRY_SIZE;

	memset(entry, 0, MBR_ENTRY_SIZE);
	entry[4] = type;
	write_le32(entry + 8U, start_lba);
	write_le32(entry + 12U, sector_count);
}

static int sector_index(const struct test_disk *disk, uint64_t lba)
{
	for (unsigned int index = 0U; index < TEST_SECTOR_COUNT; index++) {
		if (disk->lba[index] == lba) {
			return (int)index;
		}
	}
	return -1;
}

static bool read_sector(void *context, uint64_t lba, uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE])
{
	struct test_disk *disk = context;
	const int index = sector_index(disk, lba);

	if (index < 0 || !disk->read_ok[index]) {
		return false;
	}
	memcpy(sector, disk->sector[index], AWBOOT_RAUC_MBR_SECTOR_SIZE);
	return true;
}

static bool write_sector(void *context, uint64_t lba,
						 const uint8_t sector[AWBOOT_RAUC_MBR_SECTOR_SIZE])
{
	struct test_disk *disk = context;
	const int index = sector_index(disk, lba);

	if (index < 0 || !disk->write_ok) {
		return false;
	}
	disk->writes++;
	memcpy(disk->sector[index], sector, AWBOOT_RAUC_MBR_SECTOR_SIZE);
	return true;
}

static bool sync_storage(void *context)
{
	struct test_disk *disk = context;

	disk->syncs++;
	return disk->sync_ok;
}

static struct test_disk valid_disk(const struct awboot_bootstate *state)
{
	struct test_disk disk = {
		.lba = {0U, 147454U, 941952U, 1736576U, 2006912U, 2277248U, 139264U, 139265U},
		.read_ok = {true, true, true, true, true, true, true, true},
		.write_ok = true,
		.sync_ok = true,
	};
	static const uint32_t logical_start[] = {
		147456U, 942080U, 1736704U, 2007040U, 2277376U,
	};
	static const uint32_t logical_size[] = {
		786432U, 786432U, 262144U, 262144U, 1048576U,
	};

	set_signature(disk.sector[0]);
	write_le32(disk.sector[0] + 440U, 0x076c4a2aU);
	set_entry(disk.sector[0], 0U, 0x0cU, 8192U, 65536U);
	set_entry(disk.sector[0], 1U, 0x0cU, 73728U, 65536U);
	set_entry(disk.sector[0], 2U, 0xdaU, 139264U, 256U);
	set_entry(disk.sector[0], 3U, 0x0fU, 147454U, TEST_DEVICE_SECTORS - 147454U);

	for (unsigned int logical = 0U; logical < AWBOOT_RAUC_MBR_LOGICAL_COUNT; logical++) {
		const unsigned int sector = logical + 1U;

		set_signature(disk.sector[sector]);
		set_entry(disk.sector[sector], 0U, 0x83U,
				  logical_start[logical] - (uint32_t)disk.lba[sector], logical_size[logical]);
		if (logical + 1U < AWBOOT_RAUC_MBR_LOGICAL_COUNT) {
			const uint32_t next_ebr = (uint32_t)disk.lba[sector + 1U];
			set_entry(disk.sector[sector], 1U, 0x0fU, next_ebr - 147454U,
					  logical_start[logical + 1U] + logical_size[logical + 1U] - next_ebr);
		}
	}
	assert(awboot_bootstate_encode(state, disk.sector[6]));
	return disk;
}

static struct awboot_rauc_boot_io io_for(struct test_disk *disk)
{
	return (struct awboot_rauc_boot_io){
		.context = disk,
		.sector_count = TEST_DEVICE_SECTORS,
		.read_sector = read_sector,
		.write_sector = write_sector,
		.sync = sync_storage,
	};
}

static struct awboot_bootstate confirmed_a(void)
{
	return (struct awboot_bootstate){
		.generation = 10U,
		.active_slot = AWBOOT_BOOTSTATE_SLOT_A,
		.priority = {20U, 10U},
		.tries_remaining = {3U, 3U},
		.successful = {1U, 0U},
	};
}

static void test_confirmed_slot(void)
{
	const struct awboot_bootstate state = confirmed_a();
	struct test_disk disk = valid_disk(&state);
	const struct awboot_rauc_boot_io io = io_for(&disk);
	struct awboot_rauc_boot_result result;
	char cmdline[AWBOOT_RAUC_CMDLINE_SIZE];

	assert(awboot_rauc_boot_prepare(&io, &result) == AWBOOT_RAUC_BOOT_OK);
	assert(result.selected_slot == AWBOOT_BOOTSTATE_SLOT_A);
	assert(result.selection == AWBOOT_BOOTSTATE_SELECTION_CONFIRMED);
	assert(!result.fallback);
	assert(strcmp(result.root_partuuid, "076c4a2a-05") == 0);
	assert(disk.writes == 0U);
	assert(awboot_rauc_boot_format_cmdline(&result, cmdline));
	assert(strstr(cmdline, "root=PARTUUID=076c4a2a-05") != NULL);
	assert(strstr(cmdline, "rauc.slot=rootfs.0") != NULL);
	assert(strstr(cmdline, "awboot.reason=normal") != NULL);
}

static void test_trial_is_committed_before_boot(void)
{
	struct awboot_bootstate state = confirmed_a();
	struct awboot_rauc_boot_result result;
	char cmdline[AWBOOT_RAUC_CMDLINE_SIZE];

	state.priority[AWBOOT_BOOTSTATE_SLOT_B] = 30U;
	struct test_disk disk = valid_disk(&state);
	const struct awboot_rauc_boot_io io = io_for(&disk);
	assert(awboot_rauc_boot_prepare(&io, &result) == AWBOOT_RAUC_BOOT_OK);
	assert(result.selected_slot == AWBOOT_BOOTSTATE_SLOT_B);
	assert(result.selection == AWBOOT_BOOTSTATE_SELECTION_TRIAL);
	assert(result.state.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == 2U);
	assert(result.state.generation == 11U);
	assert(strcmp(result.root_partuuid, "076c4a2a-06") == 0);
	assert(disk.writes == 1U);
	assert(disk.syncs == 1U);
	assert(awboot_rauc_boot_format_cmdline(&result, cmdline));
	assert(strstr(cmdline, "awboot.reason=trial") != NULL);
	assert(strstr(cmdline, "awboot.tries_remaining=2") != NULL);
}

static void test_fallback_updates_active_slot(void)
{
	struct awboot_bootstate state = confirmed_a();
	struct awboot_rauc_boot_result result;
	char cmdline[AWBOOT_RAUC_CMDLINE_SIZE];

	state.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	state.priority[AWBOOT_BOOTSTATE_SLOT_B] = 30U;
	state.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] = 0U;
	struct test_disk disk = valid_disk(&state);
	const struct awboot_rauc_boot_io io = io_for(&disk);
	assert(awboot_rauc_boot_prepare(&io, &result) == AWBOOT_RAUC_BOOT_OK);
	assert(result.selected_slot == AWBOOT_BOOTSTATE_SLOT_A);
	assert(result.selection == AWBOOT_BOOTSTATE_SELECTION_CONFIRMED);
	assert(result.fallback);
	assert(result.state.active_slot == AWBOOT_BOOTSTATE_SLOT_A);
	assert(result.state.generation == 11U);
	assert(disk.writes == 1U);
	assert(awboot_rauc_boot_format_cmdline(&result, cmdline));
	assert(strstr(cmdline, "awboot.reason=fallback") != NULL);
}

static void test_fail_closed(void)
{
	struct awboot_bootstate state = confirmed_a();
	struct awboot_rauc_boot_result result;
	struct test_disk disk = valid_disk(&state);
	struct awboot_rauc_boot_io io = io_for(&disk);

	memset(disk.sector[6], 0, AWBOOT_RAUC_MBR_SECTOR_SIZE);
	assert(awboot_rauc_boot_prepare(&io, &result) == AWBOOT_RAUC_BOOTSTATE_LOAD_FAILED);
	state.priority[AWBOOT_BOOTSTATE_SLOT_A] = 0U;
	state.priority[AWBOOT_BOOTSTATE_SLOT_B] = 0U;
	disk = valid_disk(&state);
	io = io_for(&disk);
	assert(awboot_rauc_boot_prepare(&io, &result) == AWBOOT_RAUC_BOOT_NO_BOOTABLE_SLOT);
	state = confirmed_a();
	state.priority[AWBOOT_BOOTSTATE_SLOT_B] = 30U;
	disk = valid_disk(&state);
	disk.write_ok = false;
	io = io_for(&disk);
	assert(awboot_rauc_boot_prepare(&io, &result) == AWBOOT_RAUC_BOOTSTATE_COMMIT_FAILED);
}

int main(void)
{
	test_confirmed_slot();
	test_trial_is_committed_before_boot();
	test_fallback_updates_active_slot();
	test_fail_closed();
	puts("RAUC boot tests passed");
	return 0;
}
