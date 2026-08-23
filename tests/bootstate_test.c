#include "bootstate.h"
#include "bootstate_storage.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct awboot_bootstate default_state(void)
{
	return (struct awboot_bootstate){
		.generation		 = 7U,
		.active_slot	 = AWBOOT_BOOTSTATE_SLOT_A,
		.priority		 = {20U, 10U},
		.tries_remaining = { 3U,	2U},
		.successful		 = { 1U,	0U},
	};
}

static void encode(const struct awboot_bootstate *state, uint8_t record[AWBOOT_BOOTSTATE_RECORD_SIZE])
{
	assert(awboot_bootstate_encode(state, record));
}

static void refresh_crc(uint8_t record[AWBOOT_BOOTSTATE_RECORD_SIZE])
{
	const uint32_t crc						 = awboot_bootstate_crc32(record, AWBOOT_BOOTSTATE_CRC_OFFSET);
	record[AWBOOT_BOOTSTATE_CRC_OFFSET]		 = (uint8_t)crc;
	record[AWBOOT_BOOTSTATE_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8U);
	record[AWBOOT_BOOTSTATE_CRC_OFFSET + 2U] = (uint8_t)(crc >> 16U);
	record[AWBOOT_BOOTSTATE_CRC_OFFSET + 3U] = (uint8_t)(crc >> 24U);
}

static void assert_state_equal(const struct awboot_bootstate *expected, const struct awboot_bootstate *actual)
{
	assert(expected->generation == actual->generation);
	assert(expected->active_slot == actual->active_slot);
	assert(memcmp(expected->priority, actual->priority, sizeof(expected->priority)) == 0);
	assert(memcmp(expected->tries_remaining, actual->tries_remaining, sizeof(expected->tries_remaining)) == 0);
	assert(memcmp(expected->successful, actual->successful, sizeof(expected->successful)) == 0);
}

static void test_encode_decode_round_trip(void)
{
	const struct awboot_bootstate expected = default_state();
	struct awboot_bootstate		  actual;
	uint8_t						  record[AWBOOT_BOOTSTATE_RECORD_SIZE];

	encode(&expected, record);
	assert(record[0] == 'A');
	assert(record[1] == 'W');
	assert(record[2] == 'B');
	assert(record[3] == 'S');
	assert(record[4] == AWBOOT_BOOTSTATE_VERSION);
	assert(record[6] == AWBOOT_BOOTSTATE_RECORD_SIZE);
	assert(awboot_bootstate_decode(record, sizeof(record), &actual));
	assert_state_equal(&expected, &actual);
	assert(!awboot_bootstate_decode(record, sizeof(record) - 1U, &actual));
	assert(!awboot_bootstate_decode(record, sizeof(record) + 1U, &actual));

	for (size_t index = 0; index < AWBOOT_BOOTSTATE_RECORD_SIZE; ++index) {
		uint8_t corrupt[AWBOOT_BOOTSTATE_RECORD_SIZE];
		memcpy(corrupt, record, sizeof(corrupt));
		corrupt[index] ^= 1U;
		assert(!awboot_bootstate_decode(corrupt, sizeof(corrupt), &actual));
	}

	assert(!awboot_bootstate_decode(NULL, sizeof(record), &actual));
	assert(!awboot_bootstate_decode(record, sizeof(record), NULL));
}

static void test_semantic_validation(void)
{
	struct awboot_bootstate state = default_state();
	struct awboot_bootstate decoded;
	uint8_t					record[AWBOOT_BOOTSTATE_RECORD_SIZE];

	state.active_slot = AWBOOT_BOOTSTATE_SLOT_COUNT;
	assert(!awboot_bootstate_encode(&state, record));
	state									  = default_state();
	state.successful[AWBOOT_BOOTSTATE_SLOT_B] = 2U;
	assert(!awboot_bootstate_encode(&state, record));
	assert(!awboot_bootstate_encode(NULL, record));
	assert(!awboot_bootstate_encode(&state, NULL));

	state = default_state();
	encode(&state, record);
	record[12] = AWBOOT_BOOTSTATE_SLOT_COUNT;
	refresh_crc(record);
	assert(!awboot_bootstate_decode(record, sizeof(record), &decoded));

	encode(&state, record);
	record[17] = 2U;
	refresh_crc(record);
	assert(!awboot_bootstate_decode(record, sizeof(record), &decoded));

	encode(&state, record);
	record[19] = 1U;
	refresh_crc(record);
	assert(!awboot_bootstate_decode(record, sizeof(record), &decoded));
}

