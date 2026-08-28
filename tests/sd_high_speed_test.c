#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../include/types.h"

#define CONFIG_BOOT_SDCARD	   1
#define CONFIG_BOOT_MMC		   0
#define CONFIG_RAUC_EMMC	   0
#define CONFIG_MMC_ENABLE_RSTN 0
#define __BOARD_H__
#define __DEBUG_H__
#define __MAIN_H__
#define FALSE	  0
#define TRUE	  1
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define trace(...)
#define debug(...)
#define info(...)
#define warning(...)
#define error(...)
#define UNUSED_DEBUG __attribute__((__unused__))

void	*memset(void *dest, int value, size_t count);
void	 udelay(uint64_t us);
void	 mdelay(uint32_t ms);
uint32_t time_ms(void);

typedef struct {
	uint32_t command;
	uint32_t argument;
	uint32_t data_length;
	uint32_t clock;
	uint32_t width;
	uint32_t sequence;
} transfer_call_t;

#include "../arch/arm32/mach-t113s3/sdmmc.c"

typedef enum {
	SD_SWITCH_SUCCESS,
	SD_SWITCH_UNSUPPORTED,
	SD_SWITCH_BUSY,
	SD_SWITCH_QUERY_ERROR,
	SD_SWITCH_QUERY_R1_ERROR,
	SD_SWITCH_ERROR,
	SD_SWITCH_R1_ERROR,
	SD_SWITCH_MISMATCH,
	SD_SWITCH_APP_CMD_R1_ERROR,
	SD_SWITCH_ACMD6_R1_ERROR,
} sd_switch_scenario_t;

static transfer_call_t		transfer_calls[32];
static size_t				transfer_call_count;
static smhc_clk_t			clock_calls[8];
static size_t				clock_call_count;
static uint32_t				width_calls[8];
static uint32_t				width_sequences[8];
static size_t				width_call_count;
static uint32_t				active_width;
static uint32_t				sequence;
static sd_switch_scenario_t switch_scenario;
static bool					advertise_switch_function;

bool sdhci_reset(sdhci_t *hci)
{
	(void)hci;
	return true;
}

bool sdhci_set_clock(sdhci_t *hci, smhc_clk_t clock)
{
	assert(clock_call_count < sizeof(clock_calls) / sizeof(clock_calls[0]));
	clock_calls[clock_call_count++] = clock;
	hci->clock_active				= clock;
	return true;
}

bool sdhci_set_width(sdhci_t *hci, uint32_t width)
{
	(void)hci;
	assert(width_call_count < sizeof(width_calls) / sizeof(width_calls[0]));
	width_calls[width_call_count++] = width;
	width_sequences[width_call_count - 1U] = sequence++;
	active_width					= width;
	return true;
}

bool sdhci_transfer(sdhci_t *hci, sdhci_cmd_t *cmd, sdhci_data_t *dat)
{
	(void)hci;
	assert(transfer_call_count < sizeof(transfer_calls) / sizeof(transfer_calls[0]));
	transfer_calls[transfer_call_count++] = (transfer_call_t){
		.command	 = cmd->idx,
		.argument	 = cmd->arg,
		.data_length = dat == NULL ? 0U : dat->blksz * dat->blkcnt,
		.clock		 = hci->clock_active,
		.width		 = active_width,
		.sequence	 = sequence++,
	};
	if (cmd->idx == SD_CMD_SWITCH_FUNC && dat != NULL && dat->blksz * dat->blkcnt == 64U) {
		memset(dat->buf, 0, 64U);
		if ((cmd->arg & (1U << 31)) == 0U) {
			if (switch_scenario == SD_SWITCH_QUERY_ERROR)
				return false;
			if (switch_scenario == SD_SWITCH_QUERY_R1_ERROR)
				cmd->response[0] = 1U << 22;
			if (switch_scenario != SD_SWITCH_UNSUPPORTED)
				dat->buf[13] = 0x02U;
			if (switch_scenario == SD_SWITCH_BUSY)
				dat->buf[29] = 0x02U;
		} else {
			if (switch_scenario == SD_SWITCH_ERROR)
				return false;
			if (switch_scenario == SD_SWITCH_R1_ERROR)
				cmd->response[0] = 1U << 22;
			if (switch_scenario != SD_SWITCH_MISMATCH)
				dat->buf[16] = 0x01U;
		}
	}

	switch (cmd->idx) {
		case MMC_APP_CMD:
			if (switch_scenario == SD_SWITCH_APP_CMD_R1_ERROR && cmd->arg != 0U)
				cmd->response[0] = 1U << 22;
			break;
		case SD_CMD_SWITCH_FUNC:
			if (switch_scenario == SD_SWITCH_ACMD6_R1_ERROR && dat == NULL)
				cmd->response[0] = 1U << 22;
			break;
		case SD_CMD_SEND_IF_COND:
			cmd->response[0] = 0xaaU;
			break;
		case SD_CMD_APP_SEND_OP_COND:
			cmd->response[0] = OCR_BUSY | OCR_HCS;
			break;
		case SD_CMD_SEND_RELATIVE_ADDR:
			cmd->response[0] = 1U << 16;
			break;
		case MMC_SEND_CSD:
			cmd->response[0] = 0x32U;
			cmd->response[1] = (advertise_switch_function ? (1U << 30) : 0U) | (9U << 16);
			break;
		case MMC_SEND_STATUS:
			cmd->response[0] = MMC_STATUS_READY_FOR_DATA | (MMC_STATUS_TRAN << 9);
			break;
		default:
			break;
	}

	return true;
}

