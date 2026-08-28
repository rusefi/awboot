/*
 * driver/sd/sdcard.c
 *
 * Copyright(c) 2007-2022 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "common.h"
#include "debug.h"
#include "sunxi_sdhci.h"
#include "sdmmc.h"
#include "board.h"

#define FALSE 0
#define TRUE  1

/*
 * EXT_CSD fields
 */

#define EXT_CSD_CMDQ_MODE_EN				15 /* R/W */
#define EXT_CSD_FLUSH_CACHE					32 /* W */
#define EXT_CSD_CACHE_CTRL					33 /* R/W */
#define EXT_CSD_POWER_OFF_NOTIFICATION		34 /* R/W */
#define EXT_CSD_PACKED_FAILURE_INDEX		35 /* RO */
#define EXT_CSD_PACKED_CMD_STATUS			36 /* RO */
#define EXT_CSD_EXP_EVENTS_STATUS			54 /* RO, 2 bytes */
#define EXT_CSD_EXP_EVENTS_CTRL				56 /* R/W, 2 bytes */
#define EXT_CSD_DATA_SECTOR_SIZE			61 /* R */
#define EXT_CSD_GP_SIZE_MULT				143 /* R/W */
#define EXT_CSD_PARTITION_SETTING_COMPLETED 155 /* R/W */
#define EXT_CSD_PARTITION_ATTRIBUTE			156 /* R/W */
#define EXT_CSD_PARTITION_SUPPORT			160 /* RO */
#define EXT_CSD_HPI_MGMT					161 /* R/W */
#define EXT_CSD_RST_N_FUNCTION				162 /* R/W */
#define EXT_CSD_BKOPS_EN					163 /* R/W */
#define EXT_CSD_BKOPS_START					164 /* W */
#define EXT_CSD_SANITIZE_START				165 /* W */
#define EXT_CSD_WR_REL_PARAM				166 /* RO */
#define EXT_CSD_RPMB_MULT					168 /* RO */
#define EXT_CSD_FW_CONFIG					169 /* R/W */
#define EXT_CSD_BOOT_WP						173 /* R/W */
#define EXT_CSD_ERASE_GROUP_DEF				175 /* R/W */
#define EXT_CSD_PART_CONFIG					179 /* R/W */
#define EXT_CSD_ERASED_MEM_CONT				181 /* RO */
#define EXT_CSD_BUS_WIDTH					183 /* R/W */
#define EXT_CSD_STROBE_SUPPORT				184 /* RO */
#define EXT_CSD_HS_TIMING					185 /* R/W */
#define EXT_CSD_POWER_CLASS					187 /* R/W */
#define EXT_CSD_REV							192 /* RO */
#define EXT_CSD_STRUCTURE					194 /* RO */
#define EXT_CSD_CARD_TYPE					196 /* RO */
#define EXT_CSD_DRIVER_STRENGTH				197 /* RO */
#define EXT_CSD_OUT_OF_INTERRUPT_TIME		198 /* RO */
#define EXT_CSD_PART_SWITCH_TIME			199 /* RO */
#define EXT_CSD_PWR_CL_52_195				200 /* RO */
#define EXT_CSD_PWR_CL_26_195				201 /* RO */
#define EXT_CSD_PWR_CL_52_360				202 /* RO */
#define EXT_CSD_PWR_CL_26_360				203 /* RO */
#define EXT_CSD_SEC_CNT						212 /* RO, 4 bytes */
#define EXT_CSD_S_A_TIMEOUT					217 /* RO */
#define EXT_CSD_REL_WR_SEC_C				222 /* RO */
#define EXT_CSD_HC_WP_GRP_SIZE				221 /* RO */
#define EXT_CSD_ERASE_TIMEOUT_MULT			223 /* RO */
#define EXT_CSD_HC_ERASE_GRP_SIZE			224 /* RO */
#define EXT_CSD_BOOT_MULT					226 /* RO */
#define EXT_CSD_SEC_TRIM_MULT				229 /* RO */
#define EXT_CSD_SEC_ERASE_MULT				230 /* RO */
#define EXT_CSD_SEC_FEATURE_SUPPORT			231 /* RO */
#define EXT_CSD_TRIM_MULT					232 /* RO */
#define EXT_CSD_PWR_CL_200_195				236 /* RO */
#define EXT_CSD_PWR_CL_200_360				237 /* RO */
#define EXT_CSD_PWR_CL_DDR_52_195			238 /* RO */
#define EXT_CSD_PWR_CL_DDR_52_360			239 /* RO */
#define EXT_CSD_BKOPS_STATUS				246 /* RO */
#define EXT_CSD_POWER_OFF_LONG_TIME			247 /* RO */
#define EXT_CSD_GENERIC_CMD6_TIME			248 /* RO */
#define EXT_CSD_CACHE_SIZE					249 /* RO, 4 bytes */
#define EXT_CSD_PWR_CL_DDR_200_360			253 /* RO */
#define EXT_CSD_FIRMWARE_VERSION			254 /* RO, 8 bytes */
#define EXT_CSD_PRE_EOL_INFO				267 /* RO */
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A	268 /* RO */
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B	269 /* RO */
#define EXT_CSD_CMDQ_DEPTH					307 /* RO */
#define EXT_CSD_CMDQ_SUPPORT				308 /* RO */
#define EXT_CSD_SUPPORTED_MODE				493 /* RO */
#define EXT_CSD_TAG_UNIT_SIZE				498 /* RO */
#define EXT_CSD_DATA_TAG_SUPPORT			499 /* RO */
#define EXT_CSD_MAX_PACKED_WRITES			500 /* RO */
#define EXT_CSD_MAX_PACKED_READS			501 /* RO */
#define EXT_CSD_BKOPS_SUPPORT				502 /* RO */
#define EXT_CSD_HPI_FEATURES				503 /* RO */

