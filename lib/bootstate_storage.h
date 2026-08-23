#ifndef AWBOOT_BOOTSTATE_STORAGE_H
#define AWBOOT_BOOTSTATE_STORAGE_H

#include "bootstate.h"

#define AWBOOT_BOOTSTATE_SECTOR_SIZE 512U

typedef bool (*awboot_bootstate_read_sector_fn)(void *context, uint64_t lba,
												uint8_t sector[AWBOOT_BOOTSTATE_SECTOR_SIZE]);

typedef bool (*awboot_bootstate_write_sector_fn)(void *context, uint64_t lba,
												 const uint8_t sector[AWBOOT_BOOTSTATE_SECTOR_SIZE]);

/* Return true only after prior writes are durable across power loss. */
typedef bool (*awboot_bootstate_sync_fn)(void *context);

struct awboot_bootstate_storage {
	void							*context;
	awboot_bootstate_read_sector_fn	 read_sector;
	awboot_bootstate_write_sector_fn write_sector;
	awboot_bootstate_sync_fn		 sync;
	uint64_t						 copy_lba[2];
};

enum awboot_bootstate_storage_status {
	AWBOOT_BOOTSTATE_STORAGE_OK = 0,
	AWBOOT_BOOTSTATE_STORAGE_INVALID_ARGUMENT,
	AWBOOT_BOOTSTATE_STORAGE_READ_FAILED,
	AWBOOT_BOOTSTATE_STORAGE_EMPTY,
	AWBOOT_BOOTSTATE_STORAGE_AMBIGUOUS,
	AWBOOT_BOOTSTATE_STORAGE_STALE_UPDATE,
	AWBOOT_BOOTSTATE_STORAGE_WRITE_FAILED,
	AWBOOT_BOOTSTATE_STORAGE_SYNC_FAILED,
	AWBOOT_BOOTSTATE_STORAGE_VERIFY_FAILED,
};

/*
 * Both sectors must be readable. One CRC-invalid copy is tolerated. This API
 * is non-reentrant and the caller must provide two reserved LBAs on one device.
 */
enum awboot_bootstate_storage_status awboot_bootstate_storage_load(const struct awboot_bootstate_storage *storage,
																   struct awboot_bootstate				 *state,
																   enum awboot_bootstate_source			 *source);

/*
 * Write the next generation to the non-current copy, force it to nonvolatile
 * media, then verify it by readback. An empty store accepts generation zero and
 * starts with copy 0.
 */
enum awboot_bootstate_storage_status awboot_bootstate_storage_commit(const struct awboot_bootstate_storage *storage,
																	 const struct awboot_bootstate		   *next_state,
																	 enum awboot_bootstate_source *written_source);

#endif
