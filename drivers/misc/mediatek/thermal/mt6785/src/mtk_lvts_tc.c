// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 MediaTek Inc.
 */


/* #define DEBUG 1 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <mt-plat/aee.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <mt-plat/sync_write.h>
#include "mt-plat/mtk_thermal_monitor.h"
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "mach/mtk_thermal.h"
#include <linux/bug.h>

#if defined(CONFIG_MTK_CLKMGR)
#include <mach/mtk_clkmgr.h>
#else
#include <linux/clk.h>
#endif

#include <mt-plat/mtk_wd_api.h>
#include <mtk_gpu_utility.h>
#include <linux/time.h>

#define __MT_MTK_LVTS_TC_C__
#include <tscpu_settings.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif
#define __MT_MTK_LVTS_TC_C__

#include <mt-plat/mtk_devinfo.h>
#include "mtk_thermal_ipi.h"
/*=============================================================
 * Local variable definition
 *=============================================================
 */
/* chip dependent */
/*
 * TO-DO: I assume AHB bus frequecy is 78MHz.
 * Please confirm it.
 */
/*
 * PTP#	module		LVTS Plan
 * 0	MCU_LITTLE	LVTS2-0, 1, 2
 * 1	MCU_BIG		LVTS1-0, 1
 * 2	MCU_CCI		LVTS2-0, 1, 2
 * 3	MFG (GPU)	LVTS3-0, 1
 * 4	VPU		LVTS4-0
 * No PTP bank 5
 * 6	TOP		LVTS4-0; LVTS3-0,1
 * 7	MD		LVTS9-0
 */
struct lvts_thermal_controller lvts_tscpu_g_tc[LVTS_CONTROLLER_NUM] = {
	[0] = {
		.ts = {L_TS_LVTS1_0, L_TS_LVTS1_1},
		.ts_number = 2,
		.dominator_ts_idx = 1,
		.tc_offset = 0x0,
		.tc_speed = {
			0x001,
			0x00C,
			0x001,
			0x001,
		}
	},
	[1] = {
		.ts = {L_TS_LVTS2_0, L_TS_LVTS2_1, L_TS_LVTS2_2},
		.ts_number = 3,
		.dominator_ts_idx = 1,
		.tc_offset = 0x100,
		.tc_speed = {
			0x001,
			0x007,
			0x001,
			0x001,
		}
	},
	[2] = {
		.ts = {L_TS_LVTS3_0, L_TS_LVTS3_1},
		.ts_number = 2,
		.dominator_ts_idx = 0,
		.tc_offset = 0x200,
		.tc_speed = {
			0x001,
			0x00C,
			0x001,
			0x001,
		}
	},
	[3] = {
		.ts = {L_TS_LVTS4_0},
		.ts_number = 1,
		.dominator_ts_idx = 0,
		.tc_offset = 0x300,
		.tc_speed = {
			0x005,
			0x00C,
			0x001,
			0x001,
		}
	}
};

static unsigned int g_golden_temp;
static unsigned int g_count_r[L_TS_LVTS_NUM];
static unsigned int g_count_rc[L_TS_LVTS_NUM];
static unsigned int g_count_rc_now[L_TS_LVTS_NUM];
static int g_use_fake_efuse;
int lvts_debug_log;
int lvts_rawdata_debug_log;

#if CONFIG_LVTS_ERROR_AEE_WARNING
#if DUMP_LVTS_REGISTER
#define NUM_LVTS_DEVICE_REG (5)
static const unsigned int g_lvts_device_addrs[NUM_LVTS_DEVICE_REG] = {
	0x00,
	0x04,
	0x08,
	0x0C,
	0xF0};

static unsigned int g_lvts_device_value_b[LVTS_CONTROLLER_NUM]
	[NUM_LVTS_DEVICE_REG];
static unsigned int g_lvts_device_value_e[LVTS_CONTROLLER_NUM]
	[NUM_LVTS_DEVICE_REG];

#define NUM_LVTS_CONTROLLER_REG (9)
static const unsigned int g_lvts_controller_addrs[NUM_LVTS_CONTROLLER_REG] = {
	0x00,
	0x04,
	0x08,
	0x38,
	0x40,
	0x4C,
	0x50,
	0xE8,
	0xE4};
static unsigned int g_lvts_controller_value_b[LVTS_CONTROLLER_NUM]
	[NUM_LVTS_CONTROLLER_REG];
static unsigned int g_lvts_controller_value_e[LVTS_CONTROLLER_NUM]
	[NUM_LVTS_CONTROLLER_REG];
#endif
#endif

#if LVTS_VALID_DATA_TIME_PROFILING
unsigned long long int SODI3_count, noValid_count;
/* If isTempValid is 0, it means no valid temperature data
 * between two SODI3 entry points.
 */
int isTempValid;
/* latency_array
 * {a, b}
 * a: a time threshold in milliseconds. if it is -1, it means others.
 * b: the number of valid temperature latencies from a phone enters SODI3 to
 *    to a phone gets a valid temperature of any sensor.
 *    It is possible a phone enters SODI3 several times without a valid
 *    temperature data.
 */
#define NUM_TIME_TH (16)
static unsigned int latency_array[NUM_TIME_TH][2] = {
	{100, 0},
	{200, 0},
	{300, 0},
	{400, 0},
	{500, 0},
	{600, 0},
	{700, 0},
	{800, 0},
	{900, 0},
	{1000, 0},
	{2000, 0},
	{3000, 0},
	{4000, 0},
	{5000, 0},
	{10000, 0},
	{-1, 0}
};
long long int start_timestamp;
static long long int end_timestamp, time_diff;
/* count if start_timestamp is bigger than end_timestamp */
int diff_error_count;
#endif

#if CFG_THERM_LVTS
#define DEFAULT_EFUSE_GOLDEN_TEMP		(50)
#define DEFAULT_EFUSE_COUNT_R			(19000)
#define DEFAULT_EFUSE_COUNT_RC			(5350)
#define FAKE_EFUSE_VALUE			0x2B048500
#define LVTS_COEFF_A_X_1000			(-204650) //-204.65
#define LVTS_COEFF_B_X_1000			 (204650) // 204.65
#endif

/*=============================================================
 * Local function declartation
 *=============================================================
 */
static unsigned int  lvts_temp_to_raw(int ret, enum lvts_sensor_enum ts_name);

static void lvts_set_tc_trigger_hw_protect(
		int temperature, int temperature2, unsigned int tc_num);
/*=============================================================
 *Weak functions
 *=============================================================
 */
	void __attribute__ ((weak))
mt_ptp_lock(unsigned long *flags)
{
	pr_err("[Power/CPU_Thermal]%s doesn't exist\n", __func__);
}

	void __attribute__ ((weak))
mt_ptp_unlock(unsigned long *flags)
{
	pr_err("[Power/CPU_Thermal]%s doesn't exist\n", __func__);
}

	int __attribute__ ((weak))
get_wd_api(struct wd_api **obj)
{
	pr_err("[Power/CPU_Thermal]%s doesn't exist\n", __func__);
	return -1;
}

void mt_reg_sync_writel_print(unsigned int val, void *addr)
{
	if (lvts_debug_log)
		lvts_dbg_printk("### LVTS_REG: addr 0x%p, val 0x%x\n",
								addr, val);

	mt_reg_sync_writel(val, addr);
}

/*=============================================================*/

static int lvts_write_device(unsigned int config, unsigned int dev_reg_idx,
unsigned int data, unsigned int tc_num)
{
	int offset;

	dev_reg_idx &= 0xFF;
	data &= 0xFF;

	config = config | (dev_reg_idx << 8) | data;

	offset = lvts_tscpu_g_tc[tc_num].tc_offset;

	mt_reg_sync_writel_print(config, LVTS_CONFIG_0 + offset);

	/*
	 * LVTS Device Register Setting take 1us(by 26MHz clock source)
	 * interface latency to access.
	 * So we set 2~3us delay could guarantee access complete.
	 */
	udelay(3);

	return 1;
}

