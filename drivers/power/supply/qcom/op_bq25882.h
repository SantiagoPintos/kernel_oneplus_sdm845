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

#ifndef __BQ25882_H__
#define __BQ25882_H__

#include <linux/power_supply.h>

struct bq_chg_chip {
	struct i2c_client	*client;
	struct device       *dev;
	const struct op_chg_operations *chg_ops;

	struct power_supply	*ac_psy;
	struct power_supply	*usb_psy;
	struct power_supply	batt_psy;
	struct delayed_work	update_work;
	struct delayed_work mmi_adapter_in_work;
	atomic_t charger_suspended;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_int_active;
	bool		charger_exist;
	int			charger_type;
	int			charger_volt;
	int			charger_volt_pre;
	int			chg_pretype;
	int			temperature;
	int			batt_volt;
	int         batt_volt_2cell_max;
	int         batt_volt_2cell_min;
	int			icharging;
	int			soc;
	int			ui_soc;
	int			soc_load;
	bool		authenticate;
	int			hw_aicl_point;
	int			sw_aicl_point;
	int			bq_irq;

	int			batt_fcc;
	int			batt_cc;
	int			batt_soh;
	int			batt_rm;
	int			batt_capacity_mah;

	bool		batt_exist;
	bool		batt_full;
	bool		chging_on;
	bool		in_rechging;
	int			charging_state;
	int			total_time;
	unsigned long sleep_tm_sec;
	bool		vbatt_over;
	bool		chging_over_time;
	int			vchg_status;
	int			tbatt_status;
	int			prop_status;
	int			stop_voter;
	int			notify_code;
	int			notify_flag;
	int			request_power_off;

	bool		led_on;
	bool		led_status_change;
	bool		camera_on;
	bool		calling_on;

	bool		ac_online;
	bool		otg_switch;
	int			mmi_chg;
	int			mmi_fastchg;
	int			boot_reason;
	int			boot_mode;
	bool		vooc_project;
	bool		suspend_after_full;
	bool		check_batt_full_by_sw;
	bool		external_gauge;
	bool		chg_ctrl_by_lcd;
	bool		chg_ctrl_by_camera;
	bool		chg_ctrl_by_calling;
	bool		fg_bcl_poll;
	int			chargerid_volt;
	bool		chargerid_volt_got;
	int			enable_shipmode;
	bool		overtemp_status;
};

#define BQ25882_FIRST_REG	0x00
#define BQ25882_LAST_REG	0x25
#define BQ25882_REG_NUMBER	0x26

/* Address:00h */
#define REG00_BQ25882_ADDRESS                     0x00

#define REG00_BQ25882_BAT_THRESHOLD_MASK          (BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG00_BQ25882_BAT_THRESHOLD_SHIFT         0
#define REG00_BQ25882_BAT_THRESHOLD_OFFSET        6800
#define REG00_BQ25882_BAT_THRESHOLD_STEP          10
#define REG00_BQ25882_BAT_THRESHOLD_8600MV        (BIT(7) | BIT(5) | BIT(4) | BIT(2))
#define REG00_BQ25882_BAT_THRESHOLD               REG00_BQ25882_BAT_THRESHOLD_8600MV

/* Address:01h */
#define REG01_BQ25882_ADDRESS                     0x01

#define REG01_BQ25882_FAST_CURRENT_LIMIT_MASK     (BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG01_BQ25882_FAST_CURRENT_LIMIT_SHIFT    0
#define REG01_BQ25882_FAST_CURRENT_LIMIT_OFFSET   0
#define REG01_BQ25882_FAST_CURRENT_LIMIT_STEP     50
#define REG01_BQ25882_FAST_CURRENT_LIMIT_1500MA   (BIT(4) | BIT(3) | BIT(2) | BIT(1))
#define REG01_BQ25882_FAST_CURRENT_LIMIT          REG01_BQ25882_FAST_CURRENT_LIMIT_1500MA

