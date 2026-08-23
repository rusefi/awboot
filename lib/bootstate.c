#include "bootstate.h"

#define BOOTSTATE_GENERATION_HALF_RANGE 0x80000000U
#define BOOTSTATE_RESERVED_OFFSET		19U

static uint16_t get_le16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t get_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void put_le16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8U);
}

static void put_le32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8U);
	bytes[2] = (uint8_t)(value >> 16U);
	bytes[3] = (uint8_t)(value >> 24U);
}

static bool fields_valid(const struct awboot_bootstate *state)
{
	if (state == NULL || state->active_slot >= AWBOOT_BOOTSTATE_SLOT_COUNT) {
		return false;
	}

	for (size_t slot = 0; slot < AWBOOT_BOOTSTATE_SLOT_COUNT; ++slot) {
		if (state->successful[slot] > 1U) {
			return false;
		}
	}

	return true;
}

static bool generation_newer(uint32_t candidate, uint32_t reference)
{
	const uint32_t delta = candidate - reference;
	return delta != 0U && delta < BOOTSTATE_GENERATION_HALF_RANGE;
}

static bool generation_ambiguous(uint32_t first, uint32_t second)
{
	return first - second == BOOTSTATE_GENERATION_HALF_RANGE;
}

static bool states_equal(const struct awboot_bootstate *first, const struct awboot_bootstate *second)
{
	if (first->generation != second->generation || first->active_slot != second->active_slot) {
		return false;
	}

	for (size_t slot = 0; slot < AWBOOT_BOOTSTATE_SLOT_COUNT; ++slot) {
		if (first->priority[slot] != second->priority[slot] ||
			first->tries_remaining[slot] != second->tries_remaining[slot] ||
			first->successful[slot] != second->successful[slot]) {
			return false;
		}
	}

	return true;
}

uint32_t awboot_bootstate_crc32(const uint8_t *bytes, size_t length)
{
	uint32_t crc = 0xffffffffU;

	if (bytes == NULL) {
		return 0U;
	}

	for (size_t index = 0; index < length; ++index) {
		crc ^= bytes[index];
		for (unsigned int bit = 0; bit < 8U; ++bit) {
			const uint32_t mask = 0U - (crc & 1U);
			crc					= (crc >> 1U) ^ (0xedb88320U & mask);
		}
	}

	return crc ^ 0xffffffffU;
}

bool awboot_bootstate_encode(const struct awboot_bootstate *state, uint8_t record[AWBOOT_BOOTSTATE_RECORD_SIZE])
{
	if (record == NULL || !fields_valid(state)) {
		return false;
	}

	for (size_t index = 0; index < AWBOOT_BOOTSTATE_RECORD_SIZE; ++index) {
		record[index] = 0U;
	}

	put_le32(record, AWBOOT_BOOTSTATE_MAGIC);
	put_le16(record + 4U, AWBOOT_BOOTSTATE_VERSION);
	put_le16(record + 6U, AWBOOT_BOOTSTATE_RECORD_SIZE);
	put_le32(record + 8U, state->generation);
	record[12] = state->active_slot;
	record[13] = state->priority[AWBOOT_BOOTSTATE_SLOT_A];
	record[14] = state->priority[AWBOOT_BOOTSTATE_SLOT_B];
	record[15] = state->tries_remaining[AWBOOT_BOOTSTATE_SLOT_A];
	record[16] = state->tries_remaining[AWBOOT_BOOTSTATE_SLOT_B];
	record[17] = state->successful[AWBOOT_BOOTSTATE_SLOT_A];
	record[18] = state->successful[AWBOOT_BOOTSTATE_SLOT_B];
	put_le32(record + AWBOOT_BOOTSTATE_CRC_OFFSET, awboot_bootstate_crc32(record, AWBOOT_BOOTSTATE_CRC_OFFSET));

	return true;
}

bool awboot_bootstate_decode(const uint8_t *record, size_t length, struct awboot_bootstate *state)
{
	if (record == NULL || state == NULL || length != AWBOOT_BOOTSTATE_RECORD_SIZE) {
		return false;
	}
	if (get_le32(record) != AWBOOT_BOOTSTATE_MAGIC || get_le16(record + 4U) != AWBOOT_BOOTSTATE_VERSION ||
		get_le16(record + 6U) != AWBOOT_BOOTSTATE_RECORD_SIZE) {
		return false;
	}
	if (get_le32(record + AWBOOT_BOOTSTATE_CRC_OFFSET) != awboot_bootstate_crc32(record, AWBOOT_BOOTSTATE_CRC_OFFSET)) {
		return false;
	}
	for (size_t index = BOOTSTATE_RESERVED_OFFSET; index < AWBOOT_BOOTSTATE_CRC_OFFSET; ++index) {
		if (record[index] != 0U) {
			return false;
		}
	}

	state->generation								= get_le32(record + 8U);
	state->active_slot								= record[12];
	state->priority[AWBOOT_BOOTSTATE_SLOT_A]		= record[13];
	state->priority[AWBOOT_BOOTSTATE_SLOT_B]		= record[14];
	state->tries_remaining[AWBOOT_BOOTSTATE_SLOT_A] = record[15];
	state->tries_remaining[AWBOOT_BOOTSTATE_SLOT_B] = record[16];
	state->successful[AWBOOT_BOOTSTATE_SLOT_A]		= record[17];
	state->successful[AWBOOT_BOOTSTATE_SLOT_B]		= record[18];

	return fields_valid(state);
}