static unsigned int lvts_read_device(unsigned int config,
unsigned int dev_reg_idx, unsigned int tc_num)
{
	int offset, cnt;
	unsigned int data;

	dev_reg_idx &= 0xFF;

	config = config | (dev_reg_idx << 8) | 0x00;

	offset = lvts_tscpu_g_tc[tc_num].tc_offset;

	mt_reg_sync_writel_print(config, LVTS_CONFIG_0 + offset);

	/* wait 2us + 3us buffer*/
	udelay(5);
	/* Check ASIF bus status for transaction finished
	 * Wait until DEVICE_ACCESS_START = 0
	 */
	cnt = 0;
	while ((readl(LVTS_CONFIG_0 + offset) & _BIT_(24))) {
		cnt++;

		if (cnt == 100) {
			lvts_printk("Error: DEVICE_ACCESS_START didn't ready\n");
			break;
		}
		udelay(2);
	}

	data = (readl(LVTSRDATA0_0 + offset));

	return data;
}
int lvts_raw_to_temp(unsigned int msr_raw, enum lvts_sensor_enum ts_name)
{
	/* This function returns degree mC
	 * temp[i] = a * MSR_RAW/16384 + GOLDEN_TEMP/2 + b
	 * a = -204.65
	 * b =  204.65
	 */
	int temp_mC = 0;
	int temp1 = 0;

	temp1 = (LVTS_COEFF_A_X_1000 * ((unsigned long long int)msr_raw)) >> 14;

	temp_mC = temp1 + g_golden_temp * 500 + LVTS_COEFF_B_X_1000;

	return temp_mC;
}

static void lvts_device_check_counting_status(unsigned int tc_num)
{
	/* Check this when LVTS device is counting for
	 * a temperature or a RC now
	 */

	int offset, cnt;

	offset = lvts_tscpu_g_tc[tc_num].tc_offset; //tc offset

	cnt = 0;
	while ((readl(LVTS_CONFIG_0 + offset) & _BIT_(25))) {
		cnt++;

		if (cnt == 100) {
			lvts_printk("Error: DEVICE_SENSING_STATUS didn't ready\n");
			break;
		}
		udelay(2);
	}
}

static void lvts_device_check_read_write_status(unsigned int tc_num)
{
	/* Check this when LVTS device is doing a register
	 * read or write operation
	 */

	int offset, cnt;

	offset = lvts_tscpu_g_tc[tc_num].tc_offset;

	cnt = 0;
	while ((readl(LVTS_CONFIG_0 + offset) & _BIT_(24))) {
		cnt++;

		if (cnt == 100) {
			lvts_printk("Error: DEVICE_ACCESS_START didn't ready\n");
			break;
		}
		udelay(2);
	}
}

void lvts_device_read_count_RC_N(void)
{
	/* Resistor-Capacitor Calibration */
	/* count_RC_N: count RC now */
	unsigned int i, j, num_ts, offset, data;
	int ret, s_index;
	char buffer[512];

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {

		offset = lvts_tscpu_g_tc[i].tc_offset;
		num_ts = lvts_tscpu_g_tc[i].ts_number;

		for (j = 0; j < num_ts; j++) {
			s_index = lvts_tscpu_g_tc[i].ts[j];

			/* Select sensor-N with RCK */
			lvts_write_device(0x81030000, 0x0D, j, i);
			/* Set Device Single mode */
			lvts_write_device(0x81030000, 0x06, 0x78, i);
			/* Wait 8us for device settle + 2us buffer*/
			udelay(10);
			/* Kick-off RCK counting */
			lvts_write_device(0x81030000, 0x03, 0x02, i);
			/* wait 1ms */
#if CONFIG_LVTS_ERROR_AEE_WARNING
#if DUMP_LVTS_REGISTER
			udelay(1000);
#else
			usleep_range(1000, 2000);
#endif
#else
			usleep_range(1000, 2000);
#endif
			lvts_device_check_counting_status(i);

			/* Get RCK count data (sensor-N) */
			data = lvts_read_device(0x81020000, 0x00, i);
			/* wait 2us + 3us buffer*/
			udelay(5);

			lvts_device_check_read_write_status(i);

			/* Get RCK value from LSB[23:0] */
			g_count_rc_now[s_index] = (data & _BITMASK_(23:0));
		}

		/* Recover Setting for Normal Access on
		 * temperature fetch
		 */
		/* Select Sensor-N without RCK */
		lvts_write_device(0x81030000, 0x0D, 0x10, i);
	}

	ret = sprintf(buffer, "[COUNT_RC_NOW] ");
	if (ret < 0)
		return;

	for (i = 0; i < L_TS_LVTS_NUM; i++) {
		ret = sprintf(buffer + offset, "%d:%d ",
				i, g_count_rc_now[i]);
		if (ret < 0)
			return;

		offset += ret;
	}

	buffer[offset] = '\0';
	lvts_printk("%s\n", buffer);

#if CONFIG_LVTS_ERROR_AEE_WARNING
#if DUMP_LVTS_REGISTER
	read_device_reg_before_active();
#endif
#endif
}

void lvts_device_enable_auto_rck(void)
{
	int i;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		/* Set LVTS AUTO-RCK operation */
		lvts_write_device(0x81030000, 0x0E, 0x01, i);
	}
}

void lvts_efuse_setting(void)
{
	__u32 offset;
	int i, j, s_index;
	int efuse_data;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		for (j = 0; j < lvts_tscpu_g_tc[i].ts_number; j++) {

			offset = lvts_tscpu_g_tc[i].tc_offset;
			s_index = lvts_tscpu_g_tc[i].ts[j];

#if LVTS_DEVICE_AUTO_RCK == 0
			efuse_data = g_count_rc_now[s_index] *
				g_count_r[s_index];
#else
			efuse_data = g_count_r[s_index];
#endif

			switch (j) {
			case 0:
				mt_reg_sync_writel_print(efuse_data,
					LVTSEDATA00_0 + offset);
				lvts_dbg_printk("efuse LVTSEDATA00_%d 0x%x\n",
					i, readl(LVTSEDATA00_0 + offset));
				break;

			case 1:
				mt_reg_sync_writel_print(efuse_data,
					LVTSEDATA01_0 + offset);
				lvts_dbg_printk("efuse LVTSEDATA01_%d 0x%x\n",
					i, readl(LVTSEDATA01_0 + offset));
				break;
			case 2:
				mt_reg_sync_writel_print(efuse_data,
					LVTSEDATA02_0 + offset);
				lvts_dbg_printk("efuse LVTSEDATA02_%d 0x%x\n",
					i, readl(LVTSEDATA02_0 + offset));
				break;
			case 3:
				mt_reg_sync_writel_print(efuse_data,
					LVTSEDATA03_0 + offset);
				lvts_dbg_printk("efuse LVTSEDATA03_%d 0x%x\n",
					i, readl(LVTSEDATA03_0 + offset));
				break;
			default:
				lvts_dbg_printk("%s, illegal ts order : %d!!\n",
								__func__, j);
			}
		}
	}

}

