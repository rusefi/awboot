CC ?= cc
CFLAGS ?= -std=c2x -Wall -Wextra -Werror -pedantic -O2

BOOTSTATE_TEST := bootstate_test
BOOTSTATE_SOURCES := bootstate_test.c ../lib/bootstate.c ../lib/bootstate_storage.c
RAUC_MBR_TEST := rauc_mbr_test
RAUC_MBR_SOURCES := rauc_mbr_test.c ../lib/rauc_mbr.c
RAUC_BOOT_TEST := rauc_boot_test
RAUC_BOOT_SOURCES := rauc_boot_test.c ../lib/rauc_boot.c ../lib/rauc_mbr.c ../lib/bootstate.c ../lib/bootstate_storage.c
DISK_READ_TEST := disk_read_test
SD_HIGH_SPEED_TEST := sd_high_speed_test
BOOTSTATE_TOOL := ../tools/awboot-bootstate

.PHONY: all test clean

all: $(BOOTSTATE_TEST) $(RAUC_MBR_TEST) $(RAUC_BOOT_TEST) $(DISK_READ_TEST) $(SD_HIGH_SPEED_TEST)

$(BOOTSTATE_TEST): $(BOOTSTATE_SOURCES) ../lib/bootstate.h ../lib/bootstate_storage.h
	$(CC) $(CFLAGS) -I../lib $(BOOTSTATE_SOURCES) -o $@

$(RAUC_MBR_TEST): $(RAUC_MBR_SOURCES) ../lib/rauc_mbr.h
	$(CC) $(CFLAGS) -I../lib $(RAUC_MBR_SOURCES) -o $@

$(RAUC_BOOT_TEST): $(RAUC_BOOT_SOURCES) ../lib/rauc_boot.h ../lib/rauc_mbr.h ../lib/bootstate.h ../lib/bootstate_storage.h
	$(CC) $(CFLAGS) -I../lib $(RAUC_BOOT_SOURCES) -o $@

$(DISK_READ_TEST): disk_read_test.c ../lib/fatfs/diskio.c ../lib/fatfs/diskio.h ../lib/fatfs/ff.h ../lib/fatfs/ffconf.h
	$(CC) $(CFLAGS) -Wno-unused-parameter \
		-I.. -I../include -I../lib -I../lib/fatfs -I../arch/arm32/include \
		-I../arch/arm32/mach-t113s3/include -I../arch/arm32/mach-t113s3 \
		-I../arch/arm32/mach-t113s3/mmc disk_read_test.c -o $@

$(SD_HIGH_SPEED_TEST): sd_high_speed_test.c ../arch/arm32/mach-t113s3/sdmmc.c
	$(CC) $(CFLAGS) -Wno-pedantic \
		-I.. -I../include -I../lib -I../lib/fatfs -I../arch/arm32/include \
		-I../arch/arm32/mach-t113s3/include -I../arch/arm32/mach-t113s3 \
		-I../arch/arm32/mach-t113s3/mmc sd_high_speed_test.c -o $@

test: $(BOOTSTATE_TEST) $(RAUC_MBR_TEST) $(RAUC_BOOT_TEST) $(DISK_READ_TEST) $(SD_HIGH_SPEED_TEST) $(BOOTSTATE_TOOL)
	./$(BOOTSTATE_TEST)
	./$(RAUC_MBR_TEST)
	./$(RAUC_BOOT_TEST)
	./$(DISK_READ_TEST)
	./$(SD_HIGH_SPEED_TEST)
	sh ./memcpy_alignment_test.sh
	sh ./awboot_bootstate_tool_test.sh $(BOOTSTATE_TOOL)
	CC="$(CC)" sh ./variant_config_test.sh

$(BOOTSTATE_TOOL): ../tools/awboot_bootstate.c ../lib/bootstate.c ../lib/bootstate_storage.c
	$(MAKE) -C ../tools awboot-bootstate

clean:
	rm -f $(BOOTSTATE_TEST) $(RAUC_MBR_TEST) $(RAUC_BOOT_TEST) $(DISK_READ_TEST) $(SD_HIGH_SPEED_TEST)