static void test_newest_valid_record(void)
{
	struct awboot_bootstate state0 = default_state();
	struct awboot_bootstate state1 = default_state();
	struct awboot_bootstate actual;
	uint8_t					copy0[AWBOOT_BOOTSTATE_RECORD_SIZE];
	uint8_t					copy1[AWBOOT_BOOTSTATE_RECORD_SIZE];

	state0.generation  = 10U;
	state1.generation  = 11U;
	state1.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	encode(&state0, copy0);
	encode(&state1, copy1);
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), &actual) ==
		   AWBOOT_BOOTSTATE_SOURCE_COPY1);
	assert_state_equal(&state1, &actual);

	copy1[0] ^= 1U;
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), &actual) ==
		   AWBOOT_BOOTSTATE_SOURCE_COPY0);
	assert_state_equal(&state0, &actual);

	copy0[0] ^= 1U;
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), &actual) ==
		   AWBOOT_BOOTSTATE_SOURCE_NONE);

	state0.generation = 42U;
	state1			  = state0;
	encode(&state0, copy0);
	encode(&state1, copy1);
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), &actual) ==
		   AWBOOT_BOOTSTATE_SOURCE_COPY0);

	state1.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	encode(&state1, copy1);
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), &actual) ==
		   AWBOOT_BOOTSTATE_SOURCE_NONE);

	state0.generation = 0xfffffff0U;
	state1.generation = 0x00000010U;
	encode(&state0, copy0);
	encode(&state1, copy1);
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), &actual) ==
		   AWBOOT_BOOTSTATE_SOURCE_COPY1);

	state0.generation = 0U;
	state1.generation = 0x80000000U;
	encode(&state0, copy0);
	encode(&state1, copy1);
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), &actual) ==
		   AWBOOT_BOOTSTATE_SOURCE_NONE);
	assert(awboot_bootstate_newest_valid(copy0, sizeof(copy0), copy1, sizeof(copy1), NULL) ==
		   AWBOOT_BOOTSTATE_SOURCE_NONE);
}

struct fake_storage {
	uint64_t	 lba[2];
	uint8_t		 sector[2][AWBOOT_BOOTSTATE_SECTOR_SIZE];
	uint8_t		 pending_sector[AWBOOT_BOOTSTATE_SECTOR_SIZE];
	bool		 read_ok[2];
	bool		 write_ok;
	bool		 sync_ok;
	bool		 pending;
	int			 pending_index;
	size_t		 write_bytes;
	unsigned int writes;
	unsigned int syncs;
};

static int fake_index(const struct fake_storage *fake, uint64_t lba)
{
	if (lba == fake->lba[0]) {
		return 0;
	}
	if (lba == fake->lba[1]) {
		return 1;
	}
	return -1;
}

static bool fake_read(void *context, uint64_t lba, uint8_t sector[AWBOOT_BOOTSTATE_SECTOR_SIZE])
{
	struct fake_storage *fake  = context;
	const int			 index = fake_index(fake, lba);

	if (index < 0 || !fake->read_ok[index]) {
		return false;
	}
	if (fake->pending && index == fake->pending_index) {
		memcpy(sector, fake->pending_sector, AWBOOT_BOOTSTATE_SECTOR_SIZE);
		return true;
	}
	memcpy(sector, fake->sector[index], AWBOOT_BOOTSTATE_SECTOR_SIZE);
	return true;
}

static bool fake_write(void *context, uint64_t lba, const uint8_t sector[AWBOOT_BOOTSTATE_SECTOR_SIZE])
{
	struct fake_storage *fake  = context;
	const int			 index = fake_index(fake, lba);

	if (index < 0) {
		return false;
	}
	fake->writes++;
	if (!fake->write_ok) {
		memcpy(fake->sector[index], sector, fake->write_bytes);
		return false;
	}

	memcpy(fake->pending_sector, fake->sector[index], AWBOOT_BOOTSTATE_SECTOR_SIZE);
	memcpy(fake->pending_sector, sector, fake->write_bytes);
	fake->pending		= true;
	fake->pending_index = index;
	return true;
}