#define REG01_BQ25882_EN_ILIM_MASK                BIT(6)
#define REG01_BQ25882_EN_ILIM_DISABLE             0
#define REG01_BQ25882_EN_ILIM_ENABLE              BIT(6)

#define REG01_BQ25882_EN_HIZ_MASK                 BIT(7)
#define REG01_BQ25882_EN_HIZ_DISABLE              0
#define REG01_BQ25882_EN_HIZ_ENABLE               BIT(7)

/* Address:02h */
#define REG02_BQ25882_ADDRESS                     0x02

#define REG02_BQ25882_INPUT_VOLTAGE_LIMIT_MASK    (BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG02_BQ25882_INPUT_VOLTAGE_LIMIT_SHIFT   0
#define REG02_BQ25882_INPUT_VOLTAGE_LIMIT_OFFSET  3900
#define REG02_BQ25882_INPUT_VOLTAGE_LIMIT_STEP    100
#define REG02_BQ25882_INPUT_VOLTAGE_LIMIT_4400MV  (BIT(2) | BIT(0))
#define REG02_BQ25882_INPUT_VOLTAGE_LIMIT         REG02_BQ25882_INPUT_VOLTAGE_LIMIT_4400MV

#define REG02_BQ25882_EN_VINDPM_RST_MASK          BIT(7)
#define REG02_BQ25882_EN_VINDPM_RST_DISABLE       0
#define REG02_BQ25882_EN_VINDPM_RST_ENBALE        BIT(7)


/* Address:03h */
#define REG03_BQ25882_ADDRESS                     0x03

#define REG03_BQ25882_INPUT_CURRENT_LIMIT_MASK    (BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG03_BQ25882_INPUT_CURRENT_LIMIT_SHIFT   0
#define REG03_BQ25882_INPUT_CURRENT_LIMIT_OFFSET  500
#define REG03_BQ25882_INPUT_CURRENT_LIMIT_STEP    100
#define REG03_BQ25882_INPUT_CURRENT_LIMIT_1000MA  (BIT(2) | BIT(0))
#define REG03_BQ25882_INPUT_CURRENT_LIMIT         REG03_BQ25882_INPUT_CURRENT_LIMIT_1000MA

#define REG03_BQ25882_EN_ICO_MASK                 BIT(5)
#define REG03_BQ25882_EN_ICO_DISABLE              0
#define REG03_BQ25882_EN_ICO_ENABLE               BIT(5)

#define REG03_BQ25882_FORCE_INDET_MASK            BIT(6)
#define REG03_BQ25882_FORCE_INDET_DISABLE         0
#define REG03_BQ25882_FORCE_INDET_ENABLE          BIT(6)

#define REG03_BQ25882_FORCE_ICO_MASK              BIT(7)
#define REG03_BQ25882_FORCE_ICO_DISABLE           0
#define REG03_BQ25882_FORCE_ICO_ENABLE            BIT(7)

/* Address:04h */
#define REG04_BQ25882_ADDRESS                     0x04

#define REG04_BQ25882_ITERM_LIMIT_MASK            (BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG04_BQ25882_ITERM_LIMIT_SHIFT           0
#define REG04_BQ25882_ITERM_LIMIT_OFFSET          50
#define REG04_BQ25882_ITERM_LIMIT_STEP            50
#define REG04_BQ25882_ITERM_LIMIT_100MA           BIT(0)
#define REG04_BQ25882_ITERM_LIMIT_150MA           BIT(1)
#define REG04_BQ25882_ITERM_LIMIT_200MA           (BIT(1) | BIT(0))
#define REG04_BQ25882_ITERM_LIMIT_600MA           (BIT(3) | BIT(1) | BIT(0))
#define REG04_BQ25882_ITERM_LIMIT                 REG04_BQ25882_ITERM_LIMIT_600MA