#define EXT_CSD_BUS_WIDTH_1		0 /* Card is in 1 bit mode */
#define EXT_CSD_BUS_WIDTH_4		1 /* Card is in 4 bit mode */
#define EXT_CSD_BUS_WIDTH_8		2 /* Card is in 8 bit mode */
#define EXT_CSD_DDR_BUS_WIDTH_4 5 /* Card is in 4 bit DDR mode */
#define EXT_CSD_DDR_BUS_WIDTH_8 6 /* Card is in 8 bit DDR mode */

#define EXT_CSD_TIMING_BC	0
#define EXT_CSD_TIMING_HS	1

#define MMC_SWITCH_MODE_WRITE_BYTE (3U << 24)
#define MMC_STATUS_READY_FOR_DATA   (1U << 8)
#define MMC_STATUS_SWITCH_ERROR		(1U << 7)
#define MMC_STATUS_ERROR_MASK                                                                                          \
	((1U << 31) | (1U << 30) | (1U << 29) | (1U << 28) | (1U << 27) | (1U << 26) | (1U << 25) | (1U << 24) |       \
	 (1U << 23) | (1U << 22) | (1U << 21) | (1U << 20) | (1U << 19) | (1U << 18) | (1U << 17) | (1U << 16) |       \
	 (1U << 15) | (1U << 13) | MMC_STATUS_SWITCH_ERROR | (1U << 3))

#define MMC_WRITE_TIMEOUT_MS 6000U
#define MMC_FLUSH_TIMEOUT_MS 30000U
#define MMC_READ_STATE_TIMEOUT_MS 6000U

#define SD_DEFAULT_SPEED_HZ			  25000000U
#define SD_SWITCH_CHECK_ARGUMENT	  0x00fffff1U
#define SD_SWITCH_SET_ARGUMENT		  0x80fffff1U
#define SD_SWITCH_STATUS_SIZE		  64U
#define SD_SWITCH_HIGH_SPEED_SUPPORT  0x02U
#define SD_SWITCH_HIGH_SPEED_FUNCTION 0x01U
#define SD_SWITCH_CHECK_ATTEMPTS	  4U
#define SD_CCC_SWITCH				  (1U << 10)

#define EXT_CSD_CARD_TYPE_26	   (1 << 0) /* Card can run at 26MHz */
#define EXT_CSD_CARD_TYPE_52	   (1 << 1) /* Card can run at 52MHz */
#define EXT_CSD_CARD_TYPE_MASK	   0x3F /* Mask out reserved bits */
#define EXT_CSD_CARD_TYPE_DDR_1_8V (1 << 2) /* Card can run at 52MHz */
/* DDR mode @1.8V or 3V I/O */
#define EXT_CSD_CARD_TYPE_DDR_1_2V (1 << 3) /* Card can run at 52MHz */
/* DDR mode @1.2V I/O */
#define EXT_CSD_CARD_TYPE_DDR_52   (EXT_CSD_CARD_TYPE_DDR_1_8V | EXT_CSD_CARD_TYPE_DDR_1_2V)
#define EXT_CSD_CARD_TYPE_SDR_1_8V (1 << 4) /* Card can run at 200MHz */
#define EXT_CSD_CARD_TYPE_SDR_1_2V (1 << 5) /* Card can run at 200MHz */
/* SDR mode @1.2V I/O */

#define EXT_CSD_CMD_SET_NORMAL	 (1 << 0)
#define EXT_CSD_CMD_SET_SECURE	 (1 << 1)
#define EXT_CSD_CMD_SET_CPSECURE (1 << 2)

#define EXT_CSD_PWR_CL_8BIT_MASK  0xF0 /* 8 bit PWR CLS */
#define EXT_CSD_PWR_CL_4BIT_MASK  0x0F /* 8 bit PWR CLS */
#define EXT_CSD_PWR_CL_8BIT_SHIFT 4
#define EXT_CSD_PWR_CL_4BIT_SHIFT 0

sdmmc_pdata_t card0;

#define UNSTUFF_BITS(resp, start, size)                              \
	({                                                               \
		const int	   __size = size;                                \
		const uint32_t __mask = (__size < 32 ? 1 << __size : 0) - 1; \
		const int	   __off  = 3 - ((start) / 32);                  \
		const int	   __shft = (start) & 31;                        \
		uint32_t	   __res;                                        \
                                                                     \
		__res = resp[__off] >> __shft;                               \
		if (__size + __shft > 32)                                    \
			__res |= resp[__off - 1] << ((32 - __shft) % 32);        \
		__res & __mask;                                              \
	})

static const unsigned tran_speed_unit[] = {
	[0] = 10000,
	[1] = 100000,
	[2] = 1000000,
	[3] = 10000000,
};

static const unsigned char tran_speed_time[] = {
	0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80,
};

static bool go_idle_state(sdhci_t *hci)
{
	sdhci_cmd_t cmd = {0};

	// CMD0 0x0 for user partition
	cmd.idx		 = MMC_GO_IDLE_STATE;
	cmd.arg		 = 0;
	cmd.resptype = hci->isspi ? MMC_RSP_R1 : MMC_RSP_NONE;

	return sdhci_transfer(hci, &cmd, NULL);
}

