#include "common.h"
#include "loaders.h"
#include "board.h"
#if CONFIG_BOOT_SPINAND
#include "fdt.h"
#endif

#if CONFIG_BOOT_SDCARD || CONFIG_BOOT_MMC

#include "sdmmc.h"

#if CONFIG_RAUC_EMMC
#if FF_VOLUMES != 2 || FF_MULTI_PARTITION != 1
#error "RAUC eMMC requires two FatFs volumes with explicit partition mapping"
#endif
static FATFS fs[FF_VOLUMES];
static unsigned int active_volume;
static bool volume_mounted;
#else
static FATFS fs;
#endif

#ifndef CLTBL_DWORDS
#define CLTBL_DWORDS 2000U
#endif

#ifndef READ_CHUNK
#define READ_CHUNK (32U * 1024U)
#endif

static FRESULT read_stream(const char *path, void (*consume)(const uint8_t *, UINT))
{
	FRESULT fret;
	FIL	file;
	UINT	bytes_read = 0U;
#if CONFIG_RAUC_EMMC
	char volume_path[MAX_FILENAME_SIZE + 4U];
#endif

	if ((path == NULL) || (consume == NULL))
		return FR_INVALID_PARAMETER;

#if CONFIG_RAUC_EMMC
	const size_t path_length = strlen(path);
	if (!volume_mounted || path_length + 4U > sizeof(volume_path))
		return FR_INVALID_PARAMETER;
	volume_path[0] = (char)('0' + active_volume);
	volume_path[1] = ':';
	volume_path[2] = '/';
	memcpy(volume_path + 3U, path[0] == '/' ? path + 1U : path,
		   path[0] == '/' ? path_length : path_length + 1U);
	if (path[0] == '/')
		volume_path[path_length + 2U] = '\0';
	path = volume_path;
#endif

	fret = f_open(&file, path, FA_READ);
	if (fret != FR_OK)
		return fret;

	static DWORD cltbl[CLTBL_DWORDS];
	cltbl[0]    = CLTBL_DWORDS;
	file.cltbl  = cltbl;
	fret = f_lseek(&file, CREATE_LINKMAP);
	if (fret == FR_NOT_ENOUGH_CORE) {
		f_close(&file);
		return fret;
	}
	if (fret != FR_OK) {
		f_close(&file);
		return fret;
	}

	static uint8_t buf[READ_CHUNK];
	FRESULT     read_result = FR_OK;
	do {
		fret = f_read(&file, buf, READ_CHUNK, &bytes_read);
		if (fret != FR_OK) {
			read_result = fret;
			break;
		}
		if (bytes_read == 0U)
			break;
		consume(buf, bytes_read);
	} while (bytes_read == READ_CHUNK);

	file.cltbl = NULL;
	FRESULT close_result = f_close(&file);
	if (read_result != FR_OK)
		return read_result;
	return close_result;
}


static LBA_t fatfs_clst2sect(/* !=0:Sector number, 0:Failed (invalid cluster#) */
					   FATFS *fs, /* Filesystem object */
					   DWORD  clst /* Cluster# to be converted */
)
{
	clst -= 2; /* Cluster number is origin from 2 */
	if (clst >= fs->n_fatent - 2)
		return 0; /* Is it invalid cluster number? */
	return fs->database + (LBA_t)fs->csize * clst; /* Start sector number of the cluster */
}