#define REG04_BQ25882_IPRECHG_LIMIT_MASK          (BIT(7) | BIT(6) | BIT(5) | BIT(4))
#define REG04_BQ25882_IPRECHG_LIMIT_SHIFT         4
#define REG04_BQ25882_IPRECHG_LIMIT_OFFSET        50
#define REG04_BQ25882_IPRECHG_LIMIT_STEP          50
#define REG04_BQ25882_IPRECHG_LIMIT_150MA         BIT(5)
#define REG04_BQ25882_IPRECHG_LIMIT               REG04_BQ25882_IPRECHG_LIMIT_150MA

/* Address:05h */
#define REG05_BQ25882_ADDRESS                     0x05

#define REG05_BQ25882_TMR2X_EN_MASK               BIT(0)
#define REG05_BQ25882_TMR2X_EN_DISABLE            0
#define REG05_BQ25882_TMR2X_EN_ENABLE             BIT(0)


#define REG05_BQ25882_CHG_TIMER_MASK              (BIT(2) | BIT(1))
#define REG05_BQ25882_CHG_TIMER_5H                0
#define REG05_BQ25882_CHG_TIMER_8H                BIT(1)
#define REG05_BQ25882_CHG_TIMER_12H               BIT(2)
#define REG05_BQ25882_CHG_TIMER_20H               (BIT(2) | BIT(1))

#define REG05_BQ25882_EN_TIMER_MASK               BIT(3)
#define REG05_BQ25882_EN_TIMER_DISABLE            0
#define REG05_BQ25882_EN_TIMER_ENABLE             BIT(3)

#define REG05_BQ25882_WATCHDOG_MASK               (BIT(5) | BIT(4))
#define REG05_BQ25882_WATCHDOG_DISABLE            0
#define REG05_BQ25882_WATCHDOG_40S                BIT(4)
#define REG05_BQ25882_WATCHDOG_80S                BIT(5)
#define REG05_BQ25882_WATCHDOG_160S               (BIT(5) | BIT(4))
#define REG05_BQ25882_WATCHDOG                    REG05_BQ25882_WATCHDOG_DISABLE

/*BIT(6) reserved*/
#define REG05_BQ25882_EN_TERM_MASK                BIT(7)
#define REG05_BQ25882_EN_TERM_DISABLE             0
#define REG05_BQ25882_EN_TERM_ENABLE              BIT(7)

/* Address:06h */
#define REG06_BQ25882_ADDRESS                     0x06

#define REG06_BQ25882_VRECHG_MASK                 (BIT(1) | BIT(0))
#define REG06_BQ25882_VRECHG_100MV                0
#define REG06_BQ25882_VRECHG_200MV                BIT(0)
#define REG06_BQ25882_VRECHG_300MV                BIT(1)
#define REG06_BQ25882_VRECHG_400MV                (BIT(1) | BIT(0))
#define REG06_BQ25882_VRECHG                      REG06_BQ25882_VRECHG_100MV

#define REG06_BQ25882_BATLOWV_MASK                BIT(2)
#define REG06_BQ25882_BATLOWV_5600MV              0
#define REG06_BQ25882_BATLOWV_6000MV              BIT(2)

#define REG06_BQ25882_EN_CHG_MASK                 BIT(3)
#define REG06_BQ25882_EN_CHG_DISABLE              0
#define REG06_BQ25882_EN_CHG_ENABLE               BIT(3)

#define REG06_BQ25882_TREG_MASK                   (BIT(5) | BIT(4))
#define REG06_BQ25882_TREG_60_0C                  0
#define REG06_BQ25882_TREG_80_0C                  BIT(4)
#define REG06_BQ25882_TREG_100_0C                 BIT(5)
#define REG06_BQ25882_TREG_120_0C                 (BIT(5) | BIT(4))

#define REG06_BQ25882_AUTO_INDET_EN_MASK          BIT(6)
#define REG06_BQ25882_AUTO_INDET_EN_DISABLE       0
#define REG06_BQ25882_AUTO_INDET_EN_ENABLE        BIT(6)