#if CONFIG_BOOT_SDCARD
static bool sd_send_if_cond(sdhci_t *hci, sdmmc_t *card)
{
	sdhci_cmd_t cmd = {0};

	cmd.idx = SD_CMD_SEND_IF_COND;
	if (hci->voltage & MMC_VDD_27_36)
		cmd.arg = (0x1 << 8);
	else if (hci->voltage & MMC_VDD_165_195)
		cmd.arg = (0x2 << 8);
	else
		cmd.arg = (0x0 << 8);
	cmd.arg |= 0xaa;
	cmd.resptype = MMC_RSP_R7;
	if (!sdhci_transfer(hci, &cmd, NULL))
		return FALSE;

	if ((cmd.response[0] & 0xff) != 0xaa)
		return FALSE;
	card->version = SD_VERSION_2;
	return TRUE;
}

static bool sd_send_op_cond(sdhci_t *hci, sdmmc_t *card)
{
	sdhci_cmd_t cmd		= {0};
	int			retries = 50;

	if (!go_idle_state(hci))
		return FALSE;

	if (!sd_send_if_cond(hci, card)) {
		return FALSE;
	}

	do {
		cmd.idx		 = MMC_APP_CMD;
		cmd.arg		 = 0;
		cmd.resptype = MMC_RSP_R1;
		if (!sdhci_transfer(hci, &cmd, NULL))
			continue;

		cmd.idx = SD_CMD_APP_SEND_OP_COND;
		if (hci->isspi) {
			cmd.arg = 0;
			if (card->version == SD_VERSION_2)
				cmd.arg |= OCR_HCS;
			cmd.resptype = MMC_RSP_R1;
			if (sdhci_transfer(hci, &cmd, NULL))
				break;
		} else {
			if (hci->voltage & MMC_VDD_27_36)
				cmd.arg = 0x00ff8000;
			else if (hci->voltage & MMC_VDD_165_195)
				cmd.arg = 0x00000080;
			else
				cmd.arg = 0;
			if (card->version == SD_VERSION_2)
				cmd.arg |= OCR_HCS;
			cmd.resptype = MMC_RSP_R3;
			if (!sdhci_transfer(hci, &cmd, NULL) || (cmd.response[0] & OCR_BUSY))
				break;
		}
	} while (retries--);

	if (retries <= 0)
		return FALSE;

	if (card->version != SD_VERSION_2)
		card->version = SD_VERSION_1_0;
	if (hci->isspi) {
		cmd.idx		 = MMC_SPI_READ_OCR;
		cmd.arg		 = 0;
		cmd.resptype = MMC_RSP_R3;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
	}
	card->ocr			= cmd.response[0];
	card->high_capacity = ((card->ocr & OCR_HCS) == OCR_HCS);
	card->rca			= 0;

	return TRUE;
}
#endif

#if CONFIG_BOOT_SDCARD
static bool sd_enable_high_speed(sdhci_t *hci, sdmmc_t *card)
{
	sdhci_cmd_t  cmd = {0};
	sdhci_data_t dat = {0};
	u32		 switch_status_words[SD_SWITCH_STATUS_SIZE / sizeof(u32)] = {0};
	u8		*switch_status = (u8 *)switch_status_words;
	u32		 attempt;

	if ((UNSTUFF_BITS(card->csd, 84, 12) & SD_CCC_SWITCH) == 0U) {
		debug("SMHC: SD card does not advertise switch-function support\r\n");
		return false;
	}

	cmd.idx		 = SD_CMD_SWITCH_FUNC;
	cmd.resptype = MMC_RSP_R1;
	dat.buf		 = switch_status;
	dat.flag	 = MMC_DATA_READ | MMC_DATA_PIO;
	dat.blksz	 = SD_SWITCH_STATUS_SIZE;
	dat.blkcnt	 = 1U;

	for (attempt = 0U; attempt < SD_SWITCH_CHECK_ATTEMPTS; attempt++) {
		memset(switch_status, 0, SD_SWITCH_STATUS_SIZE);
		cmd.arg = SD_SWITCH_CHECK_ARGUMENT;
		if (!sdhci_transfer(hci, &cmd, &dat) || (cmd.response[0] & MMC_STATUS_ERROR_MASK) != 0U) {
			debug("SMHC: SD CMD6 query failed\r\n");
			return false;
		}
		if ((switch_status[29] & SD_SWITCH_HIGH_SPEED_SUPPORT) == 0U)
			break;
	}

	if ((switch_status[29] & SD_SWITCH_HIGH_SPEED_SUPPORT) != 0U) {
		debug("SMHC: SD CMD6 high-speed function remained busy\r\n");
		return false;
	}
	if ((switch_status[13] & SD_SWITCH_HIGH_SPEED_SUPPORT) == 0U) {
		debug("SMHC: SD card does not support high-speed timing\r\n");
		return false;
	}

	memset(switch_status, 0, SD_SWITCH_STATUS_SIZE);
	cmd.arg = SD_SWITCH_SET_ARGUMENT;
	if (!sdhci_transfer(hci, &cmd, &dat) || (cmd.response[0] & MMC_STATUS_ERROR_MASK) != 0U) {
		debug("SMHC: SD CMD6 switch failed\r\n");
		return false;
	}
	if ((switch_status[16] & 0x0fU) != SD_SWITCH_HIGH_SPEED_FUNCTION) {
		debug("SMHC: SD CMD6 high-speed function not selected\r\n");
		return false;
	}

	return true;
}
#endif