enum awboot_bootstate_source awboot_bootstate_newest_valid(const uint8_t *copy0, size_t copy0_length,
														   const uint8_t *copy1, size_t copy1_length,
														   struct awboot_bootstate *state)
{
	struct awboot_bootstate state0;
	struct awboot_bootstate state1;
	const bool				valid0 = awboot_bootstate_decode(copy0, copy0_length, &state0);
	const bool				valid1 = awboot_bootstate_decode(copy1, copy1_length, &state1);

	if (state == NULL || (!valid0 && !valid1)) {
		return AWBOOT_BOOTSTATE_SOURCE_NONE;
	}
	if (!valid0) {
		*state = state1;
		return AWBOOT_BOOTSTATE_SOURCE_COPY1;
	}
	if (!valid1) {
		*state = state0;
		return AWBOOT_BOOTSTATE_SOURCE_COPY0;
	}
	if (state1.generation == state0.generation) {
		if (!states_equal(&state0, &state1)) {
			return AWBOOT_BOOTSTATE_SOURCE_NONE;
		}
		*state = state0;
		return AWBOOT_BOOTSTATE_SOURCE_COPY0;
	}
	if (generation_ambiguous(state1.generation, state0.generation)) {
		return AWBOOT_BOOTSTATE_SOURCE_NONE;
	}
	if (!generation_newer(state1.generation, state0.generation)) {
		*state = state0;
		return AWBOOT_BOOTSTATE_SOURCE_COPY0;
	}

	*state = state1;
	return AWBOOT_BOOTSTATE_SOURCE_COPY1;
}

enum awboot_bootstate_selection awboot_bootstate_select(const struct awboot_bootstate *state, uint8_t *selected_slot,
														struct awboot_bootstate *next_state)
{
	uint8_t							selected = AWBOOT_BOOTSTATE_SLOT_COUNT;
	struct awboot_bootstate			next;
	enum awboot_bootstate_selection selection;

	if (!fields_valid(state) || selected_slot == NULL || next_state == NULL) {
		return AWBOOT_BOOTSTATE_SELECTION_NONE;
	}

	for (uint8_t slot = 0; slot < AWBOOT_BOOTSTATE_SLOT_COUNT; ++slot) {
		const bool bootable =
			state->priority[slot] > 0U && (state->successful[slot] != 0U || state->tries_remaining[slot] > 0U);
		if (!bootable) {
			continue;
		}
		if (selected >= AWBOOT_BOOTSTATE_SLOT_COUNT || state->priority[slot] > state->priority[selected] ||
			(state->priority[slot] == state->priority[selected] && slot == state->active_slot)) {
			selected = slot;
		}
	}

	if (selected >= AWBOOT_BOOTSTATE_SLOT_COUNT) {
		return AWBOOT_BOOTSTATE_SELECTION_NONE;
	}

	next = *state;
	if (state->successful[selected] != 0U) {
		if (selected != state->active_slot) {
			next.active_slot = selected;
			next.generation++;
		}
		selection = AWBOOT_BOOTSTATE_SELECTION_CONFIRMED;
	} else {
		next.active_slot = selected;
		next.tries_remaining[selected]--;
		next.generation++;
		selection = AWBOOT_BOOTSTATE_SELECTION_TRIAL;
	}

	*next_state	   = next;
	*selected_slot = selected;
	return selection;
}

bool awboot_bootstate_set_primary(const struct awboot_bootstate *state, uint8_t slot,
									 struct awboot_bootstate *next_state)
{
	struct awboot_bootstate next;
	const uint8_t other = slot == AWBOOT_BOOTSTATE_SLOT_A ? AWBOOT_BOOTSTATE_SLOT_B : AWBOOT_BOOTSTATE_SLOT_A;

	if (!fields_valid(state) || slot >= AWBOOT_BOOTSTATE_SLOT_COUNT || next_state == NULL) {
		return false;
	}

	next = *state;
	next.active_slot = slot;
	next.priority[slot] = AWBOOT_BOOTSTATE_PRIMARY_PRIORITY;
	next.tries_remaining[slot] = AWBOOT_BOOTSTATE_DEFAULT_TRIES;
	if (next.priority[other] != 0U &&
		(next.successful[other] != 0U || next.tries_remaining[other] != 0U)) {
		next.priority[other] = AWBOOT_BOOTSTATE_FALLBACK_PRIORITY;
	}
	next.generation++;
	*next_state = next;
	return true;
}

bool awboot_bootstate_set_successful(const struct awboot_bootstate *state, uint8_t slot, bool successful,
										struct awboot_bootstate *next_state)
{
	struct awboot_bootstate next;

	if (!fields_valid(state) || slot >= AWBOOT_BOOTSTATE_SLOT_COUNT || next_state == NULL) {
		return false;
	}

	next = *state;
	if (successful) {
		next.successful[slot] = 1U;
		next.tries_remaining[slot] = AWBOOT_BOOTSTATE_DEFAULT_TRIES;
		if (next.priority[slot] == 0U) {
			next.priority[slot] = slot == next.active_slot ? AWBOOT_BOOTSTATE_PRIMARY_PRIORITY
												  : AWBOOT_BOOTSTATE_FALLBACK_PRIORITY;
		}
	} else {
		next.successful[slot] = 0U;
		next.tries_remaining[slot] = 0U;
		next.priority[slot] = 0U;
	}
	next.generation++;
	*next_state = next;
	return true;
}