static bool fake_sync(void *context)
{
	struct fake_storage *fake = context;

	fake->syncs++;
	if (!fake->sync_ok) {
		return false;
	}
	if (fake->pending) {
		memcpy(fake->sector[fake->pending_index], fake->pending_sector, AWBOOT_BOOTSTATE_SECTOR_SIZE);
		fake->pending = false;
	}
	return true;
}

static void fake_power_loss(struct fake_storage *fake)
{
	fake->pending = false;
}

static struct fake_storage empty_fake_storage(void)
{
	struct fake_storage fake = {
		.lba		   = {100U, 200U},
		.read_ok	   = {true, true},
		.write_ok	   = true,
		.sync_ok	   = true,
		.pending_index = -1,
		.write_bytes   = AWBOOT_BOOTSTATE_SECTOR_SIZE,
	};
	return fake;
}

static struct awboot_bootstate_storage storage_for(struct fake_storage *fake)
{
	return (struct awboot_bootstate_storage){
		.context	  = fake,
		.read_sector  = fake_read,
		.write_sector = fake_write,
		.sync		  = fake_sync,
		.copy_lba	  = {fake->lba[0], fake->lba[1]},
	};
}

static void test_storage_commit_and_alternate(void)
{
	struct fake_storage				fake	= empty_fake_storage();
	struct awboot_bootstate_storage storage = storage_for(&fake);
	struct awboot_bootstate			state	= default_state();
	struct awboot_bootstate			loaded;
	enum awboot_bootstate_source	source;

	assert(awboot_bootstate_storage_load(&storage, &loaded, &source) == AWBOOT_BOOTSTATE_STORAGE_EMPTY);

	state.generation = 0U;
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY0);
	assert(fake.writes == 1U);
	assert(fake.syncs == 1U);
	assert(awboot_bootstate_storage_load(&storage, &loaded, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY0);
	assert_state_equal(&state, &loaded);

	state.generation++;
	state.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY1);
	assert(fake.writes == 2U);
	assert(fake.syncs == 2U);
	assert(awboot_bootstate_storage_load(&storage, &loaded, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY1);
	assert_state_equal(&state, &loaded);

	state.generation++;
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY0);
	assert(fake.writes == 3U);
	assert(fake.syncs == 3U);
}

static void test_storage_rejects_stale_and_ambiguous_state(void)
{
	struct fake_storage				fake	= empty_fake_storage();
	struct awboot_bootstate_storage storage = storage_for(&fake);
	struct awboot_bootstate			state	= default_state();
	struct awboot_bootstate			different;
	struct awboot_bootstate			loaded;
	enum awboot_bootstate_source	source;

	state.generation = 0U;
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_STALE_UPDATE);
	assert(fake.writes == 1U);

	different			  = state;
	different.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	encode(&different, fake.sector[1]);
	assert(awboot_bootstate_storage_load(&storage, &loaded, &source) == AWBOOT_BOOTSTATE_STORAGE_AMBIGUOUS);
	different.generation++;
	assert(awboot_bootstate_storage_commit(&storage, &different, &source) == AWBOOT_BOOTSTATE_STORAGE_AMBIGUOUS);
	assert(fake.writes == 1U);
}