#if CONFIG_BOOT_MMC
static bool mmc_send_op_cond(sdhci_t *hci, sdmmc_t *card)
{
	sdhci_cmd_t cmd		= {0};
	int			retries = 50;

	cmd.idx		 = MMC_SEND_OP_COND;
	cmd.resptype = MMC_RSP_R3;
	if (hci->voltage == MMC_VDD_27_36) {
		cmd.arg = 0x40FF8000; // Sector access mode, 2.7-3.6v VCCQ
	} else if (hci->voltage == MMC_VDD_165_195) {
		cmd.arg = 0x40FF0080; // Sector access mode, 1.65-1.95v VCCQ
	}

	do {
		cmd.response[0] = 0;
		if (!sdhci_transfer(hci, &cmd, NULL)) {
			return FALSE;
		}
		udelay(5000);
	} while (!(cmd.response[0] & OCR_BUSY) && retries--);
	trace("SHMC: op_cond 0x%" PRIx32 "\r\n", cmd.response[0]);

	if (retries <= 0)
		return FALSE;

	if (hci->isspi) {
		cmd.idx		 = MMC_SPI_READ_OCR;
		cmd.arg		 = 0;
		cmd.resptype = MMC_RSP_R3;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
	}
	card->version		= MMC_VERSION_UNKNOWN;
	card->ocr			= cmd.response[0];
	card->high_capacity = ((card->ocr & OCR_HCS) == OCR_HCS);
	card->rca			= 1;
	return TRUE;
}
#endif

static int sdmmc_status(sdhci_t *hci, sdmmc_t *card)
{
	sdhci_cmd_t cmd		= {0};
	int			retries = 100;

	cmd.idx		 = MMC_SEND_STATUS;
	cmd.resptype = MMC_RSP_R1;
	cmd.arg		 = card->rca << 16;
	do {
		if (!sdhci_transfer(hci, &cmd, NULL))
			continue;
		if (cmd.response[0] & (1 << 8))
			break;
		udelay(1);
	} while (retries-- > 0);
	if (retries > 0)
		return ((cmd.response[0] >> 9) & 0xf);
	warning("SMHC: status error\r\n");
	return -1;
}

static bool sdmmc_status_once(sdhci_t *hci, sdmmc_t *card, uint32_t *status)
{
	sdhci_cmd_t cmd = {0};

	cmd.idx		 = MMC_SEND_STATUS;
	cmd.resptype = MMC_RSP_R1;
	cmd.arg		 = card->rca << 16;
	if (!sdhci_transfer(hci, &cmd, NULL))
		return false;
	*status = cmd.response[0];
	return true;
}

static bool sdmmc_wait_ready(sdmmc_pdata_t *data, uint32_t timeout_ms)
{
	const uint32_t start = time_ms();
	sdhci_cmd_t	   cmd;
	sdhci_t		  *hci  = data->hci;
	sdmmc_t		  *card = &data->card;

	do {
		memset(&cmd, 0, sizeof(cmd));
		cmd.idx		 = MMC_SEND_STATUS;
		cmd.resptype = MMC_RSP_R1;
		cmd.arg		 = card->rca << 16;
		if (sdhci_transfer(hci, &cmd, NULL)) {
			const uint32_t status = cmd.response[0];
			if ((status & MMC_STATUS_ERROR_MASK) != 0U) {
				warning("SMHC: card status error 0x%08" PRIx32 "\r\n", status);
				data->online = false;
				return false;
			}
			if ((status & MMC_STATUS_READY_FOR_DATA) != 0U &&
				((status >> 9) & 0xfU) == MMC_STATUS_TRAN) {
				return true;
			}
		}
		mdelay(1);
	} while (time_ms() - start <= timeout_ms);

	warning("SMHC: card ready timeout\r\n");
	data->online = false;
	return false;
}

static bool mmc_refresh_ext_csd(sdmmc_pdata_t *data)
{
	sdhci_cmd_t  cmd = {0};
	sdhci_data_t dat = {0};

	cmd.idx		 = MMC_SEND_EXT_CSD;
	cmd.resptype = MMC_RSP_R1;
	dat.buf		 = data->card.extcsd;
	dat.flag	 = MMC_DATA_READ | MMC_DATA_PIO;
	dat.blksz	 = 512U;
	dat.blkcnt	 = 1U;
	if (!sdhci_transfer(data->hci, &cmd, &dat) || (cmd.response[0] & MMC_STATUS_ERROR_MASK) != 0U) {
		warning("SMHC: EXT_CSD refresh failed\r\n");
		data->online = false;
		return false;
	}

	return sdmmc_wait_ready(data, MMC_WRITE_TIMEOUT_MS);
}