#if CONFIG_LVTS_ERROR_AEE_WARNING
void dump_efuse_data(void)
{
	int i, efuse, offset;
	char buffer[512];

	lvts_printk("[LVTS_ERROR][GOLDEN_TEMP][DUMP] %d\n", g_golden_temp);

	offset = sprintf(buffer, "[LVTS_ERROR][COUNT_R][DUMP] ");
	for (i = 0; i < L_TS_LVTS_NUM; i++)
		offset += sprintf(buffer + offset, "%d:%d ", i, g_count_r[i]);

	buffer[offset] = '\0';
	lvts_printk("%s\n", buffer);

	offset = sprintf(buffer, "[LVTS_ERROR][COUNT_RC][DUMP] ");
	for (i = 0; i < L_TS_LVTS_NUM; i++)
		offset += sprintf(buffer + offset, "%d:%d ", i, g_count_rc[i]);

	buffer[offset] = '\0';
	lvts_printk("%s\n", buffer);

	offset = sprintf(buffer, "[LVTS_ERROR][COUNT_RC_NOW][DUMP] ");
	for (i = 0; i < L_TS_LVTS_NUM; i++)
		offset += sprintf(buffer + offset, "%d:%d ",
				i, g_count_rc_now[i]);

	buffer[offset] = '\0';
	lvts_printk("%s\n", buffer);

	offset = sprintf(buffer, "[LVTS_ERROR][LVTSEDATA][DUMP] ");
	for (i = 0; i < L_TS_LVTS_NUM; i++) {
#if LVTS_DEVICE_AUTO_RCK == 0
		efuse = g_count_rc_now[i] * g_count_r[i];
#else
		efuse = g_count_r[i];
#endif
		offset += sprintf(buffer + offset, "%d:%d ", i, efuse);
	}

	buffer[offset] = '\0';
	lvts_printk("%s\n", buffer);
}

int check_lvts_mcu_efuse(void)
{
	return (g_use_fake_efuse)?(0):(1);
}
#if DUMP_LVTS_REGISTER
void read_controller_reg_before_active(void)
{
	int i, j, offset, temp;

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset;

		for (j = 0; j < NUM_LVTS_CONTROLLER_REG; j++) {
			temp = readl(LVTSMONCTL0_0 + g_lvts_controller_addrs[j]
				+ offset);
			g_lvts_controller_value_b[i][j] = temp;
		}
	}

}

void read_controller_reg_when_error(void)
{
	int i, j, offset, temp;

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset; //tc offset

		for (j = 0; j < NUM_LVTS_CONTROLLER_REG; j++) {
			temp = readl(LVTSMONCTL0_0 + g_lvts_controller_addrs[j]
				+ offset);
			g_lvts_controller_value_e[i][j] = temp;
		}
	}

}

void read_device_reg_before_active(void)
{
	unsigned int i, j, addr, data;

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		for (j = 0; j < NUM_LVTS_DEVICE_REG; j++) {
			addr = g_lvts_device_addrs[j];
			data =  lvts_read_device(0x81020000, addr, i);
			g_lvts_device_value_b[i][j] = data;
		}
	}
}

void read_device_reg_when_error(void)
{
	int i, j, offset, cnt;
	unsigned int addr;

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {

		offset = lvts_tscpu_g_tc[i].tc_offset; //tc offset

		for (j = 0; j < NUM_LVTS_DEVICE_REG; j++) {
			addr = g_lvts_device_addrs[j];
			lvts_write_device(0x81020000, addr, 0x00, i);
			/* wait 2us + 3us buffer*/
			udelay(5);
			/* Check ASIF bus status for transaction finished
			 * Wait until DEVICE_ACCESS_START = 0
			 */
			cnt = 0;
			while ((readl(LVTS_CONFIG_0 + offset) & _BIT_(24))) {
				cnt++;

				if (cnt == 100) {
					lvts_printk("Error: DEVICE_ACCESS_START didn't ready\n");
					break;
				}
				udelay(2);
			}

			g_lvts_device_value_e[i][j] = (readl(LVTSRDATA0_0
				+ offset));
		}
	}
}

void clear_lvts_register_value_array(void)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		for (j = 0; j < NUM_LVTS_CONTROLLER_REG; j++) {
			g_lvts_controller_value_b[i][j] = 0;
			g_lvts_controller_value_e[i][j] = 0;
		}

		for (j = 0; j < NUM_LVTS_DEVICE_REG; j++) {
			g_lvts_device_value_b[i][j] = 0;
			g_lvts_device_value_e[i][j] = 0;
		}
	}
}
void dump_lvts_register_value(void)
{
	int i, j, offset, tc_offset;
	char buffer[512];

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		lvts_printk("[LVTS_ERROR][BEFROE][CONTROLLER_%d][DUMP]\n", i);
		tc_offset = lvts_tscpu_g_tc[i].tc_offset; //tc offset

		offset = sprintf(buffer, "[LVTS_ERROR][BEFORE][TC][DUMP] ");
		for (j = 0; j < NUM_LVTS_CONTROLLER_REG; j++)
			offset += sprintf(buffer + offset, "0x%x:%x ",
					tc_offset + g_lvts_controller_addrs[j],
					g_lvts_controller_value_b[i][j]);

		buffer[offset] = '\0';
		lvts_printk("%s\n", buffer);

		offset = sprintf(buffer, "[LVTS_ERROR][BEFORE][DEVICE][DUMP] ");
		for (j = 0; j < NUM_LVTS_DEVICE_REG; j++)
			offset += sprintf(buffer + offset, "0x%x:%x ",
					g_lvts_device_addrs[j],
					g_lvts_device_value_b[i][j]);

		buffer[offset] = '\0';
		lvts_printk("%s\n", buffer);
	}

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		lvts_printk("[LVTS_ERROR][AFTER][CONTROLLER_%d][DUMP]\n", i);
		tc_offset = lvts_tscpu_g_tc[i].tc_offset; //tc offset

		offset = sprintf(buffer, "[LVTS_ERROR][AFTER][TC][DUMP] ");
		for (j = 0; j < NUM_LVTS_CONTROLLER_REG; j++)
			offset += sprintf(buffer + offset, "0x%x:%x ",
					tc_offset + g_lvts_controller_addrs[j],
					g_lvts_controller_value_e[i][j]);

		buffer[offset] = '\0';
		lvts_printk("%s\n", buffer);

		offset = sprintf(buffer, "[LVTS_ERROR][AFTER][DEVICE][DUMP] ");
		for (j = 0; j < NUM_LVTS_DEVICE_REG; j++)
			offset += sprintf(buffer + offset, "0x%x:%x ",
					g_lvts_device_addrs[j],
					g_lvts_device_value_e[i][j]);

		buffer[offset] = '\0';
		lvts_printk("%s\n", buffer);
	}
}
#endif
#endif

void lvts_device_identification(void)
{
	unsigned int tc_num, data, offset;

	lvts_dbg_printk("%s\n", __func__);

	for (tc_num = 0; tc_num < ARRAY_SIZE(lvts_tscpu_g_tc); tc_num++) {

		offset = lvts_tscpu_g_tc[tc_num].tc_offset;

		/*  Enable LVTS_CTRL Clock */
		mt_reg_sync_writel_print(0x00000001, LVTSCLKEN_0 + offset);

		/*  Reset All Devices */
		lvts_write_device(0x81030000, 0xFF, 0xFF, tc_num);
		/* udelay(100); */

		/*  Read back Dev_ID with Update */
		lvts_write_device(0x85020000, 0xFF, 0x55, tc_num);

		/*  Check LVTS device ID */
		data = (readl(LVTS_ID_0 + offset) & _BITMASK_(7:0));
		if (data != (0x81 + tc_num))
			lvts_printk("LVTS_TC_%d, Device ID should be 0x%x, but 0x%x\n",
				tc_num, (0x81 + tc_num), data);
	}
}

