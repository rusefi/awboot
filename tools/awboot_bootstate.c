#define _POSIX_C_SOURCE 200809L

#include "bootstate.h"
#include "bootstate_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#define DEFAULT_BOOTSTATE_DEVICE "/dev/rusefi/emmc-bootstate"

struct file_storage {
	int fd;
};

static bool transfer_at(int fd, void *buffer, size_t length, off_t offset, bool write_data)
{
	uint8_t *bytes = buffer;
	size_t transferred = 0U;

	while (transferred < length) {
		ssize_t result;

		if (write_data) {
			result = pwrite(fd, bytes + transferred, length - transferred, offset + (off_t)transferred);
		} else {
			result = pread(fd, bytes + transferred, length - transferred, offset + (off_t)transferred);
		}
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result <= 0) {
			return false;
		}
		transferred += (size_t)result;
	}
	return true;
}

static bool read_sector(void *context, uint64_t lba, uint8_t sector[AWBOOT_BOOTSTATE_SECTOR_SIZE])
{
	struct file_storage *storage = context;
	const uint64_t offset = lba * AWBOOT_BOOTSTATE_SECTOR_SIZE;

	if (lba > (uint64_t)INT64_MAX / AWBOOT_BOOTSTATE_SECTOR_SIZE) {
		return false;
	}
	return transfer_at(storage->fd, sector, AWBOOT_BOOTSTATE_SECTOR_SIZE, (off_t)offset, false);
}

static bool write_sector(void *context, uint64_t lba,
						 const uint8_t sector[AWBOOT_BOOTSTATE_SECTOR_SIZE])
{
	struct file_storage *storage = context;
	const uint64_t offset = lba * AWBOOT_BOOTSTATE_SECTOR_SIZE;

	if (lba > (uint64_t)INT64_MAX / AWBOOT_BOOTSTATE_SECTOR_SIZE) {
		return false;
	}
	return transfer_at(storage->fd, (void *)sector, AWBOOT_BOOTSTATE_SECTOR_SIZE, (off_t)offset, true);
}

static bool sync_storage(void *context)
{
	const struct file_storage *storage = context;
	return fdatasync(storage->fd) == 0;
}

static bool parse_slot(const char *bootname, uint8_t *slot)
{
	if (strcmp(bootname, "rootfs.0") == 0) {
		*slot = AWBOOT_BOOTSTATE_SLOT_A;
		return true;
	}
	if (strcmp(bootname, "rootfs.1") == 0) {
		*slot = AWBOOT_BOOTSTATE_SLOT_B;
		return true;
	}
	return false;
}

static const char *bootname(uint8_t slot)
{
	return slot == AWBOOT_BOOTSTATE_SLOT_A ? "rootfs.0" : "rootfs.1";
}

static bool commit(struct awboot_bootstate_storage *storage, const struct awboot_bootstate *state)
{
	enum awboot_bootstate_source written_source;
	const enum awboot_bootstate_storage_status status =
		awboot_bootstate_storage_commit(storage, state, &written_source);

	if (status != AWBOOT_BOOTSTATE_STORAGE_OK) {
		fprintf(stderr, "awboot-bootstate: commit failed: %u\n", (unsigned int)status);
		return false;
	}
	return true;
}

static bool load(struct awboot_bootstate_storage *storage, struct awboot_bootstate *state)
{
	enum awboot_bootstate_source source;
	const enum awboot_bootstate_storage_status status =
		awboot_bootstate_storage_load(storage, state, &source);

	if (status != AWBOOT_BOOTSTATE_STORAGE_OK) {
		fprintf(stderr, "awboot-bootstate: load failed: %u\n", (unsigned int)status);
		return false;
	}
	return true;
}