static void test_storage_preserves_old_copy_on_failed_write(void)
{
	struct fake_storage				fake	= empty_fake_storage();
	struct awboot_bootstate_storage storage = storage_for(&fake);
	struct awboot_bootstate			state	= default_state();
	struct awboot_bootstate			next;
	struct awboot_bootstate			loaded;
	enum awboot_bootstate_source	source;

	state.generation = 0U;
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	next = state;
	next.generation++;
	next.active_slot = AWBOOT_BOOTSTATE_SLOT_B;

	fake.write_ok	 = false;
	fake.write_bytes = 31U;
	assert(awboot_bootstate_storage_commit(&storage, &next, &source) == AWBOOT_BOOTSTATE_STORAGE_WRITE_FAILED);
	assert(awboot_bootstate_storage_load(&storage, &loaded, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY0);
	assert_state_equal(&state, &loaded);

	fake.write_ok	 = true;
	fake.write_bytes = 31U;
	assert(awboot_bootstate_storage_commit(&storage, &next, &source) == AWBOOT_BOOTSTATE_STORAGE_VERIFY_FAILED);
	assert(awboot_bootstate_storage_load(&storage, &loaded, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY0);
	assert_state_equal(&state, &loaded);
}

static void test_storage_requires_durable_sync(void)
{
	struct fake_storage				fake	= empty_fake_storage();
	struct awboot_bootstate_storage storage = storage_for(&fake);
	struct awboot_bootstate			state	= default_state();
	struct awboot_bootstate			next;
	struct awboot_bootstate			loaded;
	enum awboot_bootstate_source	source;

	state.generation = 0U;
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	next = state;
	next.generation++;
	next.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	fake.sync_ok	 = false;
	assert(awboot_bootstate_storage_commit(&storage, &next, &source) == AWBOOT_BOOTSTATE_STORAGE_SYNC_FAILED);
	assert(fake.pending);

	fake_power_loss(&fake);
	assert(awboot_bootstate_storage_load(&storage, &loaded, &source) == AWBOOT_BOOTSTATE_STORAGE_OK);
	assert(source == AWBOOT_BOOTSTATE_SOURCE_COPY0);
	assert_state_equal(&state, &loaded);
}

static void test_storage_rejects_read_errors_and_bad_arguments(void)
{
	struct fake_storage				fake	= empty_fake_storage();
	struct awboot_bootstate_storage storage = storage_for(&fake);
	struct awboot_bootstate			state	= default_state();
	enum awboot_bootstate_source	source;

	fake.read_ok[1] = false;
	assert(awboot_bootstate_storage_load(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_READ_FAILED);
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_READ_FAILED);

	assert(awboot_bootstate_storage_load(NULL, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT);
	assert(awboot_bootstate_storage_load(&storage, NULL, &source) == AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT);
	assert(awboot_bootstate_storage_load(&storage, &state, NULL) == AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT);
	storage.copy_lba[1] = storage.copy_lba[0];
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT);
	storage		 = storage_for(&fake);
	storage.sync = NULL;
	assert(awboot_bootstate_storage_commit(&storage, &state, &source) == AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT);
}

static void test_crc32_standard_vector(void)
{
	static const uint8_t vector[] = "123456789";
	assert(awboot_bootstate_crc32(vector, sizeof(vector) - 1U) == 0xcbf43926U);
}

static void test_slot_selection(void)
{
	struct awboot_bootstate state = default_state();
	struct awboot_bootstate next;
	uint8_t					selected = 0xffU;

	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_CONFIRMED);
	assert(selected == AWBOOT_BOOTSTATE_SLOT_A);
	assert_state_equal(&state, &next);

	state.priority[AWBOOT_BOOTSTATE_SLOT_B] = 30U;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_TRIAL);
	assert(selected == AWBOOT_BOOTSTATE_SLOT_B);
	assert(next.active_slot == AWBOOT_BOOTSTATE_SLOT_B);
	assert(next.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == 1U);
	assert(next.generation == state.generation + 1U);
	assert(state.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == 2U);

	state.priority[AWBOOT_BOOTSTATE_SLOT_A] = 30U;
	state.active_slot						= AWBOOT_BOOTSTATE_SLOT_A;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_CONFIRMED);
	assert(selected == AWBOOT_BOOTSTATE_SLOT_A);
	state.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_TRIAL);
	assert(selected == AWBOOT_BOOTSTATE_SLOT_B);

	state = default_state();
	state.active_slot = AWBOOT_BOOTSTATE_SLOT_B;
	state.priority[AWBOOT_BOOTSTATE_SLOT_B] = 30U;
	state.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] = 0U;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_CONFIRMED);
	assert(selected == AWBOOT_BOOTSTATE_SLOT_A);
	assert(next.active_slot == AWBOOT_BOOTSTATE_SLOT_A);
	assert(next.generation == state.generation + 1U);

	state										   = default_state();
	state.priority[AWBOOT_BOOTSTATE_SLOT_A]		   = 0U;
	state.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] = 1U;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_TRIAL);
	assert(next.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == 0U);

	state.generation = 0xffffffffU;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_TRIAL);
	assert(next.generation == 0U);

	state.priority[AWBOOT_BOOTSTATE_SLOT_B] = 0U;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_NONE);
	assert(awboot_bootstate_select(NULL, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_NONE);
	assert(awboot_bootstate_select(&state, NULL, &next) == AWBOOT_BOOTSTATE_SELECTION_NONE);
	assert(awboot_bootstate_select(&state, &selected, NULL) == AWBOOT_BOOTSTATE_SELECTION_NONE);
}