void lvts_reset_device_and_stop_clk(void)
{
	__u32 offset;
	unsigned int tc_num;

	lvts_dbg_printk("%s\n", __func__);

	for (tc_num = 0; tc_num < ARRAY_SIZE(lvts_tscpu_g_tc); tc_num++) {

		offset = lvts_tscpu_g_tc[tc_num].tc_offset;

		/*  Reset All Devices */
		lvts_write_device(0x81030000, 0xFF, 0xFF, tc_num);

		/*  Disable LVTS_CTRL Clock */
		mt_reg_sync_writel_print(0x00000000, LVTSCLKEN_0 + offset);
	}
}

void lvts_Device_Enable_Init_all_Devices(void)
{
	int i;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		/* Set LVTS device counting window 20us */
		lvts_write_device(0x81030000, 0x04, 0x20, i);
		lvts_write_device(0x81030000, 0x05, 0x00, i);
		/* Release Counting StateMachine */
		lvts_write_device(0x81030000, 0x03, 0x00, i);
		/* TSV2F_CHOP_CKSEL & TSV2F_EN */
		lvts_write_device(0x81030000, 0x08, 0xC3, i);
		/* TSBG_DEM_CKSEL * TSBG_CHOP_EN */
		lvts_write_device(0x81030000, 0x09, 0x8D, i);
		/* Set TS_RSV */
		lvts_write_device(0x81030000, 0x0C, 0x7C, i);
		/* TSBG_RSV[3:0] = 4'ha */
		lvts_write_device(0x81030000, 0x0A, 0xA8, i);
		/* TSV2F_RSV = 4’h4 (Str_EN) */
		lvts_write_device(0x81030000, 0x0B, 0x04, i);

#if LVTS_DEVICE_AUTO_RCK == 0
		/* Device low power mode can ignore these settings and
		 * Device auto RCK mode will force device in low power
		 * mode
		 */

		/* Enable TS_EN */
		lvts_write_device(0x81030000, 0x08, 0xC7, i);
		/* Toggle TSDIV_EN & TSVCO_TG */
		lvts_write_device(0x81030000, 0x08, 0xCE, i);
		/* Toggle TSDIV_EN & TSVCO_TG */
		lvts_write_device(0x81030000, 0x08, 0xC7, i);
#endif
	}
}

void lvts_thermal_cal_prepare(void)
{
	unsigned int temp[16], offset;
	int i, ret;
	char buffer[512];

	temp[0] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_1); /* 0x01B0 */
	temp[1] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_2); /* 0x01C8 */
	temp[2] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_3); /* 0x095C */
	temp[3] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_4); /* 0x01CC */
	temp[4] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_5); /* 0x0960 */
	temp[5] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_6); /* 0x093C */
	temp[6] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_7); /* 0x0964 */
	temp[7] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_8); /* 0x0940 */
	temp[8] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_9); /* 0x0968 */
	temp[9] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_10); /* 0x0944 */
	temp[10] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_11); /* 0x096C */
	temp[11] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_12); /* 0x0948 */
	temp[12] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_13); /* 0x094C */
	temp[13] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_14); /* 0x0950 */
	temp[14] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_15); /* 0x0954 */
	temp[15] = get_devinfo_with_index(LVTS_ADDRESS_INDEX_16); /* 0x0958 */

	for (i = 0; (i + 5) < 16; i = i + 5)
		lvts_printk("[lvts_cal] %d: 0x%x, %d: 0x%x, %d: 0x%x, %d: 0x%x, %d: 0x%x\n",
		i, temp[i], i + 1, temp[i + 1], i + 2, temp[i + 2],
		i + 3, temp[i + 3], i + 4, temp[i + 4]);

	lvts_printk("[lvts_cal] 15: 0x%x\n", temp[15]);

	g_golden_temp = (temp[0] & _BITMASK_(7:0));
	g_count_r[0] = (temp[1] & _BITMASK_(23:0));
	g_count_rc[0] = (temp[2] & _BITMASK_(23:0));
	g_count_r[1] = (temp[3] & _BITMASK_(23:0));
	g_count_rc[1] = (temp[4] & _BITMASK_(23:0));
	g_count_r[2] = (temp[5] & _BITMASK_(23:0));
	g_count_rc[2] = (temp[6] & _BITMASK_(23:0));
	g_count_r[3] = (temp[7] & _BITMASK_(23:0));
	g_count_rc[3] = (temp[8] & _BITMASK_(23:0));
	g_count_r[4] = (temp[9] & _BITMASK_(23:0));
	g_count_rc[4] = (temp[10] & _BITMASK_(23:0));
	g_count_r[5] = (temp[11] & _BITMASK_(23:0));
	g_count_rc[5] = ((temp[1] & _BITMASK_(31:24)) >> 8) +
		((temp[3] & _BITMASK_(31:24)) >> 16)+
		((temp[5] & _BITMASK_(31:24)) >> 24);
	g_count_r[6] = (temp[12] & _BITMASK_(23:0));
	g_count_rc[6] = ((temp[7] & _BITMASK_(31:24)) >> 8) +
		((temp[9] & _BITMASK_(31:24)) >> 16) +
		((temp[11] & _BITMASK_(31:24)) >> 24);
	g_count_r[7] = (temp[13] & _BITMASK_(23:0));
	g_count_rc[7] = ((temp[12] & _BITMASK_(31:24)) >> 8) +
		((temp[13] & _BITMASK_(31:24)) >> 16) +
		((temp[14] & _BITMASK_(31:24)) >> 24);
	/* There is no LVTS4_1 in mt6785 compared with mt6779 */
	/*
	 * g_count_r[8] = (temp[14] & _BITMASK_(23:0));
	 * g_count_rc[8] = ((temp[15] & _BITMASK_(31:24)) >> 8) +
	 *	((temp[2] & _BITMASK_(31:24)) >> 16) +
	 *	((temp[4] & _BITMASK_(31:24)) >> 24);
	 */
	g_count_r[8] = (temp[15] & _BITMASK_(23:0));
	g_count_rc[8] = ((temp[6] & _BITMASK_(31:24)) >> 8) +
		((temp[8] & _BITMASK_(31:24)) >> 16) +
		((temp[10] & _BITMASK_(31:24)) >> 24);

	for (i = 0; i < 16; i++) {
		if (i == 0) {
			if ((temp[0] & _BITMASK_(7:0)) != 0)
				break;
		} else {
			if (temp[i] != 0)
				break;
		}
	}

	if (i == 16) {
		/* It means all efuse data are equal to 0 */
		lvts_printk(
			"[lvts_cal] This sample is not calibrated, fake !!\n");

		g_golden_temp = DEFAULT_EFUSE_GOLDEN_TEMP;
		for (i = 0; i < L_TS_LVTS_NUM; i++) {
			g_count_r[i] = DEFAULT_EFUSE_COUNT_R;
			g_count_rc[i] = DEFAULT_EFUSE_COUNT_RC;
		}

		g_use_fake_efuse = 1;
	}

	lvts_printk("[lvts_cal] g_golden_temp = %d\n", g_golden_temp);

	ret = sprintf(buffer, "[lvts_cal] num:g_count_r:g_count_rc ");
	if (ret < 0)
		return;

	offset = ret;

	for (i = 0; i < L_TS_LVTS_NUM; i++) {
		ret = sprintf(buffer + offset, "%d:%d:%d ",
				i, g_count_r[i], g_count_rc[i]);
		if (ret < 0)
			return;

		offset += ret;
	}

	buffer[offset] = '\0';
	lvts_printk("%s\n", buffer);
}

