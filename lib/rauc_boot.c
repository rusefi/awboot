#include "rauc_boot.h"

#include <stddef.h>
#include <string.h>

struct cmdline_builder {
	char *output;
	size_t length;
	bool valid;
};

static void append_char(struct cmdline_builder *builder, char value)
{
	if (!builder->valid || builder->length + 1U >= AWBOOT_RAUC_CMDLINE_SIZE) {
		builder->valid = false;
		return;
	}
	builder->output[builder->length++] = value;
	builder->output[builder->length] = '\0';
}

static void append_string(struct cmdline_builder *builder, const char *value)
{
	if (value == NULL) {
		builder->valid = false;
		return;
	}
	while (*value != '\0') {
		append_char(builder, *value++);
	}
}

static void append_u32(struct cmdline_builder *builder, uint32_t value)
{
	char digits[10];
	size_t count = 0U;

	do {
		digits[count++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0U);
	while (count > 0U) {
		append_char(builder, digits[--count]);
	}
}

enum awboot_rauc_boot_status awboot_rauc_boot_prepare(
	const struct awboot_rauc_boot_io *io, struct awboot_rauc_boot_result *result)
{
	uint8_t mbr[AWBOOT_RAUC_MBR_SECTOR_SIZE];
	struct awboot_bootstate loaded_state;
	struct awboot_bootstate next_state;
	struct awboot_bootstate_storage storage;
	enum awboot_bootstate_source source;
	enum awboot_bootstate_source written_source;

	if (io == NULL || io->sector_count == 0U || io->read_sector == NULL ||
		io->write_sector == NULL || io->sync == NULL || result == NULL) {
		return AWBOOT_RAUC_BOOT_INVALID_ARGUMENT;
	}

	memset(result, 0, sizeof(*result));
	result->mbr_status = AWBOOT_RAUC_MBR_INVALID_ARGUMENT;
	result->storage_status = AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT;
	if (!io->read_sector(io->context, 0U, mbr)) {
		return AWBOOT_RAUC_BOOT_MBR_READ_FAILED;
	}
	result->mbr_status = awboot_rauc_mbr_parse(mbr, io->sector_count,
											  io->read_sector, io->context, &result->layout);
	if (result->mbr_status != AWBOOT_RAUC_MBR_OK) {
		return AWBOOT_RAUC_BOOT_MBR_INVALID;
	}

	storage = (struct awboot_bootstate_storage){
		.context = io->context,
		.read_sector = io->read_sector,
		.write_sector = io->write_sector,
		.sync = io->sync,
		.copy_lba = {
			result->layout.bootstate_copy_lba[0],
			result->layout.bootstate_copy_lba[1],
		},
	};
	result->storage_status = awboot_bootstate_storage_load(&storage, &loaded_state, &source);
	if (result->storage_status != AWBOOT_BOOTSTATE_STORAGE_OK) {
		return AWBOOT_RAUC_BOOTSTATE_LOAD_FAILED;
	}

	result->selection = awboot_bootstate_select(&loaded_state, &result->selected_slot, &next_state);
	if (result->selection == AWBOOT_BOOTSTATE_SELECTION_NONE) {
		return AWBOOT_RAUC_BOOT_NO_BOOTABLE_SLOT;
	}
	result->fallback = result->selection == AWBOOT_BOOTSTATE_SELECTION_CONFIRMED &&
		result->selected_slot != loaded_state.active_slot;

	if (next_state.generation != loaded_state.generation) {
		result->storage_status = awboot_bootstate_storage_commit(&storage, &next_state, &written_source);
		if (result->storage_status != AWBOOT_BOOTSTATE_STORAGE_OK) {
			return AWBOOT_RAUC_BOOTSTATE_COMMIT_FAILED;
		}
	}
	result->state = next_state;

	if (!awboot_rauc_mbr_format_partuuid(result->layout.disk_signature,
			(uint8_t)(AWBOOT_RAUC_PART_ROOTFS_A + result->selected_slot), result->root_partuuid)) {
		return AWBOOT_RAUC_BOOT_PARTUUID_FAILED;
	}
	return AWBOOT_RAUC_BOOT_OK;
}

bool awboot_rauc_boot_watchdog_required(const struct awboot_rauc_boot_result *result)
{
	return result != NULL && result->selection == AWBOOT_BOOTSTATE_SELECTION_TRIAL;
}

bool awboot_rauc_boot_format_cmdline(const struct awboot_rauc_boot_result *result,
									 char output[AWBOOT_RAUC_CMDLINE_SIZE])
{
	struct cmdline_builder builder = {
		.output = output,
		.valid = output != NULL,
	};
	const char *reason;

	if (result == NULL || output == NULL || result->selected_slot >= AWBOOT_BOOTSTATE_SLOT_COUNT ||
		result->root_partuuid[0] == '\0') {
		return false;
	}
	output[0] = '\0';
	reason = result->fallback ? "fallback" :
		(result->selection == AWBOOT_BOOTSTATE_SELECTION_TRIAL ? "trial" : "normal");

	append_string(&builder, "quiet cma=32M root=PARTUUID=");
	append_string(&builder, result->root_partuuid);
	append_string(&builder,
		" ro rootwait init=/sbin/init console=ttyS0,115200 clk_ignore_unused random.trust_cpu=on rauc.slot=rootfs.");
	append_char(&builder, (char)('0' + result->selected_slot));
	append_string(&builder, " awboot.slot=");
	append_char(&builder, result->selected_slot == AWBOOT_BOOTSTATE_SLOT_A ? 'A' : 'B');
	append_string(&builder, " awboot.reason=");
	append_string(&builder, reason);
	append_string(&builder, " awboot.tries_remaining=");
	append_u32(&builder, result->state.tries_remaining[result->selected_slot]);
	append_string(&builder, " awboot.generation=");
	append_u32(&builder, result->state.generation);
	return builder.valid;
}