#define REG06_BQ25882_EN_OTG_MASK                 BIT(7)
#define REG06_BQ25882_EN_OTG_DISABLE              0
#define REG06_BQ25882_EN_OTG_ENABLE               BIT(7)

/* Address:07h */
#define REG07_BQ25882_ADDRESS                     0x07

#define REG07_BQ25882_SYS_MIN_MASK                (BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG07_BQ25882_SYS_MIN_SHIFT               0
#define REG07_BQ25882_SYS_MIN_OFFSET              6000
#define REG07_BQ25882_SYS_MIN_STEP                100
#define REG07_BQ25882_SYS_MIN_7000MV              (BIT(3) | BIT(1))
#define REG07_BQ25882_SYS_MIN                     REG07_BQ25882_SYS_MIN_7000MV

#define REG07_BQ25882_TOPOFF_TIMER_MASK           (BIT(5) | BIT(4))
#define REG07_BQ25882_TOPOFF_TIMER_DISABLE        0
#define REG07_BQ25882_TOPOFF_TIMER_15MIN          BIT(4)
#define REG07_BQ25882_TOPOFF_TIMER_30MIN          BIT(5)
#define REG07_BQ25882_TOPOFF_TIMER_45MIN          (BIT(5) | BIT(4))
#define REG07_BQ25882_TOPOFF                      REG07_BQ25882_TOPOFF_TIMER_15MIN

#define REG07_BQ25882_WD_RST_MASK                 BIT(6)
#define REG07_BQ25882_WD_RST_NORMAL               0
#define REG07_BQ25882_WD_RST_RESET                BIT(6)

#define REG07_BQ25882_PFM_DIS_MASK                BIT(7)
#define REG07_BQ25882_PFM_DIS_ENABLE              0
#define REG07_BQ25882_PFM_DIS_DISABLE             BIT(7)

/* Address:08h */
#define REG08_BQ25882_ADDRESS                     0x08

/* Address:09h */
#define REG09_BQ25882_ADDRESS                     0x09

#define REG09_BQ25882_OTG_VLIM_MASK               (BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG09_BQ25882_OTG_VLIM_SHIFT              0
#define REG09_BQ25882_OTG_VLIM_OFFSET             4500
#define REG09_BQ25882_OTG_VLIM_STEP               100
#define REG09_BQ25882_OTG_VLIM_5100MV             (BIT(2) | BIT(1))
#define REG09_BQ25882_OTG_VLIM                    REG09_BQ25882_OTG_VLIM_5100MV

#define REG09_BQ25882_OTG_ILIM_MASK               (BIT(7) | BIT(6) | BIT(5) | BIT(4))
#define REG09_BQ25882_OTG_ILIM_SHIFT              4
#define REG09_BQ25882_OTG_ILIM_OFFSET             500
#define REG09_BQ25882_OTG_ILIM_STEP               100
#define REG09_BQ25882_OTG_ILIM_500MA              0
#define REG09_BQ25882_OTG_ILIM                    REG09_BQ25882_OTG_ILIM_500MA


/* Address:0Ah */
#define REG0A_BQ25882_ADDRESS                     0x0A

#define REG0A_BQ25882_ICO_ILIM_MASK               (BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG0A_BQ25882_ICO_ILIM_SHIFT              0
#define REG0A_BQ25882_ICO_ILIM_OFFSET             500
#define REG0A_BQ25882_ICO_ILIM_STEP               100
#define REG0A_BQ25882_ICO_ILIM_500MA              0
#define REG0A_BQ25882_ICO_ILIM                    REG0A_BQ25882_ICO_ILIM_500MA

/* Address:0Bh */
#define REG0B_BQ25882_ADDRESS                     0x0B

