#include "bootstate_storage.h"

static uint8_t copy0_sector[AWBOOT_BOOTSTATE_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t copy1_sector[AWBOOT_BOOTSTATE_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t write_sector[AWBOOT_BOOTSTATE_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t verify_sector[AWBOOT_BOOTSTATE_SECTOR_SIZE] __attribute__((aligned(4)));

static bool storage_valid(const struct awboot_bootstate_storage *storage)
{
	return storage != NULL && storage->read_sector != NULL && storage->write_sector != NULL && storage->sync != NULL &&
		   storage->copy_lba[0] != storage->copy_lba[1];
}

static bool sectors_equal(const uint8_t *first, const uint8_t *second)
{
	for (size_t index = 0; index < AWBOOT_BOOTSTATE_SECTOR_SIZE; ++index) {
		if (first[index] != second[index]) {
			return false;
		}
	}

	return true;
}

static bool states_equal(const struct awboot_bootstate *first, const struct awboot_bootstate *second)
{
	uint8_t first_record[AWBOOT_BOOTSTATE_RECORD_SIZE];
	uint8_t second_record[AWBOOT_BOOTSTATE_RECORD_SIZE];

	if (!awboot_bootstate_encode(first, first_record) || !awboot_bootstate_encode(second, second_record)) {
		return false;
	}

	for (size_t index = 0; index < AWBOOT_BOOTSTATE_RECORD_SIZE; ++index) {
		if (first_record[index] != second_record[index]) {
			return false;
		}
	}

	return true;
}

enum awboot_bootstate_storage_status awboot_bootstate_storage_load(const struct awboot_bootstate_storage *storage,
																   struct awboot_bootstate				 *state,
																   enum awboot_bootstate_source			 *source)
{
	struct awboot_bootstate decoded0;
	struct awboot_bootstate decoded1;
	bool					valid0;
	bool					valid1;

	if (!storage_valid(storage) || state == NULL || source == NULL) {
		return AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT;
	}
	if (!storage->read_sector(storage->context, storage->copy_lba[0], copy0_sector) ||
		!storage->read_sector(storage->context, storage->copy_lba[1], copy1_sector)) {
		return AWBOOT_BOOTSTATE_STORAGE_READ_FAILED;
	}

	valid0 = awboot_bootstate_decode(copy0_sector, AWBOOT_BOOTSTATE_RECORD_SIZE, &decoded0);
	valid1 = awboot_bootstate_decode(copy1_sector, AWBOOT_BOOTSTATE_RECORD_SIZE, &decoded1);
	if (!valid0 && !valid1) {
		return AWBOOT_BOOTSTATE_STORAGE_EMPTY;
	}

	*source = awboot_bootstate_newest_valid(copy0_sector, AWBOOT_BOOTSTATE_RECORD_SIZE, copy1_sector,
											AWBOOT_BOOTSTATE_RECORD_SIZE, state);
	if (*source == AWBOOT_BOOTSTATE_SOURCE_NONE) {
		return AWBOOT_BOOTSTATE_STORAGE_AMBIGUOUS;
	}

	return AWBOOT_BOOTSTATE_STORAGE_OK;
}

enum awboot_bootstate_storage_status awboot_bootstate_storage_commit(const struct awboot_bootstate_storage *storage,
																	 const struct awboot_bootstate		   *next_state,
																	 enum awboot_bootstate_source *written_source)
{
	struct awboot_bootstate				 current_state;
	struct awboot_bootstate				 verified_state;
	enum awboot_bootstate_source		 current_source;
	enum awboot_bootstate_source		 verify_source;
	enum awboot_bootstate_source		 target_source;
	enum awboot_bootstate_storage_status status;
	uint64_t							 target_lba;

	if (!storage_valid(storage) || next_state == NULL || written_source == NULL) {
		return AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT;
	}

	status = awboot_bootstate_storage_load(storage, &current_state, &current_source);
	if (status == AWBOOT_BOOTSTATE_STORAGE_EMPTY) {
		if (next_state->generation != 0U) {
			return AWBOOT_BOOTSTATE_STORAGE_STALE_UPDATE;
		}
		target_source = AWBOOT_BOOTSTATE_SOURCE_COPY0;
	} else if (status == AWBOOT_BOOTSTATE_STORAGE_OK) {
		if (next_state->generation != current_state.generation + 1U) {
			return AWBOOT_BOOTSTATE_STORAGE_STALE_UPDATE;
		}
		target_source = current_source == AWBOOT_BOOTSTATE_SOURCE_COPY0 ? AWBOOT_BOOTSTATE_SOURCE_COPY1
																		: AWBOOT_BOOTSTATE_SOURCE_COPY0;
	} else {
		return status;
	}

	for (size_t index = 0; index < AWBOOT_BOOTSTATE_SECTOR_SIZE; ++index) {
		write_sector[index] = 0U;
	}
	if (!awboot_bootstate_encode(next_state, write_sector)) {
		return AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT;
	}

	target_lba = storage->copy_lba[target_source == AWBOOT_BOOTSTATE_SOURCE_COPY0 ? 0 : 1];
	if (!storage->write_sector(storage->context, target_lba, write_sector)) {
		return AWBOOT_BOOTSTATE_STORAGE_WRITE_FAILED;
	}
	if (!storage->sync(storage->context)) {
		return AWBOOT_BOOTSTATE_STORAGE_SYNC_FAILED;
	}
	if (!storage->read_sector(storage->context, target_lba, verify_sector) ||
		!sectors_equal(write_sector, verify_sector)) {
		return AWBOOT_BOOTSTATE_STORAGE_VERIFY_FAILED;
	}

	status = awboot_bootstate_storage_load(storage, &verified_state, &verify_source);
	if (status != AWBOOT_BOOTSTATE_STORAGE_OK || verify_source != target_source ||
		!states_equal(next_state, &verified_state)) {
		return AWBOOT_BOOTSTATE_STORAGE_VERIFY_FAILED;
	}

	*written_source = target_source;
	return AWBOOT_BOOTSTATE_STORAGE_OK;
}
