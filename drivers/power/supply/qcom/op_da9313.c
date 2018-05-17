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
#include "op_da9313.h"

static DEFINE_MUTEX(da9313_i2c_access);

static int __da9313_read_reg(struct op_chg_chip *chip,
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

static int da9313_read_reg(struct op_chg_chip *chip, int reg, int *returnData)
{
	int ret = 0;

	mutex_lock(&da9313_i2c_access);
	ret = __da9313_read_reg(chip, reg, returnData);
	mutex_unlock(&da9313_i2c_access);
	return ret;
}
static int __da9313_write_reg(struct op_chg_chip *chip, int reg, int val)
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

static int da9313_config_interface(struct op_chg_chip *chip,
		int RegNum, int val, int MASK)
{
	int da9313_reg = 0;
	int ret = 0;

	mutex_lock(&da9313_i2c_access);
	ret = __da9313_read_reg(chip, RegNum, &da9313_reg);

	da9313_reg &= ~MASK;
	da9313_reg |= val;
	ret = __da9313_write_reg(chip, RegNum, da9313_reg);
	__da9313_read_reg(chip, RegNum, &da9313_reg);
	mutex_unlock(&da9313_i2c_access);

	return ret;
}

void da9313_dump_registers(struct op_chg_chip *chip)
{
	int rc;
	int addr;
	unsigned int val_buf[DA9313_REG2_NUMBER] = {0x0};

	for (addr = DA9313_FIRST_REG; addr <= DA9313_LAST_REG; addr++) {
		rc = da9313_read_reg(chip, addr, &val_buf[addr]);
		if (rc)
			pr_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
		else
			pr_err("%s success addr = %d, value = 0x%x\n",
			__func__, addr, val_buf[addr]);
	}

	for (addr = DA9313_FIRST2_REG; addr <= DA9313_LAST2_REG; addr++) {
		rc = da9313_read_reg(chip, addr, &val_buf[addr]);
		if (rc)
			pr_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
	}
}

int da9313_hardware_init(struct op_chg_chip *chip)
{
	int rc = 0;

	rc = da9313_config_interface(chip,
		REG04_DA9313_ADDRESS, REG04_DA9313_PVC_MODE_AUTO,
		REG04_DA9313_PVC_MODE_MASK);

	return true;
}


static int da9313_driver_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct op_chg_chip	*chip;

	pr_info("call\n");
	chip = devm_kzalloc(&client->dev,
		sizeof(struct op_chg_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->dev = &client->dev;
	da9313_dump_registers(chip);
	da9313_hardware_init(chip);
	return 0;
}

static struct i2c_driver da9313_i2c_driver;

static int da9313_driver_remove(struct i2c_client *client)
{
	int ret = 0;

	pr_info("  ret = %d\n", ret);
	return 0;
}

static const struct of_device_id da9313_match[] = {
	{ .compatible = "dlg,da9313-divider"},
	{ },
};

static const struct i2c_device_id da9313_id[] = {
	{"da9313-divider", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, da9313_id);


static struct i2c_driver da9313_i2c_driver = {
	.driver		= {
		.name = "da9313-divider",
		.owner	= THIS_MODULE,
		.of_match_table = da9313_match,
	},
	.probe		= da9313_driver_probe,
	.remove		= da9313_driver_remove,
	.id_table	= da9313_id,
};

module_i2c_driver(da9313_i2c_driver);
MODULE_DESCRIPTION("Driver for da9313 divider chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:da9313-divider");