#define REG0B_BQ25882_CHGSTAT_MASK                (BIT(2) | BIT(1) | BIT(0))
#define REG0B_BQ25882_CHGSTAT_NOCHG               0
#define REG0B_BQ25882_CHGSTAT_TRICKLE             BIT(0)
#define REG0B_BQ25882_CHGSTAT_PRECHG              BIT(1)
#define REG0B_BQ25882_CHGSTAT_CCCHG               (BIT(1) | BIT(0))
#define REG0B_BQ25882_CHGSTAT_TAPECHG             BIT(2)
#define REG0B_BQ25882_CHGSTAT_TOPOFFCHG           (BIT(2) | BIT(0))
#define REG0B_BQ25882_CHGSTAT_CHGDONE             (BIT(2) | BIT(1))

#define REG0B_BQ25882_WD_STAT_MASK                BIT(3)
#define REG0B_BQ25882_WD_STAT_NORMAL              0
#define REG0B_BQ25882_WD_STAT_EXPIRED             BIT(3)

#define REG0B_BQ25882_TREG_STAT_MASK              BIT(4)
#define REG0B_BQ25882_TREG_STAT_NORMAL            0
#define REG0B_BQ25882_TREG_STAT_THERMAL           BIT(4)

#define REG0B_BQ25882_VINDPM_STAT_MASK            BIT(5)
#define REG0B_BQ25882_VINDPM_STAT_NORMAL          0
#define REG0B_BQ25882_VINDPM_STAT_VINDPM          BIT(5)

#define REG0B_BQ25882_IINDPM_STAT_MASK            BIT(6)
#define REG0B_BQ25882_IINDPM_STAT_NORMAL          0
#define REG0B_BQ25882_IINDPM_STAT_IINDPM          BIT(6)

#define REG0B_BQ25882_ADC_DONE_STAT_MASK          BIT(7)
#define REG0B_BQ25882_ADC_DONE_STAT_NOT           0
#define REG0B_BQ25882_ADC_DONE_STAT_DONE          BIT(7)

/* Address:0Ch */
#define REG0C_BQ25882_ADDRESS                     0x0C

#define REG0C_BQ25882_VSYS_STAT_MASK              BIT(0)
#define REG0C_BQ25882_VSYS_STAT_NOT               0
#define REG0C_BQ25882_VSYS_STAT_IN                BIT(0)

#define REG0C_BQ25882_ICO_STAT_MASK               BIT(1)
#define REG0C_BQ25882_ICO_STAT_OPT                0
#define REG0C_BQ25882_ICO_STAT_MAX                BIT(1)

#define REG0C_BQ25882_VBUS_STAT_MASK              (BIT(6) | BIT(5) | BIT(4))
#define REG0C_BQ25882_VBUS_STAT_NO_INPUT          0
#define REG0C_BQ25882_VBUS_STAT_SDP               BIT(4)
#define REG0C_BQ25882_VBUS_STAT_CDP               BIT(5)
#define REG0C_BQ25882_VBUS_STAT_DCP               (BIT(5) | BIT(4))
#define REG0C_BQ25882_VBUS_STAT_POORSRC           BIT(6)
#define REG0C_BQ25882_VBUS_STAT_UNKOWN            (BIT(6) | BIT(4))
#define REG0C_BQ25882_VBUS_STAT_NON_STD           (BIT(6) | BIT(5))
#define REG0C_BQ25882_VBUS_STAT_OTG               (BIT(6) | BIT(5) | BIT(4))

#define REG0C_BQ25882_PG_STAT_MASK                BIT(7)
#define REG0C_BQ25882_PG_STAT_NOTGOOD             0
#define REG0C_BQ25882_PG_STAT_GOOD                BIT(7)

/* Address:15h */
#define REG15_BQ25882_ADDRESS                     0x15
#define REG15_BQ25882_ADC_EN_MASK                 BIT(7)
#define REG15_BQ25882_ADC_EN_DISABLE              0
#define REG15_BQ25882_ADC_EN_ENABLE               BIT(7)