static bool initialize(struct awboot_bootstate_storage *storage, uint8_t slot)
{
	struct awboot_bootstate state = {
		.generation = 0U,
		.active_slot = slot,
		.priority = {AWBOOT_BOOTSTATE_FALLBACK_PRIORITY, AWBOOT_BOOTSTATE_FALLBACK_PRIORITY},
		.tries_remaining = {AWBOOT_BOOTSTATE_DEFAULT_TRIES, AWBOOT_BOOTSTATE_DEFAULT_TRIES},
		.successful = {1U, 1U},
	};
	struct awboot_bootstate current;
	enum awboot_bootstate_source source;
	const enum awboot_bootstate_storage_status status =
		awboot_bootstate_storage_load(storage, &current, &source);

	if (status != AWBOOT_BOOTSTATE_STORAGE_EMPTY) {
		fprintf(stderr, "awboot-bootstate: refusing to initialize non-empty state: %u\n", (unsigned int)status);
		return false;
	}
	state.priority[slot] = AWBOOT_BOOTSTATE_PRIMARY_PRIORITY;
	return commit(storage, &state);
}

static int run_command(struct awboot_bootstate_storage *storage, int argc, char **argv)
{
	struct awboot_bootstate state;
	struct awboot_bootstate next;
	uint8_t slot;

	if (argc == 3 && strcmp(argv[1], "initialize") == 0) {
		return parse_slot(argv[2], &slot) && initialize(storage, slot) ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (!load(storage, &state)) {
		return EXIT_FAILURE;
	}
	if (argc == 2 && (strcmp(argv[1], "get-primary") == 0 || strcmp(argv[1], "get-current") == 0)) {
		puts(bootname(state.active_slot));
		return EXIT_SUCCESS;
	}
	if (argc == 3 && strcmp(argv[1], "get-state") == 0 && parse_slot(argv[2], &slot)) {
		puts(state.successful[slot] != 0U ? "good" : "bad");
		return EXIT_SUCCESS;
	}
	if (argc == 3 && strcmp(argv[1], "set-primary") == 0 && parse_slot(argv[2], &slot)) {
		return awboot_bootstate_set_primary(&state, slot, &next) && commit(storage, &next)
			? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (argc == 4 && strcmp(argv[1], "set-state") == 0 && parse_slot(argv[2], &slot)) {
		bool successful;
		if (strcmp(argv[3], "good") == 0) {
			successful = true;
		} else if (strcmp(argv[3], "bad") == 0) {
			successful = false;
		} else {
			return EXIT_FAILURE;
		}
		return awboot_bootstate_set_successful(&state, slot, successful, &next) && commit(storage, &next)
			? EXIT_SUCCESS : EXIT_FAILURE;
	}

	fprintf(stderr,
		"usage: awboot-bootstate <get-primary|get-current>\n"
		"       awboot-bootstate <get-state|set-primary|initialize> <rootfs.0|rootfs.1>\n"
		"       awboot-bootstate set-state <rootfs.0|rootfs.1> <good|bad>\n");
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	const char *device = getenv("AWBOOT_BOOTSTATE_DEVICE");
	struct file_storage context;
	struct awboot_bootstate_storage storage;
	int result;

	if (device == NULL || device[0] == '\0') {
		device = DEFAULT_BOOTSTATE_DEVICE;
	}
	context.fd = open(device, O_RDWR | O_CLOEXEC);
	if (context.fd < 0) {
		fprintf(stderr, "awboot-bootstate: cannot open %s: %s\n", device, strerror(errno));
		return EXIT_FAILURE;
	}
	if (flock(context.fd, LOCK_EX) != 0) {
		fprintf(stderr, "awboot-bootstate: cannot lock %s: %s\n", device, strerror(errno));
		close(context.fd);
		return EXIT_FAILURE;
	}
	storage = (struct awboot_bootstate_storage){
		.context = &context,
		.read_sector = read_sector,
		.write_sector = write_sector,
		.sync = sync_storage,
		.copy_lba = {0U, 1U},
	};
	result = run_command(&storage, argc, argv);
	close(context.fd);
	return result;
}
