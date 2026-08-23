#ifndef AWBOOT_BOOTSTATE_H
#define AWBOOT_BOOTSTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AWBOOT_BOOTSTATE_MAGIC		 0x53425741U /* "AWBS" in little-endian storage */
#define AWBOOT_BOOTSTATE_VERSION	 1U
#define AWBOOT_BOOTSTATE_RECORD_SIZE 64U
#define AWBOOT_BOOTSTATE_CRC_OFFSET	 60U
#define AWBOOT_BOOTSTATE_SLOT_COUNT	 2U
#define AWBOOT_BOOTSTATE_DEFAULT_TRIES 3U
#define AWBOOT_BOOTSTATE_PRIMARY_PRIORITY 20U
#define AWBOOT_BOOTSTATE_FALLBACK_PRIORITY 10U

enum awboot_bootstate_slot {
	AWBOOT_BOOTSTATE_SLOT_A = 0,
	AWBOOT_BOOTSTATE_SLOT_B = 1,
};

/*
 * Durable v1 record layout (all multi-byte fields are little-endian):
 *   0x00 u32 magic
 *   0x04 u16 version
 *   0x06 u16 record size
 *   0x08 u32 generation
 *   0x0c u8  active slot
 *   0x0d u8  priority[2]
 *   0x0f u8  tries remaining[2]
 *   0x11 u8  successful[2]
 *   0x13 u8  reserved[41], must be zero
 *   0x3c u32 CRC-32/IEEE over bytes [0x00, 0x3c)
 */
struct awboot_bootstate {
	uint32_t generation;
	uint8_t	 active_slot;
	uint8_t	 priority[AWBOOT_BOOTSTATE_SLOT_COUNT];
	uint8_t	 tries_remaining[AWBOOT_BOOTSTATE_SLOT_COUNT];
	uint8_t	 successful[AWBOOT_BOOTSTATE_SLOT_COUNT];
};

enum awboot_bootstate_source {
	AWBOOT_BOOTSTATE_SOURCE_NONE = 0,
	AWBOOT_BOOTSTATE_SOURCE_COPY0,
	AWBOOT_BOOTSTATE_SOURCE_COPY1,
};

enum awboot_bootstate_selection {
	AWBOOT_BOOTSTATE_SELECTION_NONE = 0,
	AWBOOT_BOOTSTATE_SELECTION_CONFIRMED,
	AWBOOT_BOOTSTATE_SELECTION_TRIAL,
};

uint32_t awboot_bootstate_crc32(const uint8_t *bytes, size_t length);

bool awboot_bootstate_encode(const struct awboot_bootstate *state, uint8_t record[AWBOOT_BOOTSTATE_RECORD_SIZE]);

bool awboot_bootstate_decode(const uint8_t *record, size_t length, struct awboot_bootstate *state);

enum awboot_bootstate_source awboot_bootstate_newest_valid(const uint8_t *copy0, size_t copy0_length,
														   const uint8_t *copy1, size_t copy1_length,
														   struct awboot_bootstate *state);

/*
 * Select the highest-priority bootable slot. A successful slot is bootable
 * without remaining trial attempts. Selecting an unconfirmed slot consumes one
 * attempt in next_state and advances its generation. Falling back to a
 * different confirmed slot also advances the generation so active_slot stays
 * authoritative. Persistence is the caller's responsibility and must complete
 * before loading whenever next_state has advanced.
 */
enum awboot_bootstate_selection awboot_bootstate_select(const struct awboot_bootstate *state, uint8_t *selected_slot,
														struct awboot_bootstate *next_state);

/* RAUC custom-backend transitions. Each successful mutation advances generation once. */
bool awboot_bootstate_set_primary(const struct awboot_bootstate *state, uint8_t slot,
									 struct awboot_bootstate *next_state);

bool awboot_bootstate_set_successful(const struct awboot_bootstate *state, uint8_t slot, bool successful,
										struct awboot_bootstate *next_state);

#endif