static uint64_t sdmmc_read_blocks(sdhci_t *hci, sdmmc_t *card, uint8_t *buf, uint64_t start, uint64_t blkcnt,
								  bool pio)
{
	sdhci_cmd_t	 cmd = {0};
	sdhci_data_t dat = {0};
	uint32_t	 card_status = 0U;
	uint32_t	 state_start;

	if (blkcnt > 1)
		cmd.idx = MMC_READ_MULTIPLE_BLOCK;
	else
		cmd.idx = MMC_READ_SINGLE_BLOCK;
	if (card->high_capacity)
		cmd.arg = start;
	else
		cmd.arg = start * card->read_bl_len;
	cmd.resptype = MMC_RSP_R1;
	dat.buf		 = buf;
	dat.flag	 = MMC_DATA_READ;
	if (pio)
		dat.flag |= MMC_DATA_PIO;
	dat.blksz	 = card->read_bl_len;
	dat.blkcnt	 = blkcnt;

	if (!sdhci_transfer(hci, &cmd, &dat)) {
		warning("SMHC: read failed\r\n");
		return 0;
	}

	if (!hci->isspi) {
		state_start = time_ms();
		for (;;) {
			if (sdmmc_status_once(hci, card, &card_status)) {
				if ((card_status & MMC_STATUS_ERROR_MASK) != 0U) {
					warning("SMHC: read status error 0x%08" PRIx32 "\r\n", card_status);
					return 0;
				}
				if ((card_status & MMC_STATUS_READY_FOR_DATA) != 0U) {
					const uint32_t state = (card_status >> 9) & 0xfU;
					if (state == MMC_STATUS_TRAN || state == MMC_STATUS_DATA)
						break;
				}
			}
			if (time_ms() - state_start > MMC_READ_STATE_TIMEOUT_MS) {
				warning("SMHC: read state timeout (0x%08" PRIx32 ")\r\n", card_status);
				return 0;
			}
			mdelay(1);
		}
	}

	if (blkcnt > 1) {
		cmd.idx		 = MMC_STOP_TRANSMISSION;
		cmd.arg		 = 0;
		cmd.resptype = MMC_RSP_R1B;
		if (!sdhci_transfer(hci, &cmd, NULL)) {
			warning("SMHC: transfer stop failed\r\n");
			return 0;
		}
	}
	return blkcnt;
}