#if THERMAL_ENABLE_TINYSYS_SSPM || THERMAL_ENABLE_ONLY_TZ_SSPM
void lvts_ipi_send_efuse_data(void)
{
	struct thermal_ipi_data thermal_data;

	lvts_printk("%s\n", __func__);

	thermal_data.u.data.arg[0] = g_golden_temp;
	thermal_data.u.data.arg[1] = 0;
	thermal_data.u.data.arg[2] = 0;
	while (thermal_to_sspm(THERMAL_IPI_LVTS_INIT_GRP1, &thermal_data) != 0)
		udelay(100);
}
#endif

static unsigned int lvts_temp_to_raw(int temp, enum lvts_sensor_enum ts_name)
{
	/* MSR_RAW = ((temp[i] - GOLDEN_TEMP/2 - b) * 16384) / a
	 * a = -204.65
	 * b =  204.65
	 */
	unsigned int msr_raw = 0;

	msr_raw = ((long long int)((g_golden_temp * 500 + LVTS_COEFF_B_X_1000
			- temp)) << 14)/(-1 * LVTS_COEFF_A_X_1000);

	lvts_printk("%s msr_raw = 0x%x,temp=%d\n", __func__, msr_raw, temp);

	return msr_raw;
}

static void lvts_interrupt_handler(unsigned int tc_num)
{
	unsigned int  ret = 0;
	int offset;

	offset = lvts_tscpu_g_tc[tc_num].tc_offset;

	ret = readl(offset + LVTSMONINTSTS_0);
	/* Write back to clear interrupt status */
	mt_reg_sync_writel(ret, offset + LVTSMONINTSTS_0);

	lvts_printk("[Thermal IRQ] LVTS thermal controller %d, LVTSMONINTSTS=0x%08x\n",
		tc_num, ret);

	if (ret & THERMAL_COLD_INTERRUPT_0)
		lvts_dbg_printk("[Thermal IRQ]: Cold interrupt triggered, sensor point 0\n");

	if (ret & THERMAL_HOT_INTERRUPT_0)
		lvts_dbg_printk("[Thermal IRQ]: Hot interrupt triggered, sensor point 0\n");

	if (ret & THERMAL_LOW_OFFSET_INTERRUPT_0)
		lvts_dbg_printk("[Thermal IRQ]: Low offset interrupt triggered, sensor point 0\n");

	if (ret & THERMAL_HIGH_OFFSET_INTERRUPT_0)
		lvts_dbg_printk("[Thermal IRQ]: High offset interrupt triggered, sensor point 0\n");

	if (ret & THERMAL_HOT2NORMAL_INTERRUPT_0)
		lvts_dbg_printk("[Thermal IRQ]: Hot to normal interrupt triggered, sensor point 0\n");

	if (ret & THERMAL_COLD_INTERRUPT_1)
		lvts_dbg_printk("[Thermal IRQ]: Cold interrupt triggered, sensor point 1\n");

	if (ret & THERMAL_HOT_INTERRUPT_1)
		lvts_dbg_printk("[Thermal IRQ]: Hot interrupt triggered, sensor point 1\n");

	if (ret & THERMAL_LOW_OFFSET_INTERRUPT_1)
		lvts_dbg_printk("[Thermal IRQ]: Low offset interrupt triggered, sensor point 1\n");

	if (ret & THERMAL_HIGH_OFFSET_INTERRUPT_1)
		lvts_dbg_printk("[Thermal IRQ]: High offset interrupt triggered, sensor point 1\n");

	if (ret & THERMAL_HOT2NORMAL_INTERRUPT_1)
		lvts_dbg_printk("[Thermal IRQ]: Hot to normal interrupt triggered, sensor point 1\n");

	if (ret & THERMAL_COLD_INTERRUPT_2)
		lvts_dbg_printk("[Thermal IRQ]: Cold interrupt triggered, sensor point 2\n");

	if (ret & THERMAL_HOT_INTERRUPT_2)
		lvts_dbg_printk("[Thermal IRQ]: Hot interrupt triggered, sensor point 2\n");

	if (ret & THERMAL_LOW_OFFSET_INTERRUPT_2)
		lvts_dbg_printk("[Thermal IRQ]: Low offset interrupt triggered, sensor point 2\n");

	if (ret & THERMAL_HIGH_OFFSET_INTERRUPT_2)
		lvts_dbg_printk("[Thermal IRQ]: High offset interrupt triggered, sensor point 2\n");

	if (ret & THERMAL_HOT2NORMAL_INTERRUPT_2)
		lvts_dbg_printk("[Thermal IRQ]: Hot to normal interrupt triggered, sensor point 2\n");

	if (ret & THERMAL_DEVICE_TIMEOUT_INTERRUPT)
		lvts_dbg_printk("[Thermal IRQ]: Device access timeout triggered\n");

	if (ret & THERMAL_IMMEDIATE_INTERRUPT_0)
		lvts_dbg_printk("[Thermal IRQ]: Immediate sense interrupt triggered, sensor point 0\n");

	if (ret & THERMAL_IMMEDIATE_INTERRUPT_1)
		lvts_dbg_printk("[Thermal IRQ]: Immediate sense interrupt triggered, sensor point 1\n");

	if (ret & THERMAL_IMMEDIATE_INTERRUPT_2)
		lvts_dbg_printk("[Thermal IRQ]: Immediate sense interrupt triggered, sensor point 2\n");

	if (ret & THERMAL_FILTER_INTERRUPT_0)
		lvts_dbg_printk("[Thermal IRQ]: Filter sense interrupt triggered, sensor point 0\n");

	if (ret & THERMAL_FILTER_INTERRUPT_1)
		lvts_dbg_printk("[Thermal IRQ]: Filter sense interrupt triggered, sensor point 1\n");

	if (ret & THERMAL_FILTER_INTERRUPT_2)
		lvts_dbg_printk("[Thermal IRQ]: Filter sense interrupt triggered, sensor point 2\n");

	if (ret & THERMAL_COLD_INTERRUPT_3)
		lvts_dbg_printk("[Thermal IRQ]: Cold interrupt triggered, sensor point 3\n");

	if (ret & THERMAL_HOT_INTERRUPT_3)
		lvts_dbg_printk("[Thermal IRQ]: Hot interrupt triggered, sensor point 3\n");

	if (ret & THERMAL_LOW_OFFSET_INTERRUPT_3)
		lvts_dbg_printk("[Thermal IRQ]: Low offset interrupt triggered, sensor point 3\n");

	if (ret & THERMAL_HIGH_OFFSET_INTERRUPT_3)
		lvts_dbg_printk("[Thermal IRQ]: High offset triggered, sensor point 3\n");

	if (ret & THERMAL_HOT2NORMAL_INTERRUPT_3)
		lvts_dbg_printk("[Thermal IRQ]: Hot to normal interrupt triggered, sensor point 3\n");

	if (ret & THERMAL_IMMEDIATE_INTERRUPT_3)
		lvts_dbg_printk("[Thermal IRQ]: Immediate sense interrupt triggered, sensor point 3\n");

	if (ret & THERMAL_FILTER_INTERRUPT_3)
		lvts_dbg_printk("[Thermal IRQ]: Filter sense interrupt triggered, sensor point 3\n");

	if (ret & THERMAL_PROTECTION_STAGE_1)
		lvts_dbg_printk("[Thermal IRQ]: Thermal protection stage 1 interrupt triggered\n");

	if (ret & THERMAL_PROTECTION_STAGE_2) {
		lvts_dbg_printk("[Thermal IRQ]: Thermal protection stage 2 interrupt triggered\n");
#if MTK_TS_CPU_RT
		wake_up_process(ktp_thread_handle);
#endif
	}

	if (ret & THERMAL_PROTECTION_STAGE_3)
		lvts_printk("[Thermal IRQ]: Thermal protection stage 3 interrupt triggered, Thermal HW reboot\n");
}

