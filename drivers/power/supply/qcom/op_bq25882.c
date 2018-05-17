/* Copyright (c) 2016-2018 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#include "op_bq25882.h"

void (*enable_aggressive_segmentation_fn)(bool);

struct bq_chg_chip *bq25882_chip;
static DEFINE_MUTEX(bq25882_i2c_access);

static int __bq25882_read_reg(struct bq_chg_chip *chip,
			int reg, int *returnData)
{
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	}
	*returnData = ret;

	return 0;
}

static int bq25882_read_reg(struct bq_chg_chip *chip,
			int reg, int *returnData)
{
	int ret = 0;

	mutex_lock(&bq25882_i2c_access);
	ret = __bq25882_read_reg(chip, reg, returnData);
	mutex_unlock(&bq25882_i2c_access);
	return ret;
}

static int __bq25882_write_reg(struct bq_chg_chip *chip,
					int reg, int val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, ret);
		return ret;
	}

	return 0;
}

static int bq25882_config_interface(struct bq_chg_chip *chip,
				int RegNum, int val, int MASK)
{
	int bq25882_reg = 0;
	int ret = 0;

	mutex_lock(&bq25882_i2c_access);
	ret = __bq25882_read_reg(chip, RegNum, &bq25882_reg);

	pr_debug(" Reg[%x]=0x%x\n", RegNum, bq25882_reg);
	bq25882_reg &= ~MASK;
	bq25882_reg |= val;
	ret = __bq25882_write_reg(chip, RegNum, bq25882_reg);
	pr_debug(" write Reg[%x]=0x%x\n", RegNum, bq25882_reg);
	__bq25882_read_reg(chip, RegNum, &bq25882_reg);
	pr_debug(" Check Reg[%x]=0x%x\n", RegNum, bq25882_reg);
	mutex_unlock(&bq25882_i2c_access);
	return ret;
}


static int bq25882_usbin_input_current_limit[] = {
	500,    600,    700,    800,    900,
	1000,   1100,   1200,   1300,   1400,
	1500,   1600,   1700,   1800,   1900,
	2000,   2100,   2200,   2300,   2400,
	2500,   2600,   2700,   2800,   2900,
	3000,   3100,   3200,   3300
};

static int qpnp_get_prop_charger_voltage_now(void)
{
	static struct power_supply *psy;
	union power_supply_propval ret = {0,};
	int vchager, rc;

	if (!psy) {
		psy = power_supply_get_by_name("usb");
		if (!psy) {
			pr_info("failed to get ps usb\n");
			return -EINVAL;
		}
	}

	rc = power_supply_get_property(psy,
		POWER_SUPPLY_PROP_VOLTAGE_NOW, &ret);
	if (rc)
		return -EINVAL;

	if (ret.intval < 0)
		return -EINVAL;

	vchager = ret.intval;
	return vchager;
}


int bq25882_input_current_limit_write(struct bq_chg_chip *chip, int value)
{
	int rc = 0, i = 0, j = 0;
	int chg_vol = 0;

	pr_info("%s,ICL mA=%d\n", __func__, value);

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	for (i = ARRAY_SIZE(bq25882_usbin_input_current_limit) - 1;
		i >= 0; i--) {
		if (bq25882_usbin_input_current_limit[i] <= value)
			break;
		else if (i == 0)
			break;
	}
		pr_info("ICL =%d setting %02x\n", value, i);
		for (j = 2; j <= i; j++) {
			bq25882_config_interface(chip, REG03_BQ25882_ADDRESS,
			j<<REG03_BQ25882_INPUT_CURRENT_LIMIT_SHIFT,
			REG03_BQ25882_INPUT_CURRENT_LIMIT_MASK);
			msleep(90);
			chg_vol = qpnp_get_prop_charger_voltage_now();
			if (chg_vol < chip->sw_aicl_point) {
				if (j > 2)
					j = j-1;
			pr_info("aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d\n",
			chg_vol, j, bq25882_usbin_input_current_limit[j],
					chip->sw_aicl_point);
			bq25882_config_interface(chip, REG03_BQ25882_ADDRESS,
				j << REG03_BQ25882_INPUT_CURRENT_LIMIT_SHIFT,
					REG03_BQ25882_INPUT_CURRENT_LIMIT_MASK);
				return 0;
			}
	}
	j = i;

	pr_info("usb input max current limit aicl chg_vol=%d j[%d]=%d\n",
		chg_vol, j, bq25882_usbin_input_current_limit[j]);
	rc = bq25882_config_interface(chip, REG03_BQ25882_ADDRESS,
		j << REG03_BQ25882_INPUT_CURRENT_LIMIT_SHIFT,
		REG03_BQ25882_INPUT_CURRENT_LIMIT_MASK);
	return rc;
}

int bq25882_charging_current_write_fast(struct bq_chg_chip *chip, int chg_cur)
{
	int rc = 0;
	int tmp = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	pr_info("%s,FCC mA=%d\n", __func__, chg_cur);
	tmp = chg_cur - REG01_BQ25882_FAST_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG01_BQ25882_FAST_CURRENT_LIMIT_STEP;
	rc = bq25882_config_interface(chip, REG01_BQ25882_ADDRESS,
		tmp << REG01_BQ25882_FAST_CURRENT_LIMIT_SHIFT,
		REG01_BQ25882_FAST_CURRENT_LIMIT_MASK);
	return rc;
}

int bq25882_set_vindpm_vol(struct bq_chg_chip *chip, int vol)
{
	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	return 0;
}


int bq25882_set_enable_volatile_writes(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	/*need do nothing*/
	return rc;
}