static int read_direct(const char *path, uint8_t *dest)
{
	FRESULT fret;
	FIL	file;
	UINT	bytes_read = 0U;
#if CONFIG_RAUC_EMMC
	char volume_path[MAX_FILENAME_SIZE + 4U];

	const size_t path_length = strlen(path);
	if (!volume_mounted || path_length + 4U > sizeof(volume_path))
		return FR_INVALID_PARAMETER;
	volume_path[0] = (char)('0' + active_volume);
	volume_path[1] = ':';
	volume_path[2] = '/';
	memcpy(volume_path + 3U, path[0] == '/' ? path + 1U : path,
		   path[0] == '/' ? path_length : path_length + 1U);
	if (path[0] == '/')
		volume_path[path_length + 2U] = '\0';
	path = volume_path;
#endif

	fret = f_open(&file, path, FA_READ);
	if (fret != FR_OK)
		return -fret;

	static DWORD cltbl[CLTBL_DWORDS];
	cltbl[0]    = CLTBL_DWORDS;
	file.cltbl  = cltbl;
	fret = f_lseek(&file, CREATE_LINKMAP);
	if ((fret != FR_OK) && (fret != FR_NOT_ENOUGH_CORE)) {
		f_close(&file);
		return -fret;
	}

	if (fret == FR_OK) {
		DWORD *map = cltbl;
		u32 num_extents = map[0];
		u32 index = 1;

		u32 chunks = (num_extents - 2) / 2;
		debug("%s total chunks %" PRIu32 "\r\n", path, chunks);

		for (u32 i = 0; i < chunks; i++) {
			uint64_t blkno = fatfs_clst2sect(&fs, map[index + 1]);
			uint64_t blkcnt = fs.csize * map[index];

			if (sdmmc_blk_read(&card0, dest, blkno, blkcnt) != blkcnt) {
				return -1;
			}

			bytes_read += blkcnt * 512;
			dest += blkcnt * 512;
			index += 2;
		}
	} else {
		// fallback to slow mode
		fret = f_read(&file, dest, f_size(&file), &bytes_read);
	}

	file.cltbl = NULL;
	f_close(&file);

	if (fret != FR_OK) {
		return -fret;
	}

	return bytes_read;
}

typedef struct {
	uint8_t *dest;
	u32	 total;
} read_copy_state_t;

static read_copy_state_t read_copy_state;

static void read_copy_consume(const uint8_t *buf, UINT len)
{
	memcpy(read_copy_state.dest, buf, (size_t)len);
	read_copy_state.dest += (size_t)len;
	read_copy_state.total += (u32)len;
}

void sdmmc_speed_test(void)
{
	u32 start = time_ms();
	u32 test_time;
	u32 kb_tested;
	u32 kb_per_second;

	if (sdmmc_blk_read(&card0, (u8 *)(SDRAM_BASE), 0, CONFIG_SDMMC_SPEED_TEST_SIZE) <= 0) {
		return;
	}
	test_time	  = time_ms() - start;
	kb_tested	  = (CONFIG_SDMMC_SPEED_TEST_SIZE * 512U) / 1024U;
	kb_per_second = (test_time == 0U) ? 0U : (CONFIG_SDMMC_SPEED_TEST_SIZE * 512U) / test_time;
	if (kb_per_second < 10000) {
		info("SDMMC: speedtest %" PRIu32 "KB in %" PRIu32 "ms at %" PRIu32 "KB/S\r\n", kb_tested, test_time,
			 kb_per_second);
	} else {
		u32 mb_per_second = kb_per_second / 1024;
		info("SDMMC: speedtest %" PRIu32 "KB in %" PRIu32 "ms at %" PRIu32 "MB/S\r\n", kb_tested, test_time,
			 mb_per_second);
	}
}

int mount_sdmmc()
{
#if CONFIG_RAUC_EMMC
	return mount_sdmmc_volume(0U);
#else
	FRESULT fret;

	/* mount fs */
	fret = f_mount(&fs, "", 1);
	if (fret != FR_OK) {
		error("FATFS: mount error: %d\r\n", fret);
		return -1;
	} else {
		debug("FATFS: mount OK\r\n");
	}

	return 0;
#endif
}

#if CONFIG_RAUC_EMMC
int mount_sdmmc_volume(unsigned int volume)
{
	FRESULT fret;
	char path[3];

	if (volume >= FF_VOLUMES)
		return -1;
	path[0] = (char)('0' + volume);
	path[1] = ':';
	path[2] = '\0';
	fret = f_mount(&fs[volume], path, 1);
	if (fret != FR_OK) {
		error("FATFS: volume %u mount error: %d\r\n", volume, fret);
		return -1;
	}
	active_volume = volume;
	volume_mounted = true;
	debug("FATFS: volume %u mount OK\r\n", volume);
	return 0;
}
#endif