irqreturn_t lvts_tscpu_thermal_all_tc_interrupt_handler(int irq, void *dev_id)
{
	unsigned int ret = 0, i, mask = 1;

	ret = readl(THERMINTST);
	ret = ret & 0x7F;
	/* MSB LSB NAME
	 * 6   6   LVTSINT3
	 * 5   5   LVTSINT2
	 * 4   4   LVTSINT1
	 * 3   3   LVTSINT0
	 * 2   2   THERMINT2
	 * 1   1   THERMINT1
	 * 0   0   THERMINT0
	 */

	lvts_printk("%s : THERMINTST = 0x%x\n", __func__, ret);
	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		mask = 1 << (i+3); /* shift 3 to skip THERMINT0,1,2 */

		if ((ret & mask) == 0)
			lvts_interrupt_handler(i);
	}

	return IRQ_HANDLED;
}

static void lvts_configure_polling_speed_and_filter(unsigned int tc_num)
{
	__u32 offset, lvtsMonCtl1, lvtsMonCtl2;

	lvts_dbg_printk("%s\n", __func__);

	offset = lvts_tscpu_g_tc[tc_num].tc_offset;

	lvtsMonCtl1 = (((lvts_tscpu_g_tc[tc_num].tc_speed.group_interval_delay
			<< 20) & _BITMASK_(29:20)) |
			(lvts_tscpu_g_tc[tc_num].tc_speed.period_unit &
			_BITMASK_(9:0)));
	lvtsMonCtl2 = (((lvts_tscpu_g_tc[tc_num].tc_speed.filter_interval_delay
			<< 16) & _BITMASK_(25:16)) |
			(lvts_tscpu_g_tc[tc_num].tc_speed.sensor_interval_delay
			& _BITMASK_(9:0)));
	/*
	 * Calculating period unit in Module clock x 256, and the Module clock
	 * will be changed to 26M when Infrasys enters Sleep mode.
	 */

	/*
	 * bus clock 66M counting unit is
	 *			12 * 1/66M * 256 = 12 * 3.879us = 46.545 us
	 */
	mt_reg_sync_writel_print(lvtsMonCtl1, offset + LVTSMONCTL1_0);
	/*
	 *filt interval is 1 * 46.545us = 46.545us,
	 *sen interval is 429 * 46.545us = 19.968ms
	 */
	mt_reg_sync_writel_print(lvtsMonCtl2, offset + LVTSMONCTL2_0);

	/* temperature sampling control, 2 out of 4 samples */
	mt_reg_sync_writel_print(0x00000492, offset + LVTSMSRCTL0_0);

	udelay(1);
	lvts_printk(
		"%s %d, LVTSMONCTL1_0= 0x%x,LVTSMONCTL2_0= 0x%x,LVTSMSRCTL0_0= 0x%x\n",
		__func__, tc_num,
		readl(LVTSMONCTL1_0 + offset),
		readl(LVTSMONCTL2_0 + offset),
		readl(LVTSMSRCTL0_0 + offset));
}

/*
 * temperature2 to set the middle threshold for interrupting CPU.
 * -275000 to disable it.
 */
static void lvts_set_tc_trigger_hw_protect(
int temperature, int temperature2, unsigned int tc_num)
{
	int temp = 0, raw_high, config, offset;
#if LVTS_USE_DOMINATOR_SENSING_POINT
	int d_index;
	enum lvts_sensor_enum ts_name;
#endif

	offset = lvts_tscpu_g_tc[tc_num].tc_offset;

	lvts_dbg_printk("%s t1=%d t2=%d\n",
				__func__, temperature, temperature2);

#if LVTS_USE_DOMINATOR_SENSING_POINT
	if (lvts_tscpu_g_tc[tc_num].dominator_ts_idx <
		lvts_tscpu_g_tc[tc_num].ts_number){
		d_index = lvts_tscpu_g_tc[tc_num].dominator_ts_idx;
	} else {
		lvts_printk("Error: LVTS TC%d, dominator_ts_idx = %d should smaller than ts_number = %d\n",
			tc_num,
			lvts_tscpu_g_tc[tc_num].dominator_ts_idx,
			lvts_tscpu_g_tc[tc_num].ts_number);

		lvts_printk("Use the sensor point 0 as the dominated sensor\n");
		d_index = 0;
	}

	ts_name = lvts_tscpu_g_tc[tc_num].ts[d_index];

	lvts_printk("%s # in tc%d , the dominator ts_name is %d\n",
						__func__, tc_num, ts_name);

	/* temperature to trigger SPM state2 */
	raw_high = lvts_temp_to_raw(temperature, ts_name);
#else
	raw_high = lvts_temp_to_raw(temperature, 0);
#endif

	temp = readl(offset + LVTSMONINT_0);
	/* disable trigger SPM interrupt */
	mt_reg_sync_writel_print(temp & 0x00000000, offset + LVTSMONINT_0);

#if LVTS_USE_DOMINATOR_SENSING_POINT
	/* Select protection sensor */
	config = ((d_index << 2) + 0x2) << 16;
	mt_reg_sync_writel_print(config, offset + LVTSPROTCTL_0);
#else
	/* Maximum of 4 sensing points */
	config = (0x1 << 16);
	mt_reg_sync_writel_print(config, offset + LVTSPROTCTL_0);
#endif
	/* set hot to HOT wakeup event */
	mt_reg_sync_writel_print(raw_high, offset + LVTSPROTTC_0);

	/* enable trigger Hot SPM interrupt */
	mt_reg_sync_writel_print(temp | 0x80000000, offset + LVTSMONINT_0);
}

static void dump_lvts_device(unsigned int tc_num, __u32 offset)
{
	lvts_printk("%s, LVTS_CONFIG_%d= 0x%x\n", __func__,
				tc_num, readl(LVTS_CONFIG_0 + offset));
	udelay(2);

	//read raw data
	lvts_printk("%s, LVTSRDATA0_%d= 0x%x\n", __func__,
				tc_num, readl(LVTSRDATA0_0 + offset));

	udelay(2);
	lvts_printk("%s, LVTSRDATA1_%d= 0x%x\n", __func__,
				tc_num, readl(LVTSRDATA1_0 + offset));

	udelay(2);
	lvts_printk("%s, LVTSRDATA2_%d= 0x%x\n", __func__,
				tc_num, readl(LVTSRDATA2_0 + offset));

	udelay(2);
	lvts_printk("%s, LVTSRDATA3_%d= 0x%x\n", __func__,
				tc_num, readl(LVTSRDATA3_0 + offset));
}

#if LVTS_VALID_DATA_TIME_PROFILING
void lvts_dump_time_profiling_result(struct seq_file *m)
{
	int i, sum;

	seq_printf(m, "SODI3_count= %llu\n", SODI3_count);
	seq_printf(m, "noValid_count %llu, %d%%\n",
		noValid_count, ((noValid_count * 100) / SODI3_count));
	seq_printf(m, "valid_count %llu, %d%%\n",
		(SODI3_count - noValid_count),
		(((SODI3_count - noValid_count) * 100) / SODI3_count));

	sum = 0;
	for (i = 0; i < NUM_TIME_TH; i++)
		sum += latency_array[i][1];

	seq_printf(m, "Valid count in latency_array = %d\n", sum);

	for (i = 0; i < NUM_TIME_TH; i++) {
		if (i == 0) {
			seq_printf(m, "Count valid latency between 0ms ~ %dms: %d, %d%%\n",
				latency_array[i][0], latency_array[i][1],
				((latency_array[i][1] * 100) / sum));
		} else if (i == (NUM_TIME_TH - 1)) {
			seq_printf(m, "Count valid others: %d, %d%%\n",
				latency_array[i][1],
				((latency_array[i][1] * 100) / sum));
		} else {
			seq_printf(m, "Count valid latency between %dms ~ %dms: %d, %d%%\n",
				latency_array[i - 1][0],
				latency_array[i][0], latency_array[i][1],
				((latency_array[i][1] * 100) / sum));
		}
	}

	/* count if start_timestamp is bigger than end_timestamp */
	seq_printf(m, "diff_error_count= %d\n", diff_error_count);
	seq_printf(m, "Current start_timestamp= %lldus\n", start_timestamp);
	seq_printf(m, "Current end_timestamp= %lldus\n", end_timestamp);
	seq_printf(m, "Current time_diff= %lldus\n", time_diff);
}