int bq25882_set_complete_charge_timeout(struct bq_chg_chip *chip, int val)
{
	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	return 0;
}

int bq25882_float_voltage_write(struct bq_chg_chip *chip, int vfloat_mv)
{
	int rc = 0;
	int tmp = 0;

	pr_info("%s,vfloat_mv=%d\n", __func__, vfloat_mv);
	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	tmp = vfloat_mv - REG00_BQ25882_BAT_THRESHOLD_OFFSET;
	tmp = tmp / REG00_BQ25882_BAT_THRESHOLD_STEP;
	rc = bq25882_config_interface(chip, REG00_BQ25882_ADDRESS,
		tmp << REG00_BQ25882_BAT_THRESHOLD_SHIFT,
		REG00_BQ25882_BAT_THRESHOLD_MASK);
	return rc;
}

int bq25882_set_prechg_current(struct bq_chg_chip *chip, int ipre_mA)
{
	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	return 0;
}

int bq25882_set_topoff_timer(struct bq_chg_chip *chip)
{
	return 0;
}

int bq25882_set_termchg_current(struct bq_chg_chip *chip, int term_curr)
{
	int rc = 0;

	pr_info("%s,term_curr=%d\n", __func__, term_curr);
	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = bq25882_config_interface(chip, REG04_BQ25882_ADDRESS,
	term_curr, REG04_BQ25882_ITERM_LIMIT_MASK);
	return 0;
}

int bq25882_set_rechg_voltage(struct bq_chg_chip *chip, int recharge_mv)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = bq25882_config_interface(chip, REG06_BQ25882_ADDRESS,
		REG06_BQ25882_VRECHG, REG06_BQ25882_VRECHG_MASK);
	if (rc)
		pr_err("Couldn't set recharging threshold rc = %d\n", rc);
	return rc;
}

int bq25882_set_wdt_timer(struct bq_chg_chip *chip, int reg)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = bq25882_config_interface(chip, REG05_BQ25882_ADDRESS,
		REG05_BQ25882_WATCHDOG, REG05_BQ25882_WATCHDOG_MASK);
	if (rc)
	pr_err("Couldn't set recharging threshold rc = %d\n", rc);

	return 0;
}

int bq25882_set_chging_term_disable(struct bq_chg_chip *chip)
{
	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	return 0;
}

int bq25882_kick_wdt(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	return rc;
}

int bq25882_enable_charging(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	pr_err("%s\n", __func__);
	rc = bq25882_config_interface(chip, REG06_BQ25882_ADDRESS,
		REG06_BQ25882_EN_CHG_ENABLE, REG06_BQ25882_EN_CHG_MASK);
	if (rc < 0)
		pr_err("Couldn'tbq25882_enable_charging rc = %d\n", rc);

	return rc;
}

int bq25882_disable_charging(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	pr_err("%s\n", __func__);
	rc = bq25882_config_interface(chip, REG06_BQ25882_ADDRESS,
		REG06_BQ25882_EN_CHG_DISABLE, REG06_BQ25882_EN_CHG_MASK);
	if (rc < 0)
		pr_err("Couldn't bq25882_disable_charging  rc = %d\n", rc);

	return rc;
}

