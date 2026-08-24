#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void *memcpy(void *dest, const void *src, size_t count);

#define CONFIG_RAUC_EMMC 1
#define __BOARD_H__
#define __MAIN_H__
#define __SDCARD_H__
#define __DEBUG_H__
#define __dram_head_h__
#define _SUNXI_DMA_H
#define trace(...)
#define info(...) do { } while (0)
#define CONFIG_FATFS_CACHE_SIZE 36U

static uint8_t bounce[CONFIG_FATFS_CACHE_SIZE * 512U];
#define SDRAM_BASE ((uintptr_t)bounce)

typedef struct {
	int unused;
} sdmmc_pdata_t;

static sdmmc_pdata_t card0;
typedef struct {
	uint8_t *buffer;
	uint64_t sector;
	uint64_t count;
	int pio;
} read_call_t;

static read_call_t calls[4];
static size_t call_count;
static size_t short_read_call;

static uint64_t mock_read(sdmmc_pdata_t *data, uint8_t *buffer, uint64_t sector, uint64_t count, int pio)
{
	assert(data == &card0);
	assert(call_count < sizeof(calls) / sizeof(calls[0]));
	calls[call_count].buffer = buffer;
	calls[call_count].sector = sector;
	calls[call_count].count = count;
	calls[call_count].pio = pio;
	call_count++;
	for (uint64_t block = 0U; block < count; block++)
		for (size_t byte = 0U; byte < 512U; byte++)
			buffer[block * 512U + byte] = (uint8_t)(sector + block);
	return call_count == short_read_call ? count - 1U : count;
}

uint64_t sdmmc_blk_read(sdmmc_pdata_t *data, uint8_t *buffer, uint64_t sector, uint64_t count)
{
	return mock_read(data, buffer, sector, count, 0);
}

uint64_t sdmmc_blk_read_pio(sdmmc_pdata_t *data, uint8_t *buffer, uint64_t sector)
{
	return mock_read(data, buffer, sector, 1U, 1);
}

#include "../lib/fatfs/diskio.c"

int main(void)
{
	uint8_t buffer[64U * 512U] = {0};

	assert(disk_initialize(0U) == 0U);
	assert(disk_read(0U, buffer, 1234U, 64U) == RES_OK);
	assert(call_count == 2U);
	assert(calls[0].buffer == bounce);
	assert(calls[0].sector == 1234U);
	assert(calls[0].count == CONFIG_FATFS_CACHE_SIZE);
	assert(calls[0].pio == 0);
	assert(calls[1].buffer == bounce);
	assert(calls[1].sector == 1234U + CONFIG_FATFS_CACHE_SIZE);
	assert(calls[1].count == 64U - CONFIG_FATFS_CACHE_SIZE);
	assert(calls[1].pio == 0);
	for (size_t block = 0U; block < 64U; block++)
		for (size_t byte = 0U; byte < 512U; byte++)
			assert(buffer[block * 512U + byte] == (uint8_t)(1234U + block));

	call_count = 0U;
	assert(disk_read(0U, buffer, 8324U, 1U) == RES_OK);
	assert(call_count == 1U);
	assert(calls[0].buffer == bounce);
	assert(calls[0].sector == 8324U);
	assert(calls[0].count == 1U);
	assert(calls[0].pio == 1);
	assert(buffer[0] == (uint8_t)8324U);

	call_count = 0U;
	assert(disk_read(0U, buffer, 5000U, CONFIG_FATFS_CACHE_SIZE + 1U) == RES_OK);
	assert(call_count == 2U);
	assert(calls[0].sector == 5000U);
	assert(calls[0].count == CONFIG_FATFS_CACHE_SIZE);
	assert(calls[0].pio == 0);
	assert(calls[1].sector == 5000U + CONFIG_FATFS_CACHE_SIZE);
	assert(calls[1].count == 1U);
	assert(calls[1].pio == 1);
	assert(buffer[CONFIG_FATFS_CACHE_SIZE * 512U] ==
		   (uint8_t)(5000U + CONFIG_FATFS_CACHE_SIZE));

	call_count = 0U;
	short_read_call = 2U;
	assert(disk_read(0U, buffer, 1234U, 64U) == RES_ERROR);
	assert(call_count == 2U);
	assert(disk_read(1U, buffer, 1234U, 64U) == RES_PARERR);

	puts("disk read tests passed");
	return 0;
}