static void lvts_count_valid_temp_latency(long long int time_diff)
{
	/* time_diff is in microseconds */
	int i;

	for (i = 0; i < (NUM_TIME_TH - 1); i++) {
		if (time_diff < (((long long int)latency_array[i][0])
			* 1000)) {
			latency_array[i][1]++;
			break;
		}
	}

	if (i == (NUM_TIME_TH - 1))
		latency_array[i][1]++;
}
#endif

static int lvts_read_tc_raw_and_temp(
		u32 *tempmsr_name, enum lvts_sensor_enum ts_name)
{
	int temp = 0, raw = 0, raw1 = 0, raw2 = 0;

	if (tempmsr_name == 0)
		return 0;

	raw = readl((tempmsr_name));
	raw1 = (raw & 0x10000) >> 16; //bit 16 : valid bit
	raw2 = raw & 0xFFFF;
	temp = lvts_raw_to_temp(raw2, ts_name);

	if (lvts_rawdata_debug_log) {
		lvts_printk(
			"[LVTS_MSR] ts%d msr_all=%x, valid=%d, msr_temp=%d, temp=%d\n",
			ts_name, raw, raw1, raw2, temp);
	}

	tscpu_ts_lvts_temp_r[ts_name] = raw2;
#if CONFIG_LVTS_ERROR_AEE_WARNING
	tscpu_ts_lvts_temp_v[ts_name] = raw1;
#endif
#if LVTS_VALID_DATA_TIME_PROFILING
	if (isTempValid == 0 && raw1 != 0 && SODI3_count != 0) {
		isTempValid = 1;
		end_timestamp = thermal_get_current_time_us();
		time_diff = end_timestamp - start_timestamp;
		if (time_diff < 0) {
			lvts_printk("[LVTS_ERROR] time_diff = %lldus,start_time= %lldus, end_time= %lldus\n",
				time_diff, start_timestamp, end_timestamp);
			diff_error_count++;
		} else {
			lvts_count_valid_temp_latency(time_diff);
		}
	}
#endif

	return temp;
}

static void lvts_tscpu_thermal_read_tc_temp(
		unsigned int tc_num, enum lvts_sensor_enum type, int order)
{
	__u32 offset;

	offset = lvts_tscpu_g_tc[tc_num].tc_offset;

	if (lvts_rawdata_debug_log)
		dump_lvts_device(tc_num, offset);

	switch (order) {
	case 0:
		tscpu_ts_lvts_temp[type] =
			lvts_read_tc_raw_and_temp((offset + LVTSMSR0_0), type);
		lvts_dbg_printk("%s order %d tc_num %d type %d temp %d\n",
				__func__, order, tc_num, type,
				tscpu_ts_lvts_temp[type]);
		break;
	case 1:
		tscpu_ts_lvts_temp[type] =
			lvts_read_tc_raw_and_temp((offset + LVTSMSR1_0), type);
		lvts_dbg_printk("%s order %d tc_num %d type %d temp %d\n",
				__func__, order, tc_num, type,
				tscpu_ts_lvts_temp[type]);
		break;
	case 2:
		tscpu_ts_lvts_temp[type] =
			lvts_read_tc_raw_and_temp((offset + LVTSMSR2_0), type);
		lvts_dbg_printk("%s order %d tc_num %d type %d temp %d\n",
				__func__, order, tc_num, type,
				tscpu_ts_lvts_temp[type]);
		break;
	case 3:
		tscpu_ts_lvts_temp[type] =
			lvts_read_tc_raw_and_temp((offset + LVTSMSR3_0), type);
		lvts_dbg_printk("%s order %d tc_num %d type %d temp %d\n",
				__func__, order, tc_num, type,
				tscpu_ts_lvts_temp[type]);
		break;
	default:
		tscpu_ts_lvts_temp[type] =
			lvts_read_tc_raw_and_temp((offset + LVTSMSR0_0), type);
		lvts_dbg_printk("%s order %d tc_num %d type %d temp %d\n",
				__func__, order, tc_num, type,
				tscpu_ts_lvts_temp[type]);
		break;
	}
}

void read_all_tc_lvts_temperature(void)
{
	int i = 0, j = 0;

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++)
		for (j = 0; j < lvts_tscpu_g_tc[i].ts_number; j++)
			lvts_tscpu_thermal_read_tc_temp(i,
					lvts_tscpu_g_tc[i].ts[j], j);
}

/* pause ALL periodoc temperature sensing point */
void lvts_pause_all_sensing_points(void)
{
	int i, temp, offset;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset;
		temp = readl(offset + LVTSMSRCTL1_0);
		/* set bit8=bit1=bit2=bit3=1 to pause sensing point 0,1,2,3 */
		mt_reg_sync_writel_print((temp | 0x10E),
					offset + LVTSMSRCTL1_0);
	}
}

/*
 * lvts_thermal_check_all_sensing_point_idle -
 * Check if all sensing points are idle
 * Return: 0 if all sensing points are idle
 *         an error code if one of them is busy
 * error code[31:16]: an index of LVTS thermal controller
 * error code[2]: bit 10 of LVTSMSRCTL1
 * error code[1]: bit 7 of LVTSMSRCTL1
 * error code[0]: bit 0 of LVTSMSRCTL1
 */
static int lvts_thermal_check_all_sensing_point_idle(void)
{
	int i, temp, offset, error_code;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset;
		temp = readl(offset + LVTSMSRCTL1_0);
		/* Check if bit10=bit7=bit0=0 */
		if ((temp & 0x481) != 0) {
			error_code = (i << 16) + ((temp & _BIT_(10)) >> 8) +
				((temp & _BIT_(7)) >> 6) +
				(temp & _BIT_(0));

			return error_code;
		}
	}

	return 0;
}

void lvts_wait_for_all_sensing_point_idle(void)
{
	int cnt, temp;

	cnt = 0;
	/* Wait until all sensoring points idled */
	while (cnt < 50) {
		temp = lvts_thermal_check_all_sensing_point_idle();
		if (temp == 0)
			break;

		if ((cnt + 1) % 10 == 0) {
			pr_notice("Cnt = %d LVTS TC %d, LVTSMSRCTL1[10,7,0] = %d,%d,%d\n",
					cnt + 1, (temp >> 16),
					((temp & _BIT_(2)) >> 2),
					((temp & _BIT_(1)) >> 1),
					(temp & _BIT_(0)));
		}

		udelay(2);
		cnt++;
	}
}
/* release ALL periodoc temperature sensing point */
void lvts_release_all_sensing_points(void)
{
	int i = 0, temp;
	__u32 offset;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset;

		temp = readl(offset + LVTSMSRCTL1_0);
		/* set bit1=bit2=bit3=bit8=0 to release sensing point 0,1,2,3*/
		mt_reg_sync_writel_print(((temp & (~0x10E))),
					offset + LVTSMSRCTL1_0);
	}
}