void unmount_sdmmc(void)
{
	FRESULT fret;

	/* umount fs */
#if CONFIG_RAUC_EMMC
	char path[3];
	if (!volume_mounted)
		return;
	path[0] = (char)('0' + active_volume);
	path[1] = ':';
	path[2] = '\0';
	fret = f_mount(0, path, 0);
	volume_mounted = false;
#else
	fret = f_mount(0, "", 0);
#endif
	if (fret != FR_OK) {
		error("FATFS: unmount error %d\r\n", fret);
	} else {
		debug("FATFS: unmount OK\r\n");
	}
}

int read_file(const char *filename, uint8_t *dest)
{
	if (!filename) {
		error("FATFS: empty filename\r\n");
		return -1;
	}
	if (!dest) {
		error("FATFS: empty destination\r\n");
		return -1;
	}

#if LOG_LEVEL >= LOG_DEBUG
	u32 start = time_ms();
#endif

#if 0
	read_copy_state.dest  = dest;
	read_copy_state.total = 0U;

	FRESULT fret = read_stream(filename, read_copy_consume);
	if (fret != FR_OK) {
		read_copy_state.dest  = NULL;
		read_copy_state.total = 0U;
		error("FATFS: file read: [%s]: error %d\r\n", filename, fret);
		return -1;
	}

	u32 total_bytes = read_copy_state.total;
#else
	u32 total_bytes = read_direct(filename, dest);
#endif

#if LOG_LEVEL >= LOG_DEBUG
	u32 duration	 = time_ms() - start + 1U;
	f32 throughput = ((f32)total_bytes / (f32)duration) / 1024.0f;
	debug("FATFS: %s read in %" PRIu32 "ms at %.2fMB/S\r\n", filename, duration, throughput);
#endif

	read_copy_state.dest  = NULL;
	read_copy_state.total = 0U;
	return (int)total_bytes;
}

int load_sdmmc(image_info_t *image)
{
	int ret;

#if LOG_LEVEL >= LOG_DEBUG
	u32 start;
	start = time_ms();
#endif // LOG_LEVEL >= LOG_DEBUG

	info("FATFS: read %s addr=%x\r\n", image->of_filename, (unsigned int)image->dtb_dest);
	ret = read_file(image->of_filename, image->dtb_dest);
	if (ret <= 0)
		return ret;
	image->dtb_size = ret;

	info("FATFS: read %s addr=%x\r\n", image->filename, (unsigned int)image->kernel_dest);
	ret = read_file(image->filename, image->kernel_dest);
	if (ret <= 0)
		return ret;
	image->kernel_size = ret;

	if (image->initrd_filename && image->initrd_dest) {
		if (strlen(image->initrd_filename)) {
			info("FATFS: read %s addr=%x\r\n", image->initrd_filename, (unsigned int)image->initrd_dest);
			ret = read_file(image->initrd_filename, image->initrd_dest);
			if (ret <= 0)
				return ret;
			image->initrd_size = ret;
		}
	}

#if LOG_LEVEL >= LOG_DEBUG
	debug("FATFS: done in %" PRIu32 "ms\r\n", time_ms() - start);
#endif

	return 0;
}
#endif