void udelay(uint64_t us)
{
	(void)us;
}

void mdelay(uint32_t ms)
{
	(void)ms;
}

uint32_t time_ms(void)
{
	static uint32_t now;
	return now++;
}

static void test_scenario(sd_switch_scenario_t scenario, bool advertise_switch, smhc_clk_t maximum_clock,
						  size_t expected_check_count, size_t expected_switch_count, bool expect_high_speed)
{
	sdhci_t hci = {
		.width		  = MMC_BUS_WIDTH_4,
		.clock_wanted = maximum_clock,
	};
	sdmmc_pdata_t data		   = {0};
	size_t		  check_count  = 0U;
	size_t		  switch_count = 0U;
	uint32_t	  acmd6_sequence = 0U;
	uint32_t	  first_cmd6_sequence = 0U;
	bool		  found_acmd6 = false;

	transfer_call_count		  = 0U;
	clock_call_count		  = 0U;
	width_call_count		  = 0U;
	active_width			  = 0U;
	sequence				  = 0U;
	switch_scenario			  = scenario;
	advertise_switch_function = advertise_switch;

	assert(sdmmc_init(&data, &hci) == 0);
	assert(clock_call_count == (expect_high_speed ? 3U : 2U));
	assert(clock_calls[0] == MMC_CLK_400K);
	assert(clock_calls[1] == MMC_CLK_25M);
	if (expect_high_speed)
		assert(clock_calls[2] == MMC_CLK_50M);
	assert(width_call_count == 2U);
	assert(width_calls[0] == MMC_BUS_WIDTH_1);
	assert(width_calls[1] == MMC_BUS_WIDTH_4);

	for (size_t i = 0U; i < transfer_call_count; i++) {
		if (transfer_calls[i].command == SD_CMD_SWITCH_FUNC && transfer_calls[i].data_length == 0U &&
			transfer_calls[i].argument == 2U) {
			acmd6_sequence = transfer_calls[i].sequence;
			found_acmd6 = true;
		}
		if (transfer_calls[i].command != SD_CMD_SWITCH_FUNC || transfer_calls[i].data_length != 64U)
			continue;
		if (check_count == 0U && switch_count == 0U)
			first_cmd6_sequence = transfer_calls[i].sequence;
		assert(transfer_calls[i].clock == MMC_CLK_25M);
		assert(transfer_calls[i].width == MMC_BUS_WIDTH_4);
		if ((transfer_calls[i].argument & (1U << 31)) == 0U) {
			assert(transfer_calls[i].argument == 0x00fffff1U);
			check_count++;
		} else {
			assert(transfer_calls[i].argument == 0x80fffff1U);
			switch_count++;
		}
	}
	assert(check_count == expected_check_count);
	assert(switch_count == expected_switch_count);
	assert(found_acmd6);
	assert(acmd6_sequence < width_sequences[1]);
	if (expected_check_count != 0U)
		assert(width_sequences[1] < first_cmd6_sequence);
}

static void test_bus_width_r1_error(sd_switch_scenario_t scenario)
{
	sdhci_t hci = {
		.width		  = MMC_BUS_WIDTH_4,
		.clock_wanted = MMC_CLK_50M,
	};
	sdmmc_t card = {0};

	transfer_call_count		  = 0U;
	clock_call_count		  = 0U;
	width_call_count		  = 0U;
	active_width			  = 0U;
	sequence				  = 0U;
	switch_scenario			  = scenario;
	advertise_switch_function = true;

	assert(!sdmmc_detect(&hci, &card));
	assert(width_call_count == 1U);
	assert(width_calls[0] == MMC_BUS_WIDTH_1);
}

int main(void)
{
	test_scenario(SD_SWITCH_SUCCESS, true, MMC_CLK_50M, 1U, 1U, true);
	test_scenario(SD_SWITCH_UNSUPPORTED, true, MMC_CLK_50M, 1U, 0U, false);
	test_scenario(SD_SWITCH_BUSY, true, MMC_CLK_50M, 4U, 0U, false);
	test_scenario(SD_SWITCH_QUERY_ERROR, true, MMC_CLK_50M, 1U, 0U, false);
	test_scenario(SD_SWITCH_QUERY_R1_ERROR, true, MMC_CLK_50M, 1U, 0U, false);
	test_scenario(SD_SWITCH_ERROR, true, MMC_CLK_50M, 1U, 1U, false);
	test_scenario(SD_SWITCH_R1_ERROR, true, MMC_CLK_50M, 1U, 1U, false);
	test_scenario(SD_SWITCH_MISMATCH, true, MMC_CLK_50M, 1U, 1U, false);
	test_scenario(SD_SWITCH_SUCCESS, false, MMC_CLK_50M, 0U, 0U, false);
	test_scenario(SD_SWITCH_SUCCESS, true, MMC_CLK_25M, 0U, 0U, false);
	test_bus_width_r1_error(SD_SWITCH_APP_CMD_R1_ERROR);
	test_bus_width_r1_error(SD_SWITCH_ACMD6_R1_ERROR);

	puts("SD high-speed negotiation tests passed");
	return 0;
}