int bq25882_check_charging_enable(struct bq_chg_chip *chip)
{
	int rc = 0, reg_val = 0;
	bool charging_enable = false;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	rc = bq25882_read_reg(chip, REG06_BQ25882_ADDRESS, &reg_val);
	if (rc) {
		pr_err("Couldn't read REG06_BQ25882_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charging_enable = ((reg_val & REG06_BQ25882_EN_CHG_MASK)
				== REG06_BQ25882_EN_CHG_ENABLE) ? 1 : 0;

	return charging_enable;
}

int bq25882_registers_read_full(struct bq_chg_chip *chip)
{
	int rc = 0;
	int reg_full = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	rc = bq25882_read_reg(chip, REG0B_BQ25882_ADDRESS, &reg_full);
	if (rc) {
		pr_err("Couldn't read STAT_C rc = %d\n", rc);
		return 0;
	}

	reg_full = ((reg_full & REG0B_BQ25882_CHGSTAT_MASK)
			== REG0B_BQ25882_CHGSTAT_CHGDONE) ? 1 : 0;
	pr_info("reg_full = %d\n", reg_full);
	return reg_full;
}

int bq25882_suspend_charger(struct bq_chg_chip *chip)
{
	int rc = 0;

	rc = 0;
	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	pr_info("rc = %d\n", rc);
	if (rc < 0)
		pr_err("Couldn't bq25882_suspend_charger rc = %d\n", rc);
	return rc;
}
int bq25882_unsuspend_charger(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	pr_info("rc = %d\n", rc);
	if (rc < 0)
		pr_err("Couldn't bq25882_unsuspend_charger rc = %d\n", rc);
	return rc;
}

bool bq25882_check_charger_resume(struct bq_chg_chip *chip)
{
	if (atomic_read(&chip->charger_suspended) == 1)
		return false;
	else
		return true;
}

int bq25882_reset_charger(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	rc = bq25882_config_interface(chip, REG25_BQ25882_ADDRESS,
		REG25_BQ25882_RESET_DEFAULT, REG25_BQ25882_RESET_MASK);
	if (rc < 0)
		pr_err("Couldn't bq25882_reset_charger  rc = %d\n", rc);

	return rc;
}

int bq25882_otg_enable(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	pr_info("bq_otg_enable\n");
	rc = bq25882_config_interface(chip, REG06_BQ25882_ADDRESS,
		REG06_BQ25882_EN_OTG_ENABLE, REG06_BQ25882_EN_OTG_MASK);
	if (rc < 0)
		pr_err("Couldn't bq25882_otg_enable  rc = %d\n", rc);

	return rc;
}

int bq25882_otg_disable(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	pr_info("bq_otg_disable\n");
	rc = bq25882_config_interface(chip, REG06_BQ25882_ADDRESS,
		REG06_BQ25882_EN_OTG_DISABLE, REG06_BQ25882_EN_OTG_MASK);
	if (rc < 0)
		pr_err("Couldn't bq25882_otg_disable  rc = %d\n", rc);

	return rc;
}

int bq25882_ico_disable(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	rc = bq25882_config_interface(chip, REG03_BQ25882_ADDRESS,
		REG03_BQ25882_EN_ICO_DISABLE, REG03_BQ25882_EN_ICO_MASK);
	if (rc < 0)
		pr_err("Couldn't bq25882_ico_disable  rc = %d\n", rc);

	return rc;
}

int bq25882_adc_en_enable(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	rc = bq25882_config_interface(chip, REG15_BQ25882_ADDRESS,
		0x80, 0xFF);
	if (rc < 0)
		pr_err("Couldn't bq25882_adc_en_enable  rc = %d\n", rc);

	return rc;
}

int bq25882_ibus_adc_dis_enable(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	rc = bq25882_config_interface(chip, REG16_BQ25882_ADDRESS,
		0x00, 0xFF);
	if (rc < 0)
		pr_err("Couldn't bq25882_ibus_adc_dis_enable  rc = %d\n", rc);

	return rc;
}

int bq25882_ichg_adc_dis_enable(struct bq_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;
	rc = bq25882_config_interface(chip, REG16_BQ25882_ADDRESS,
		0x00, 0xFF);
	if (rc < 0)
		pr_err("Couldn't bq25882_ichg_adc_dis_enable  rc = %d\n", rc);

	return rc;
}

#define DUMP_REG_LOG_CNT_30S             6
void bq25882_dump_registers(struct bq_chg_chip *chip)
{
	int rc = 0;
	int addr;

	unsigned int val_buf[BQ25882_REG_NUMBER] = {0x0};

	if (atomic_read(&chip->charger_suspended) == 1)
		return;
	for (addr = BQ25882_FIRST_REG; addr <= BQ25882_LAST_REG; addr++) {
		rc = bq25882_read_reg(chip, addr, &val_buf[addr]);
		if (rc)
			pr_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
		else
			pr_err("bq25882_0x%02x = 0x%x\n", addr, val_buf[addr]);
	}
}
int bq25882_get_battery_status(struct bq_chg_chip *chip)
{
	int rc = 0;
	int stat = 0, value = 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = bq25882_read_reg(chip, REG0B_BQ25882_ADDRESS, &stat);
	if (rc)
		pr_err("Couldn't read 0x%02x rc = %d\n",
			REG0B_BQ25882_ADDRESS, rc);

	stat &= REG0B_BQ25882_CHGSTAT_MASK;
	pr_debug("%s:CHG STAT = 0x%02x\n", __func__, stat);

	switch (stat) {
	case BQ_NOT_CHARGE:
		value = POWER_SUPPLY_STATUS_NOT_CHARGING;
	break;
	case BQ_TRICKLE_CHARGE:
	case BQ_PRE_CHARGE:
	case BQ_FAST_CHARGE:
	case BQ_TAPER_CHARGE:
		value = POWER_SUPPLY_STATUS_CHARGING;
	break;
	case BQ_TOP_OFF_TIMER_CHARGE:
	case BQ_TERMINATE_CHARGE:
		value = POWER_SUPPLY_STATUS_FULL;
	break;
	default:
		value = POWER_SUPPLY_STATUS_UNKNOWN;
	break;
	}

	return value;
}
int bq25882_hardware_init(struct bq_chg_chip *chip)
{
	chip->hw_aicl_point = 4440;
	chip->sw_aicl_point = 4500;

	bq25882_reset_charger(chip);
	bq25882_input_current_limit_write(chip, 1000);
	bq25882_float_voltage_write(chip, 8640);
	bq25882_set_enable_volatile_writes(chip);
	bq25882_set_complete_charge_timeout(chip, OVERTIME_DISABLED);
	bq25882_set_prechg_current(chip, 300);
	bq25882_charging_current_write_fast(chip, 1500);
	bq25882_set_topoff_timer(chip);
	bq25882_set_termchg_current(chip, REG04_BQ25882_ITERM_LIMIT_200MA);
	bq25882_set_rechg_voltage(chip, 200);
	bq25882_set_vindpm_vol(chip, chip->hw_aicl_point);
	bq25882_ico_disable(chip);
	bq25882_adc_en_enable(chip);
	bq25882_ibus_adc_dis_enable(chip);
	bq25882_ichg_adc_dis_enable(chip);
	bq25882_unsuspend_charger(chip);
	bq25882_enable_charging(chip);
	bq25882_set_wdt_timer(chip, 0);
	return true;
}

static int bq25882_driver_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct bq_chg_chip	*chip;

	chip = devm_kzalloc(&client->dev,
	sizeof(struct bq_chg_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->dev = &client->dev;

	atomic_set(&chip->charger_suspended, 0);
	bq25882_dump_registers(chip);

	bq25882_hardware_init(chip);
	bq25882_chip = chip;

	return 0;
}

static struct i2c_driver bq25882_i2c_driver;
static int bq25882_driver_remove(struct i2c_client *client)
{
	int ret = 0;

	pr_info("ret = %d\n", ret);
	return 0;
}

static unsigned long suspend_tm_sec;
static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
		__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

static int bq25882_pm_resume(struct device *dev)
{
	unsigned long resume_tm_sec = 0, sleep_time = 0;
	int rc = 0;

	if (!bq25882_chip)
		return 0;
	atomic_set(&bq25882_chip->charger_suspended, 0);
	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		pr_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	if (sleep_time < 0)
		sleep_time = 0;
	return 0;
}

static int bq25882_pm_suspend(struct device *dev)
{
	if (!bq25882_chip)
		return 0;
	atomic_set(&bq25882_chip->charger_suspended, 1);
	if (get_current_time(&suspend_tm_sec)) {
		pr_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	return 0;
}

static const struct dev_pm_ops bq25882_pm_ops = {
	.resume		= bq25882_pm_resume,
	.suspend		= bq25882_pm_suspend,
};
static void bq25882_reset(struct i2c_client *client)
{
	if (!bq25882_chip)
		return;
	bq25882_input_current_limit_write(bq25882_chip, 500);
}

static const struct of_device_id bq25882_match[] = {
	{ .compatible = "ti,bq25882-charger"},
	{ },
};

static const struct i2c_device_id bq25882_id[] = {
	{"bq25882-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, bq25882_id);

static struct i2c_driver bq25882_i2c_driver = {
	.driver		= {
		.name = "bq25882-charger",
		.owner	= THIS_MODULE,
		.of_match_table = bq25882_match,
		.pm = &bq25882_pm_ops,
	},
	.probe		= bq25882_driver_probe,
	.remove		= bq25882_driver_remove,
	.shutdown	= bq25882_reset,
	.id_table	= bq25882_id,
};


module_i2c_driver(bq25882_i2c_driver);
MODULE_DESCRIPTION("Driver for bq25882 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:bq25882-charger");