static bool sdmmc_detect(sdhci_t *hci, sdmmc_t *card)
{
	sdhci_cmd_t	 cmd = {0};
	sdhci_data_t dat = {0};
	uint64_t	 csize, cmult;
	uint32_t	 unit, time;
	int			 width;
	int			 status;

	sdhci_reset(hci);
	if (!sdhci_set_clock(hci, MMC_CLK_400K) || !sdhci_set_width(hci, MMC_BUS_WIDTH_1)) {
		error("SMHC: set clock/width failed\r\n");
		return FALSE;
	}

	if (!go_idle_state(hci)) {
		error("SMHC: set idle state failed\r\n");
		return FALSE;
	}
	udelay(2000); // 1ms + 74 clocks @ 400KHz (185us)

// Both SD & MMC: try SD first
// Otherwise there's only one media type if enabled
#if CONFIG_BOOT_SDCARD
	if (!sd_send_op_cond(hci, card)) {
#if CONFIG_BOOT_MMC
		sdhci_reset(hci);
		sdhci_set_clock(hci, MMC_CLK_400K);
		sdhci_set_width(hci, MMC_BUS_WIDTH_1);

		if (!go_idle_state(hci)) {
			error("SMHC: set idle state failed\r\n");
			return FALSE;
		}
		udelay(2000); // 1ms + 74 clocks @ 400KHz (185us)

		if (!mmc_send_op_cond(hci, card)) {
			debug("SMHC: SD/MMC detect failed\r\n");
			return FALSE;
		}
#else
		debug("SMHC: SD detect failed\r\n");
		return FALSE;
#endif
	}
// Only MMC
#elif CONFIG_BOOT_MMC
	// // Workaround for eMMC starting in alternative boot mode
	// sdhci_reset(hci);
	// if (!sdhci_set_clock(hci, MMC_CLK_400K) || !sdhci_set_width(hci, MMC_BUS_WIDTH_1)) {
	// 	error("SMHC: set clock/width failed\r\n");
	// 	return FALSE;
	// }

	// if (!go_idle_state(hci)) {
	// 	error("SMHC: set idle state failed\r\n");
	// 	return FALSE;
	// }
	// udelay(2000); // 1ms + 74 clocks @ 400KHz (185us)

	if (!mmc_send_op_cond(hci, card)) {
		debug("SMHC: MMC detect failed\r\n");
		return FALSE;
	}
#endif

	if (hci->isspi) {
		cmd.idx		 = MMC_SEND_CID;
		cmd.arg		 = 0;
		cmd.resptype = MMC_RSP_R1;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
		card->cid[0] = cmd.response[0];
		card->cid[1] = cmd.response[1];
		card->cid[2] = cmd.response[2];
		card->cid[3] = cmd.response[3];

		cmd.idx		 = MMC_SEND_CSD;
		cmd.arg		 = 0;
		cmd.resptype = MMC_RSP_R1;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
		card->csd[0] = cmd.response[0];
		card->csd[1] = cmd.response[1];
		card->csd[2] = cmd.response[2];
		card->csd[3] = cmd.response[3];
	} else {
		cmd.idx		 = MMC_ALL_SEND_CID;
		cmd.arg		 = 0;
		cmd.resptype = MMC_RSP_R2;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
		card->cid[0] = cmd.response[0];
		card->cid[1] = cmd.response[1];
		card->cid[2] = cmd.response[2];
		card->cid[3] = cmd.response[3];

		cmd.idx		 = SD_CMD_SEND_RELATIVE_ADDR;
		cmd.arg		 = card->rca << 16;
		cmd.resptype = MMC_RSP_R6;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
		if (card->version & SD_VERSION_SD)
			card->rca = (cmd.response[0] >> 16) & 0xffff;

		cmd.idx		 = MMC_SEND_CSD;
		cmd.arg		 = card->rca << 16;
		cmd.resptype = MMC_RSP_R2;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
		card->csd[0] = cmd.response[0];
		card->csd[1] = cmd.response[1];
		card->csd[2] = cmd.response[2];
		card->csd[3] = cmd.response[3];

		cmd.idx		 = MMC_SELECT_CARD;
		cmd.arg		 = card->rca << 16;
		cmd.resptype = MMC_RSP_R1;
		if (!sdhci_transfer(hci, &cmd, NULL))
			return FALSE;
		do {
			status = sdmmc_status(hci, card);
			if (status < 0)
				return FALSE;
		} while (status != MMC_STATUS_TRAN);
	}

	if (card->version == MMC_VERSION_UNKNOWN) {
		switch ((card->csd[0] >> 26) & 0xf) {
			case 0:
				card->version = MMC_VERSION_1_2;
				break;
			case 1:
				card->version = MMC_VERSION_1_4;
				break;
			case 2:
				card->version = MMC_VERSION_2_2;
				break;
			case 3:
				card->version = MMC_VERSION_3;
				break;
			case 4:
				card->version = MMC_VERSION_4;
				break;
			default:
				card->version = MMC_VERSION_1_2;
				break;
		};
	}

	unit			 = tran_speed_unit[(card->csd[0] & 0x7)];
	time			 = tran_speed_time[((card->csd[0] >> 3) & 0xf)];
	card->tran_speed = time * unit;
	card->dsr_imp	 = UNSTUFF_BITS(card->csd, 76, 1);

	card->read_bl_len = 1 << UNSTUFF_BITS(card->csd, 80, 4);
	if (card->version & SD_VERSION_SD)
		card->write_bl_len = card->read_bl_len;
	else
		card->write_bl_len = 1 << ((card->csd[3] >> 22) & 0xf);
	if (card->read_bl_len > 512)
		card->read_bl_len = 512;
	if (card->write_bl_len > 512)
		card->write_bl_len = 512;

	if ((card->version & MMC_VERSION_MMC) && (card->version >= MMC_VERSION_4)) {
		card->tran_speed = 50000000;
		cmd.idx			 = MMC_SEND_EXT_CSD;
		cmd.arg			 = 0;
		cmd.resptype	 = MMC_RSP_R1;
		dat.buf			 = card->extcsd;
		dat.flag		 = MMC_DATA_READ;
		dat.blksz		 = 512;
		dat.blkcnt		 = 1;
		if (!sdhci_transfer(hci, &cmd, &dat))
			return FALSE;
		if (!hci->isspi) {
			do {
				status = sdmmc_status(hci, card);
				if (status < 0)
					return FALSE;
			} while (status != MMC_STATUS_TRAN);
		}
		const char UNUSED_DEBUG *strver = "unknown";
		switch (card->extcsd[EXT_CSD_REV]) {
			case 1:
				card->version = MMC_VERSION_4_1;
				strver		  = "4.1";
				break;
			case 2:
				card->version = MMC_VERSION_4_2;
				strver		  = "4.2";
				break;
			case 3:
				card->version = MMC_VERSION_4_3;
				strver		  = "4.3";
				break;
			case 5:
				card->version = MMC_VERSION_4_41;
				strver		  = "4.41";
				break;
			case 6:
				card->version = MMC_VERSION_4_5;
				strver		  = "4.5";
				break;
			case 7:
				card->version = MMC_VERSION_5_0;
				strver		  = "5.0";
				break;
			case 8:
				card->version = MMC_VERSION_5_1;
				strver		  = "5.1";
				break;
			default:
				break;
		}
		debug("SMHC: MMC version %s\r\n", strver);
	}

	if (card->high_capacity) {
		if (card->version & SD_VERSION_SD) {
			csize		   = UNSTUFF_BITS(card->csd, 48, 22);
			card->capacity = (1 + csize) << 10;
		} else {
			card->capacity = (uint32_t)card->extcsd[EXT_CSD_SEC_CNT] |
							 ((uint32_t)card->extcsd[EXT_CSD_SEC_CNT + 1] << 8U) |
							 ((uint32_t)card->extcsd[EXT_CSD_SEC_CNT + 2] << 16U) |
							 ((uint32_t)card->extcsd[EXT_CSD_SEC_CNT + 3] << 24U);
		}
	} else {
		cmult		   = UNSTUFF_BITS(card->csd, 47, 3);
		csize		   = UNSTUFF_BITS(card->csd, 62, 12);
		card->capacity = (csize + 1) << (cmult + 2);
	}
	card->capacity *= 1 << UNSTUFF_BITS(card->csd, 80, 4);
	debug("SMHC: capacity %.1fGB\r\n", (f32)((f64)card->capacity / (f64)1000000000.0));

	if (hci->isspi) {
		if (!sdhci_set_clock(hci, min(card->tran_speed, hci->clock_wanted)) || !sdhci_set_width(hci, MMC_BUS_WIDTH_1)) {
			error("SMHC: set clock/width failed\r\n");
			return FALSE;
		}
	} else {
		if (card->version & SD_VERSION_SD) {
#if CONFIG_BOOT_SDCARD
			smhc_clk_t sd_clock = MMC_CLK_400K;

			if (hci->clock_wanted >= MMC_CLK_25M && card->tran_speed >= SD_DEFAULT_SPEED_HZ)
				sd_clock = MMC_CLK_25M;
			if (!sdhci_set_clock(hci, sd_clock)) {
				error("SMHC: SD legacy clock failed\r\n");
				return FALSE;
			}

			if (hci->width == MMC_BUS_WIDTH_4)
				width = 2;
			else
				width = 0;

			cmd.idx		 = MMC_APP_CMD;
			cmd.arg		 = card->rca << 16;
			cmd.resptype = MMC_RSP_R1;
			if (!sdhci_transfer(hci, &cmd, NULL) || (cmd.response[0] & MMC_STATUS_ERROR_MASK) != 0U)
				return FALSE;

			cmd.idx		 = SD_CMD_SWITCH_FUNC;
			cmd.arg		 = width;
			cmd.resptype = MMC_RSP_R1;
			if (!sdhci_transfer(hci, &cmd, NULL) || (cmd.response[0] & MMC_STATUS_ERROR_MASK) != 0U)
				return FALSE;
			if (!sdhci_set_width(hci, hci->width)) {
				error("SMHC: SD width failed\r\n");
				return FALSE;
			}

			if (hci->clock_wanted >= MMC_CLK_50M && sd_enable_high_speed(hci, card)) {
				if (!sdhci_set_clock(hci, MMC_CLK_50M)) {
					error("SMHC: SD HS clock failed\r\n");
					return FALSE;
				}
				info("SMHC: SD HS 50MHz\r\n");
			} else if (sd_clock == MMC_CLK_25M) {
				info("SMHC: SD legacy 25MHz\r\n");
			} else {
				info("SMHC: SD legacy 400KHz\r\n");
			}
#else
			return FALSE;
#endif
		} else if (card->version & MMC_VERSION_MMC) {
#if CONFIG_MMC_ENABLE_RSTN
			if (card->extcsd[EXT_CSD_RST_N_FUNCTION] == 0) {
				cmd.idx		 = SD_CMD_SWITCH_FUNC;
				cmd.resptype = MMC_RSP_R1;
				cmd.arg		 = (3 << 24) | (EXT_CSD_RST_N_FUNCTION << 16) | 1 << 8 | EXT_CSD_CMD_SET_NORMAL;

				if (sdhci_transfer(hci, &cmd, NULL)) {
					debug("SHMC: eMMC hardware reset signal enabled\r\n");
				} else {
					warning("SHMC: failed to enable eMMC hardware reset signal\r\n");
				}
			} else {
				debug("SHMC: eMMC hardware reset signal already enabled\r\n");
			}
#else
			debug("SMHC: eMMC hardware reset signal skipped\r\n");
#endif

			u8 card_type = card->extcsd[EXT_CSD_CARD_TYPE];
			bool want_ddr = hci->clock_wanted == MMC_CLK_50M_DDR;

			if (want_ddr && !(card_type & EXT_CSD_CARD_TYPE_DDR_52)) {
				warning("SMHC: controller requested DDR but card does not advertise DDR support, falling back to high-speed SDR\r\n");
				hci->clock_wanted = MMC_CLK_50M;
				want_ddr = false;
			}

			if (hci->clock_wanted >= MMC_CLK_50M) {
				if (!(card_type & (EXT_CSD_CARD_TYPE_52 | EXT_CSD_CARD_TYPE_DDR_52))) {
					warning("SMHC: card does not advertise high-speed timing, using 25MHz\r\n");
					hci->clock_wanted = MMC_CLK_25M;
					want_ddr = false;
				} else {
					cmd.idx		 = SD_CMD_SWITCH_FUNC;
					cmd.resptype = MMC_RSP_R1;
					cmd.arg		 = (3 << 24) | (EXT_CSD_HS_TIMING << 16) | (EXT_CSD_TIMING_HS << 8) | EXT_CSD_CMD_SET_NORMAL;
					if (!sdhci_transfer(hci, &cmd, NULL))
						return FALSE;
					udelay(1000);
					card->extcsd[EXT_CSD_HS_TIMING] = EXT_CSD_TIMING_HS;
				}
			}

			switch (hci->width) {
				case MMC_BUS_WIDTH_4:
					if (want_ddr)
						width = EXT_CSD_DDR_BUS_WIDTH_4;
					else
						width = EXT_CSD_BUS_WIDTH_4;
					break;
				default:
					width = EXT_CSD_BUS_WIDTH_1;
					break;
			}

			if (hci->width >= MMC_BUS_WIDTH_4) {
				cmd.idx		 = SD_CMD_SWITCH_FUNC;
				cmd.resptype = MMC_RSP_R1;
				cmd.arg		 = (3 << 24) | (EXT_CSD_POWER_CLASS << 16) | EXT_CSD_CMD_SET_NORMAL;
				if (width == EXT_CSD_DDR_BUS_WIDTH_4) {
					cmd.arg |= ((card->extcsd[EXT_CSD_PWR_CL_DDR_52_360] &
								 EXT_CSD_PWR_CL_4BIT_MASK >> EXT_CSD_PWR_CL_4BIT_SHIFT)
								<< 8);
				} else {
					cmd.arg |=
						((card->extcsd[EXT_CSD_PWR_CL_52_360] & EXT_CSD_PWR_CL_4BIT_MASK >> EXT_CSD_PWR_CL_4BIT_SHIFT)
						 << 8);
				}

				if (!sdhci_transfer(hci, &cmd, NULL))
					return FALSE;
			}

			// Write EXT_CSD register 183 (width) with our value
			cmd.idx		 = SD_CMD_SWITCH_FUNC;
			cmd.resptype = MMC_RSP_R1;
			cmd.arg		 = (3 << 24) | (EXT_CSD_BUS_WIDTH << 16) | (width << 8) | 1;
			if (!sdhci_transfer(hci, &cmd, NULL))
				return FALSE;

			udelay(1000);

			if (!sdhci_set_clock(hci, hci->clock_wanted) || !sdhci_set_width(hci, hci->width)) {
				error("SMHC: set clock/width failed\r\n");
				return FALSE;
			}
		}
	}

	cmd.idx		 = MMC_SET_BLOCKLEN;
	cmd.arg		 = card->read_bl_len;
	cmd.resptype = MMC_RSP_R1;
	if (!sdhci_transfer(hci, &cmd, NULL))
		return FALSE;

	return TRUE;
}

