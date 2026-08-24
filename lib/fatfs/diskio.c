/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/
#include "ff.h" /* Obtains integer types */
#include "diskio.h"
#include "common.h"
#include "sdmmc.h"
#include "debug.h"
#include "dram.h"
#include "sunxi_dma.h"
#include "board.h"

static DSTATUS Stat = STA_NOINIT; /* Disk status */
#if FF_MULTI_PARTITION
PARTITION VolToPart[FF_VOLUMES] = {
	{0U, 1U},
	{0U, 2U},
};
#endif
#if CONFIG_RAUC_EMMC
#ifndef CONFIG_FATFS_CACHE_SIZE
#error "RAUC eMMC requires an SDRAM read-bounce size"
#elif CONFIG_FATFS_CACHE_SIZE == 0
#error "RAUC eMMC read-bounce size must be nonzero"
#endif
static u8 *const read_bounce = (u8 *)SDRAM_BASE;
#endif
#if defined(CONFIG_FATFS_CACHE_SIZE) && !CONFIG_RAUC_EMMC
static u8 *const cache		= (u8 *)SDRAM_BASE;
static const u32 cache_size = (CONFIG_FATFS_CACHE_SIZE);
static u32		 cache_first, cache_last;
#endif

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
	if (pdrv)
		return STA_NOINIT;

#if defined(CONFIG_FATFS_CACHE_SIZE) && !CONFIG_RAUC_EMMC
	cache_first = 0xFFFFFFFF - cache_size; // Set to a big sector for a proper init
	cache_last	= 0xFFFFFFFF;
#endif

	return Stat;
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
	if (pdrv)
		return STA_NOINIT;

	Stat &= ~STA_NOINIT;

	return Stat;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read(BYTE	pdrv, /* Physical drive nmuber to identify the drive */
				  BYTE *buff, /* Data buffer to store read data */
				  LBA_t sector, /* Start sector in LBA */
				  UINT	count /* Number of sectors to read */
)
{
#if defined(CONFIG_FATFS_CACHE_SIZE) && !CONFIG_RAUC_EMMC
	u32 blkread, read_pos, first, last, chunk, bytes;
#endif

	if (pdrv || !count)
		return RES_PARERR;
	if (Stat & STA_NOINIT)
		return RES_NOTRDY;

	trace("FATFS: read %" PRIu32 " sectors at %" PRIu32 "\r\n", (uint32_t)count, (uint32_t)sector);

#if CONFIG_RAUC_EMMC
	/* Keep IDMA payloads in SDRAM without the legacy cache's window over-copy. */
	while (count > 0U) {
		const UINT chunk = count > CONFIG_FATFS_CACHE_SIZE ? CONFIG_FATFS_CACHE_SIZE : count;
		const uint64_t blocks_read = chunk == 1U
			? sdmmc_blk_read_pio(&card0, read_bounce, sector)
			: sdmmc_blk_read(&card0, read_bounce, sector, chunk);
		if (blocks_read != chunk)
			return RES_ERROR;
		memcpy(buff, read_bounce, chunk * FF_MIN_SS);
		buff += chunk * FF_MIN_SS;
		sector += chunk;
		count -= chunk;
	}
	return RES_OK;
#elif defined(CONFIG_FATFS_CACHE_SIZE)
	first = sector;
	last  = sector + count;
	bytes = count * FF_MIN_SS;

	// Read starts in cache but overflows
	if (first >= cache_first && first < cache_last && last > cache_last) {
		chunk = (cache_last - first) * FF_MIN_SS;
		memcpy(buff, cache + (first - cache_first) * FF_MIN_SS, chunk);
		buff += chunk;
		first += (cache_last - first);
		trace("FATFS: chunk %" PRIu32 " first %" PRIu32 "\r\n", (uint32_t)chunk, first);
	}

	// Read is NOT in the cache
	if (last > cache_last || first < cache_first) {
		trace("FATFS: if %" PRIu32 " > %" PRIu32 " || %" PRIu32 " < %" PRIu32 "\r\n", last, cache_last, first,
			  cache_first);

		read_pos	= (first / cache_size) * cache_size; // TODO: check with card max capacity
		cache_first = read_pos;
		cache_last	= read_pos + cache_size;
		blkread		= sdmmc_blk_read(&card0, cache, read_pos, cache_size);

		if (blkread != cache_size) {
			warning("FATFS: MMC read %" PRIu32 "/%" PRIu32 " blocks\r\n", blkread, cache_size);
			return RES_ERROR;
		}
		trace("FATFS: cached %" PRIu32 " sectors (%" PRIu32 "KB) at %" PRIu32 "/[%" PRIu32 "-%" PRIu32 "]\r\n", blkread,
			  (blkread * FF_MIN_SS) / 1024, first, read_pos, read_pos + cache_size);
	}

	// Copy from read cache to output buffer
	trace("FATFS: copy %" PRIu32 " from 0x%p to 0x%p\r\n", bytes, (void *)(cache + ((first - cache_first) * FF_MIN_SS)),
		  (void *)buff);
	memcpy(buff, (cache + ((first - cache_first) * FF_MIN_SS)), bytes);

	return RES_OK;
#else
	return (sdmmc_blk_read(&card0, buff, sector, count) == count ? RES_OK : RES_ERROR);
#endif
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE		   pdrv, /* Physical drive nmuber to identify the drive */
				   const BYTE *buff, /* Data to be written */
				   LBA_t	   sector, /* Start sector in LBA */
				   UINT		   count /* Number of sectors to write */
)
{
	return RES_ERROR;
}

#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl(BYTE	 pdrv, /* Physical drive nmuber (0..) */
				   BYTE	 cmd, /* Control code */
				   void *buff /* Buffer to send/receive control data */
)
{
	return RES_PARERR;
}