#if CONFIG_BOOT_SPINAND
int load_spi_nand(sunxi_spi_t *spi, image_info_t *image)
{
	linux_zimage_header_t *hdr;
	unsigned int		   size;
	uint64_t			   start, time;

	if (spi_nand_detect(spi) != 0)
		return -1;

	/* get dtb size and read */
	spi_nand_read(spi, image->dtb_dest, CONFIG_SPINAND_DTB_ADDR, (uint32_t)sizeof(boot_param_header_t));
	if (fdt_check_blob_valid(image->dtb_dest) != 0) {
		error("SPI-NAND: DTB verification failed\r\n");
		return -1;
	}

	size = fdt_get_total_size(image->dtb_dest);
	debug("SPI-NAND: dt blob: Copy from 0x%08x to 0x%08lx size:0x%08x\r\n", CONFIG_SPINAND_DTB_ADDR,
		  (uint32_t)image->dtb_dest, size);
	start = time_us();
	spi_nand_read(spi, image->dtb_dest, CONFIG_SPINAND_DTB_ADDR, (uint32_t)size);
	time = time_us() - start;
	info("SPI-NAND: read dt blob of size %u at %.2fMB/S\r\n", size, (f32)(size / time));

	/* get kernel size and read */
	spi_nand_read(spi, image->kernel_dest, CONFIG_SPINAND_KERNEL_ADDR, (uint32_t)sizeof(linux_zimage_header_t));
	hdr = (linux_zimage_header_t *)image->kernel_dest;
	if (hdr->magic != LINUX_ZIMAGE_MAGIC) {
		debug("SPI-NAND: zImage verification failed\r\n");
		return -1;
	}
	size = hdr->end - hdr->start;
	debug("SPI-NAND: Image: Copy from 0x%08x to 0x%08lx size:0x%08x\r\n", CONFIG_SPINAND_KERNEL_ADDR,
		  (uint32_t)image->kernel_dest, size);
	start = time_us();
	spi_nand_read(spi, image->kernel_dest, CONFIG_SPINAND_KERNEL_ADDR, (uint32_t)size);
	time = time_us() - start;
	info("SPI-NAND: read Image of size %u at %.2fMB/S\r\n", size, (f32)(size / time));

	return 0;
}
#endif

#if CONFIG_BOOT_SPINAND
int load_spi_nor(sunxi_spi_t *spi, image_info_t *image)
{
	linux_zimage_header_t *hdr;
	unsigned int		   size;
	uint64_t UNUSED_DEBUG	   start, time;

	info(" %" PRIu32 "ms\r\n", time_ms());

	if (spi_nor_detect(spi) != 0)
		return -1;

	info(" %" PRIu32 "ms\r\n", time_ms());

	/* get dtb size and read */
	spi_nor_read(spi, image->dtb_dest, CONFIG_SPINAND_DTB_ADDR, (uint32_t)sizeof(boot_param_header_t));
	if (fdt_check_blob_valid(image->dtb_dest) != 0) {
		error("SPI-NAND: DTB verification failed\r\n");
		return -1;
	}

	size = fdt_get_total_size(image->dtb_dest);
	debug("SPI-NAND: dt blob: Copy from 0x%08x to 0x%08lx size:0x%08x\r\n", CONFIG_SPINAND_DTB_ADDR,
		  (uint32_t)image->dtb_dest, size);
	start = time_us();
	spi_nor_read(spi, image->dtb_dest, CONFIG_SPINAND_DTB_ADDR, (uint32_t)size);
	time = time_us() - start;
	info("SPI-NAND: read dt blob of size %u at %.2fMB/S\r\n", size, (f32)(size / time));

	info(" %" PRIu32 "ms\r\n", time_ms());

	/* get kernel size and read */
	spi_nor_read(spi, image->kernel_dest, CONFIG_SPINAND_KERNEL_ADDR, (uint32_t)sizeof(linux_zimage_header_t));
	hdr = (linux_zimage_header_t *)image->kernel_dest;
	if (hdr->magic != LINUX_ZIMAGE_MAGIC) {
		debug("SPI-NAND: zImage verification failed\r\n");
		return -1;
	}
	size = hdr->end - hdr->start;
	debug("SPI-NAND: Image: Copy from 0x%08x to 0x%08lx size:0x%08x\r\n", CONFIG_SPINAND_KERNEL_ADDR,
		  (uint32_t)image->kernel_dest, size);
	start = time_us();
	spi_nor_read(spi, image->kernel_dest, CONFIG_SPINAND_KERNEL_ADDR, (uint32_t)size);
	time = time_us() - start;
	info("SPI-NAND: read Image of size %u at %.2fMB/S\r\n", size, (f32)(size / time));

	info(" %" PRIu32 "ms\r\n", time_ms());

	return 0;
}
#endif