uint64_t sdmmc_blk_read(sdmmc_pdata_t *data, uint8_t *buf, uint64_t blkno, uint64_t blkcnt)
{
	uint64_t cnt, blks = blkcnt;
	sdmmc_t *sdcard;

	if (data == NULL || data->hci == NULL || !data->online || buf == NULL || blkcnt == 0U) {
		return 0;
	}
	sdcard = &data->card;

	while (blks > 0) {
		cnt = (blks > 127) ? 127 : blks;
		if (sdmmc_read_blocks(data->hci, sdcard, buf, blkno, cnt, false) != cnt) {
			data->online = false;
			return 0;
		}
		blks -= cnt;
		blkno += cnt;
		buf += cnt * sdcard->read_bl_len;
	}
	return blkcnt;
}

uint64_t sdmmc_blk_read_pio(sdmmc_pdata_t *data, uint8_t *buf, uint64_t blkno)
{
	if (data == NULL || data->hci == NULL || !data->online || buf == NULL || ((uintptr_t)buf & 3U) != 0U) {
		return 0;
	}
	if (sdmmc_read_blocks(data->hci, &data->card, buf, blkno, 1U, true) != 1U) {
		data->online = false;
		return 0;
	}
	return 1U;
}

bool sdmmc_blk_write(sdmmc_pdata_t *data, const uint8_t *buf, uint64_t blkno)
{
	sdmmc_t		*card;
	sdhci_cmd_t cmd = {0};
	sdhci_data_t dat = {0};

	if (data == NULL || data->hci == NULL || !data->online || buf == NULL || ((uintptr_t)buf & 3U) != 0U) {
		return false;
	}
	card = &data->card;
	if (card->write_bl_len != 512U || blkno >= card->capacity / card->write_bl_len) {
		return false;
	}

	cmd.idx		 = MMC_WRITE_SINGLE_BLOCK;
	cmd.arg		 = card->high_capacity ? (uint32_t)blkno : (uint32_t)(blkno * card->write_bl_len);
	cmd.resptype = MMC_RSP_R1;
	dat.buf		 = (uint8_t *)buf;
	dat.flag	 = MMC_DATA_WRITE | MMC_DATA_PIO;
	dat.blksz	 = card->write_bl_len;
	dat.blkcnt	 = 1U;

	if (!sdhci_transfer(data->hci, &cmd, &dat)) {
		warning("SMHC: write failed\r\n");
		data->online = false;
		return false;
	}
	if ((cmd.response[0] & MMC_STATUS_ERROR_MASK) != 0U) {
		warning("SMHC: write status error 0x%08" PRIx32 "\r\n", cmd.response[0]);
		data->online = false;
		return false;
	}

	return sdmmc_wait_ready(data, MMC_WRITE_TIMEOUT_MS);
}