static void test_rauc_callback_order(void)
{
	struct awboot_bootstate state = default_state();
	struct awboot_bootstate next;
	uint8_t selected;

	/* RAUC disables inactive B before writing the grouped boot/rootfs/appfs images. */
	assert(awboot_bootstate_set_successful(&state, AWBOOT_BOOTSTATE_SLOT_B, false, &next));
	assert(next.generation == state.generation + 1U);
	assert(next.active_slot == AWBOOT_BOOTSTATE_SLOT_A);
	assert(next.priority[AWBOOT_BOOTSTATE_SLOT_B] == 0U);
	assert(next.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == 0U);
	assert(next.successful[AWBOOT_BOOTSTATE_SLOT_B] == 0U);
	state = next;

	/* After all image writes, set-primary arms B without invalidating good A. */
	assert(awboot_bootstate_set_primary(&state, AWBOOT_BOOTSTATE_SLOT_B, &next));
	assert(next.generation == state.generation + 1U);
	assert(next.active_slot == AWBOOT_BOOTSTATE_SLOT_B);
	assert(next.priority[AWBOOT_BOOTSTATE_SLOT_B] == AWBOOT_BOOTSTATE_PRIMARY_PRIORITY);
	assert(next.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == AWBOOT_BOOTSTATE_DEFAULT_TRIES);
	assert(next.successful[AWBOOT_BOOTSTATE_SLOT_B] == 0U);
	assert(next.priority[AWBOOT_BOOTSTATE_SLOT_A] == AWBOOT_BOOTSTATE_FALLBACK_PRIORITY);
	assert(next.successful[AWBOOT_BOOTSTATE_SLOT_A] == 1U);
	state = next;

	/* AWBoot consumes one B attempt before Linux starts. */
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_TRIAL);
	assert(selected == AWBOOT_BOOTSTATE_SLOT_B);
	assert(next.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == AWBOOT_BOOTSTATE_DEFAULT_TRIES - 1U);
	state = next;

	/* Product health confirmation makes B indefinitely bootable and resets attempts. */
	assert(awboot_bootstate_set_successful(&state, AWBOOT_BOOTSTATE_SLOT_B, true, &next));
	assert(next.generation == state.generation + 1U);
	assert(next.successful[AWBOOT_BOOTSTATE_SLOT_B] == 1U);
	assert(next.tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] == AWBOOT_BOOTSTATE_DEFAULT_TRIES);
}

static void test_rauc_bad_active_falls_back(void)
{
	struct awboot_bootstate state = default_state();
	struct awboot_bootstate next;
	uint8_t selected;

	assert(awboot_bootstate_set_primary(&state, AWBOOT_BOOTSTATE_SLOT_B, &next));
	state = next;
	assert(awboot_bootstate_set_successful(&state, AWBOOT_BOOTSTATE_SLOT_B, false, &next));
	state = next;
	assert(awboot_bootstate_select(&state, &selected, &next) == AWBOOT_BOOTSTATE_SELECTION_CONFIRMED);
	assert(selected == AWBOOT_BOOTSTATE_SLOT_A);
	assert(next.active_slot == AWBOOT_BOOTSTATE_SLOT_A);
}

int main(void)
{
	test_crc32_standard_vector();
	test_encode_decode_round_trip();
	test_semantic_validation();
	test_newest_valid_record();
	test_slot_selection();
	test_rauc_callback_order();
	test_rauc_bad_active_falls_back();
	test_storage_commit_and_alternate();
	test_storage_rejects_stale_and_ambiguous_state();
	test_storage_preserves_old_copy_on_failed_write();
	test_storage_requires_durable_sync();
	test_storage_rejects_read_errors_and_bad_arguments();
	puts("bootstate tests passed");
	return 0;
}