/* Address:16h */
#define REG16_BQ25882_ADDRESS                     0x16
#define REG16_BQ25882_IBUS_ADC_DIS_MASK           BIT(7)
#define REG16_BQ25882_IBUS_ADC_DIS_DISABLE        0
#define REG16_BQ25882_IBUS_ADC_DIS_ENABLE         BIT(7)

#define REG16_BQ25882_ICHG_ADC_DIS_MASK           BIT(6)
#define REG16_BQ25882_ICHG_ADC_DIS_DISABLE        0
#define REG16_BQ25882_ICHG_ADC_DIS_ENABLE         BIT(6)

/* Address:25h */
#define REG25_BQ25882_ADDRESS                     0x25

#define REG25_BQ25882_RESET_MASK                  BIT(7)
#define REG25_BQ25882_RESET_KEEP                  0
#define REG25_BQ25882_RESET_DEFAULT               BIT(7)

enum {
	OVERTIME_AC = 0,
	OVERTIME_USB,
	OVERTIME_DISABLED,
};

enum {
	BQ_NOT_CHARGE = 0,
	BQ_TRICKLE_CHARGE,
	BQ_PRE_CHARGE,
	BQ_FAST_CHARGE,
	BQ_TAPER_CHARGE,
	BQ_TOP_OFF_TIMER_CHARGE,
	BQ_TERMINATE_CHARGE,
};
extern struct bq_chg_chip *bq25882_chip;
extern int bq25882_input_current_limit_write(struct bq_chg_chip *chip, int value);
extern int bq25882_charging_current_write_fast(struct bq_chg_chip *chip, int chg_cur);
extern int bq25882_set_vindpm_vol(struct bq_chg_chip *chip, int vol);
extern int bq25882_set_enable_volatile_writes(struct bq_chg_chip *chip);
extern int bq25882_set_complete_charge_timeout(struct bq_chg_chip *chip, int val);
extern int bq25882_float_voltage_write(struct bq_chg_chip *chip, int vfloat_mv);
extern int bq25882_set_prechg_current(struct bq_chg_chip *chip, int ipre_mA);
extern int bq25882_set_topoff_timer(struct bq_chg_chip *chip);
extern int bq25882_set_termchg_current(struct bq_chg_chip *chip, int term_curr);
extern int bq25882_set_rechg_voltage(struct bq_chg_chip *chip, int recharge_mv);
extern int bq25882_set_wdt_timer(struct bq_chg_chip *chip, int reg);
extern int bq25882_set_chging_term_disable(struct bq_chg_chip *chip);
extern int bq25882_kick_wdt(struct bq_chg_chip *chip);
extern int bq25882_enable_charging(struct bq_chg_chip *chip);
extern int bq25882_disable_charging(struct bq_chg_chip *chip);
extern int bq25882_check_charging_enable(struct bq_chg_chip *chip);
extern int bq25882_registers_read_full(struct bq_chg_chip *chip);
extern int bq25882_suspend_charger(struct bq_chg_chip *chip);
extern int bq25882_unsuspend_charger(struct bq_chg_chip *chip);
extern bool bq25882_check_charger_resume(struct bq_chg_chip *chip);
extern int bq25882_reset_charger(struct bq_chg_chip *chip);
extern int bq25882_otg_enable(struct bq_chg_chip *chip);
extern int bq25882_otg_disable(struct bq_chg_chip *chip);
extern int bq25882_ico_disable(struct bq_chg_chip *chip);
extern int bq25882_adc_en_enable(struct bq_chg_chip *chip);
extern int bq25882_ibus_adc_dis_enable(struct bq_chg_chip *chip);
extern int bq25882_ichg_adc_dis_enable(struct bq_chg_chip *chip);
extern void bq25882_dump_registers(struct bq_chg_chip *chip);
extern int bq25882_get_battery_status(struct bq_chg_chip *chip);
extern int bq25882_hardware_init(struct bq_chg_chip *chip);
#endif