bool sdmmc_sync(sdmmc_pdata_t *data)
{
	sdmmc_t		*card;
	sdhci_cmd_t cmd = {0};
	uint32_t	 cache_size;

	if (data == NULL || data->hci == NULL || !data->online) {
		return false;
	}
	card = &data->card;
	if (!sdmmc_wait_ready(data, MMC_WRITE_TIMEOUT_MS)) {
		return false;
	}
	if ((card->version & MMC_VERSION_MMC) == 0U) {
		return true;
	}
	if (card->version < MMC_VERSION_4) {
		return true;
	}
	if (!mmc_refresh_ext_csd(data)) {
		return false;
	}

	cache_size = (uint32_t)card->extcsd[EXT_CSD_CACHE_SIZE] |
			 ((uint32_t)card->extcsd[EXT_CSD_CACHE_SIZE + 1U] << 8U) |
			 ((uint32_t)card->extcsd[EXT_CSD_CACHE_SIZE + 2U] << 16U) |
			 ((uint32_t)card->extcsd[EXT_CSD_CACHE_SIZE + 3U] << 24U);
	if (cache_size == 0U || (card->extcsd[EXT_CSD_CACHE_CTRL] & 1U) == 0U) {
		return true;
	}

	cmd.idx			 = MMC_SWITCH;
	cmd.arg			 = MMC_SWITCH_MODE_WRITE_BYTE | (EXT_CSD_FLUSH_CACHE << 16) | (1U << 8) |
					   EXT_CSD_CMD_SET_NORMAL;
	cmd.resptype		 = MMC_RSP_R1B;
	cmd.busy_timeout_ms = MMC_FLUSH_TIMEOUT_MS;
	if (!sdhci_transfer(data->hci, &cmd, NULL)) {
		warning("SMHC: cache flush failed\r\n");
		data->online = false;
		return false;
	}
	if ((cmd.response[0] & MMC_STATUS_ERROR_MASK) != 0U) {
		warning("SMHC: cache flush status error 0x%08" PRIx32 "\r\n", cmd.response[0]);
		data->online = false;
		return false;
	}

	return sdmmc_wait_ready(data, MMC_FLUSH_TIMEOUT_MS);
}

int sdmmc_init(sdmmc_pdata_t *data, sdhci_t *hci)
{
	data->hci	 = hci;
	data->online = FALSE;
	int retries	 = 10;

	do {
		if (sdmmc_detect(data->hci, &data->card) == TRUE) {
			data->online = TRUE;
			info("SHMC: %s card detected\r\n", data->card.version & SD_VERSION_SD ? "SD" : "MMC");
			return 0;
		}
		mdelay(100);
	} while (retries--);

	return -1;
}