void lvts_sodi3_release_thermal_controller(void)
{
	/* SPM will close 26M to saving power during SODI3
	 * Because both auxadc thermal controllers and lvts thermal controllers
	 * need 26M to work properly, it would cause thermal controllers to
	 * report an abnormal high or low temperature after leaving SODI3
	 *
	 * The SW workaround solution is that
	 * SPM will pause LVTS thermal controllers before closing 26M, and
	 * try to release LVTS thermal controllers after leaving SODI3
	 * thermal driver check and release LVTS thermal controllers if
	 * necessary after leaving SODI3
	 */
	int i = 0, temp, lvts_paused = 0;
	__u32 offset;

	lvts_dbg_printk("%s\n", __func__);

	/* Check if SPM paused thermal controller */
	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset;
		temp = readl(offset + LVTSMSRCTL1_0);
		/* set bit8=bit1=bit2=bit3=1 to pause sensing point 0,1,2,3 */
		if ((temp & 0x10E) != 0) {
			lvts_paused = 1;
			break;
		}
	}

	/* Return if SPM didn't pause thermal controller or
	 * released thermal controllers already
	 */
	if (lvts_paused == 0)
		return;
	/* Wait until all of LVTS thermal controllers are idle
	 * Pause operation has to take time to finish.
	 * if it didn't finish before SPM closed 26M, we have to wait until
	 * it is finished to make sure all LVTS thermal controllers in a
	 * correct finite state machine
	 */

	lvts_wait_for_all_sensing_point_idle();

	lvts_release_all_sensing_points();
}

/*
 * disable ALL periodoc temperature sensing point
 */
void lvts_disable_all_sensing_points(void)
{
	int i = 0, offset;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset;
		mt_reg_sync_writel_print(0x00000000, offset + LVTSMONCTL0_0);
	}
}

void lvts_enable_all_sensing_points(void)
{
	int i, offset;

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {

		offset = lvts_tscpu_g_tc[i].tc_offset;

		lvts_dbg_printk("%s %d:%d\n", __func__, i,
			lvts_tscpu_g_tc[i].ts_number);

		switch (lvts_tscpu_g_tc[i].ts_number) {
		case 1:
			/* enable sensing point 0 */
			mt_reg_sync_writel_print(0x00000201,
					offset + LVTSMONCTL0_0);
			break;
		case 2:
			/* enable sensing point 0,1 */
			mt_reg_sync_writel_print(0x00000203,
					offset + LVTSMONCTL0_0);
			break;
		case 3:
			/* enable sensing point 0,1,2 */
			mt_reg_sync_writel_print(0x00000207,
					offset + LVTSMONCTL0_0);
			break;
		case 4:
			/* enable sensing point 0,1,2,3*/
			mt_reg_sync_writel_print(0x0000020F,
					offset + LVTSMONCTL0_0);
			break;
		default:
			lvts_printk("Error at %s\n", __func__);
			break;
		}
	}
}

void lvts_tscpu_thermal_initial_all_tc(void)
{
	unsigned int i = 0;
	int offset;

	lvts_dbg_printk("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		offset = lvts_tscpu_g_tc[i].tc_offset;

		/*  set sensor index of LVTS */
		mt_reg_sync_writel_print(0x13121110, LVTSTSSEL_0 + offset);
		/*  set calculation scale rules */
		mt_reg_sync_writel_print(0x00000300, LVTSCALSCALE_0 + offset);
		/* Set Device Single mode */
		lvts_write_device(0x81030000, 0x06, 0xB8, i);

		lvts_configure_polling_speed_and_filter(i);
	}

#if CONFIG_LVTS_ERROR_AEE_WARNING
#if DUMP_LVTS_REGISTER
	read_controller_reg_before_active();
#endif
#endif
}

void lvts_config_all_tc_hw_protect(int temperature, int temperature2)
{
	unsigned int i = 0;
	int wd_api_ret;
	struct wd_api *wd_api;

	lvts_printk("%s, temperature=%d,temperature2=%d,\n",
					__func__, temperature, temperature2);

	/*spend 860~1463 us */
	/*Thermal need to config to direct reset mode
	 *this API provide by Weiqi Fu(RGU SW owner).
	 */

	wd_api_ret = get_wd_api(&wd_api);
	if (wd_api_ret >= 0) {
		/* reset mode */
		wd_api->wd_thermal_direct_mode_config(
				WD_REQ_DIS, WD_REQ_RST_MODE);

	} else {
		lvts_warn("%d FAILED TO GET WD API\n", __LINE__);
		WARN_ON_ONCE(1);
	}


	for (i = 0; i < ARRAY_SIZE(lvts_tscpu_g_tc); i++) {
		if (lvts_tscpu_g_tc[i].ts_number == 0)
			continue;
		/* Move thermal HW protection ahead... */
		lvts_set_tc_trigger_hw_protect(temperature, temperature2, i);
	}

	/* Thermal need to config to direct reset mode
	 * this API provide by Weiqi Fu(RGU SW owner).
	 */
	if (wd_api_ret >= 0) {
		/* reset mode */
		wd_api->wd_thermal_direct_mode_config(
				WD_REQ_EN, WD_REQ_RST_MODE);
	} else {
		lvts_warn("%d FAILED TO GET WD API\n", __LINE__);
		WARN_ON_ONCE(1);
	}
}

void lvts_tscpu_reset_thermal(void)
{
	/* chip dependent, Have to confirm with DE */

	int temp = 0;

	lvts_dbg_printk("%s\n", __func__);

	/* reset thremal ctrl */
	/* TODO: Is it necessary to read INFRA_GLOBALCON_RST_0_SET? */
	temp = readl(INFRA_GLOBALCON_RST_0_SET);

	/* 1: Enables thermal control software reset */
	temp |= 0x00000001;
	mt_reg_sync_writel_print(temp, INFRA_GLOBALCON_RST_0_SET);

	/* TODO: How long to set the reset bit? */

	/* un reset */
	/* TODO: Is it necessary to read INFRA_GLOBALCON_RST_0_CLR? */
	temp = readl(INFRA_GLOBALCON_RST_0_CLR);

	/* 1: Enable reset Disables thermal control software reset */
	temp |= 0x00000001;

	mt_reg_sync_writel_print(temp, INFRA_GLOBALCON_RST_0_CLR);
}

void get_lvts_slope_intercept(struct TS_PTPOD *ts_info, enum
		thermal_bank_name ts_bank)
{
	struct TS_PTPOD ts_ptpod;
	int temp;

	lvts_dbg_printk("%s\n", __func__);

	/* chip dependent */

	temp = (0 - LVTS_COEFF_A_X_1000) * 2;
	temp /= 1000;
	ts_ptpod.ts_MTS = temp;

	temp = 500 * g_golden_temp + LVTS_COEFF_B_X_1000;
	temp /= 1000;
	ts_ptpod.ts_BTS = (temp - 25) * 4;

	ts_info->ts_MTS = ts_ptpod.ts_MTS;
	ts_info->ts_BTS = ts_ptpod.ts_BTS;
	lvts_dbg_printk("(LVTS) ts_MTS=%d, ts_BTS=%d\n",
			ts_ptpod.ts_MTS, ts_ptpod.ts_BTS);
}
EXPORT_SYMBOL(get_lvts_slope_intercept);

int lvts_tscpu_dump_cali_info(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "lvts_cal : %d\n", g_use_fake_efuse?0:1);
	seq_printf(m, "[lvts_cal] g_golden_temp %d\n", g_golden_temp);

	for (i = 0; i < L_TS_LVTS_NUM; i++)
		seq_printf(m, "[lvts_cal] g_count_r%d = 0x%x\n",
				i, g_count_r[i]);

	for (i = 0; i < L_TS_LVTS_NUM; i++)
		seq_printf(m, "[lvts_cal] g_count_rc%d = 0x%x\n",
				i, g_count_rc[i]);

	return 0;
}

