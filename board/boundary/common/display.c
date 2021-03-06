/*
 * Copyright 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <asm/mach-imx/fbpanel.h>
#include <asm/gpio.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <i2c.h>
#include <linux/delay.h>
#include <log.h>
#include "bd_common.h"

/* pca9546 mux */
static int detect_common(struct display_info_t const *di, int sub_bus,
		int sub_bus2, int reg1, u8 val1, int reg2, u8 val2,
		int gp_reset, int gp_irq)
{
	int ret = 0;
#ifdef CONFIG_DM_I2C
	struct udevice *bus;
	struct i2c_msg msg;
	struct dm_i2c_ops *ops;
	u8 buf[4];
#else
	u8 orig_i2c_bus;
#endif

#ifdef CONFIG_DM_VIDEO
	int req_bus;
	int req_en;

	if (di->bus_gp) {
		req_bus = gpio_request(di->bus_gp, "bus_gp");
	}
	if (di->enable_gp)
		req_en = gpio_request(di->enable_gp, "enable_gp");
#endif
	if (di->bus_gp)
		gpio_direction_output(di->bus_gp, 1);
	if (di->enable_gp && !(di->fbflags & FBF_ENABLE_GPIOS_OPEN_DRAIN))
		gpio_direction_output(di->enable_gp, 1);
	if (di->bus_gp_delay_ms)
		mdelay(di->bus_gp_delay_ms);
	if (gp_reset) {
		/* for gt911 */
		gpio_set_value(gp_reset, 0);
		mdelay(20);
		gpio_direction_output(gp_irq, di->addr_num == 0x14 ? 1 : 0);
		udelay(100);
		gpio_set_value(gp_reset, 1);
		mdelay(6);
		gpio_set_value(gp_irq, 0);
		mdelay(50);
		gpio_direction_input(gp_irq);
	}
#ifdef CONFIG_DM_I2C
	ret = uclass_get_device_by_seq(UCLASS_I2C, di->bus_num & 0xf, &bus);
	if (ret) {
		printf("%s: Can't find bus\n", __func__);
	} else {
		ops = i2c_get_ops(bus);
		msg.addr = 0x70;
		msg.flags = 0;
		msg.len = 1;
		msg.buf = buf;
		if (sub_bus) {
			buf[0] = sub_bus;
			ret = ops->xfer(bus, &msg, 1);
			debug("%s: sub_bus=0x%x ret=%d\n", __func__, sub_bus, ret);
		}
		/* write control register, select sub bus */
		if (!ret && sub_bus2) {
			buf[0] = sub_bus2;
			ret = ops->xfer(bus, &msg, 1);
			debug("%s: sub_bus2=0x%x ret=%d\n", __func__, sub_bus2, ret);
		}
		msg.addr = di->addr_num;
		if (!ret && (reg1 < 0)) {
			ret = i2c_probe_chip(bus, di->addr_num, 0);
			debug("%s: probe 0x%x ret=%d\n", __func__, di->addr_num, ret);
		}
		if (!ret && (reg1 >= 0)) {
			buf[0] = reg1;
			buf[1] = val1;
			msg.len = 2;
			ret = ops->xfer(bus, &msg, 1);
			if (ret < 0) {
				debug("%s: i2c write1 addr=0x%x reg=0x%x failed(%d)\n",
					__func__, msg.addr, reg1, ret);
			}
		}
		if (!ret && (reg2 >= 0)) {
			buf[0] = reg2;
			buf[1] = val2;
			msg.len = 2;
			ret = ops->xfer(bus, &msg, 1);
			if (ret < 0) {
				debug("%s: i2c write2 addr=0x%x reg=0x%x failed(%d)\n",
					__func__, msg.addr, reg2, ret);
			}
		}
	}
#else
	orig_i2c_bus = i2c_get_bus_num();
	ret = i2c_set_bus_num(di->bus_num & 0x0f);
	/* write control register, select sub bus */
	if (!ret && sub_bus)
		ret = i2c_write(0x70, sub_bus, 1, NULL, 0);
	if (!ret && sub_bus2)
		ret = i2c_write(0x70, sub_bus2, 1, NULL, 0);
	if (!ret)
		ret = i2c_probe(di->addr_num);

	if (!ret && (reg1 >= 0)) {
		ret = i2c_write(di->addr_num, reg1, 1, &val1, 1);
		if (ret < 0) {
			printf("%s: i2c write reg=0x%x failed(%d)\n",
				__func__, reg1, ret);
		}
	}
	if (!ret && (reg2 >= 0)) {
		ret = i2c_write(di->addr_num, reg2, 1, &val2, 1);
		if (ret < 0) {
			printf("%s: i2c write reg=0x%x failed(%d)\n",
				__func__, reg2, ret);
		}
	}
	i2c_set_bus_num(orig_i2c_bus);
#endif
	if (di->bus_gp)
		gpio_set_value(di->bus_gp, 0);
	if (ret) {
		if (di->bus_gp)
			gpio_direction_input(di->bus_gp);
		if (di->enable_gp && !(di->fbflags & FBF_ENABLE_GPIOS_OPEN_DRAIN))
			gpio_direction_input(di->enable_gp);
	}
#ifdef CONFIG_DM_VIDEO
	if (di->bus_gp && !req_bus) {
		gpio_free(di->bus_gp);
	}
	if (di->enable_gp && !req_en) {
		gpio_free(di->enable_gp);
	}
#endif
	return (ret == 0);
}

int board_detect_lcd133(struct display_info_t const *di)
{
	/* 0 - 0xf : VPOS 5.5V output */
	/* 1 - 0xf : VNEG -5.5V output */
	return detect_common(di, 1 << (di->bus_num >> 4), 0, 0, 0xf, 1, 0xf, 0, 0);
}

/* pca9540 mux */
int board_detect_pca9540(struct display_info_t const *di)
{
	return detect_common(di, (di->bus_num >> 4) ? 5 : 4, 0, -1, 0x0, -1, 0x0, 0, 0);
}

/* pca9546 mux */
int board_detect_pca9546(struct display_info_t const *di)
{
	return detect_common(di, 1 << (di->bus_num >> 4), 0, -1, 0x0, -1, 0x0, 0, 0);
}

int board_detect_pca9546_2(struct display_info_t const *di)
{
	return detect_common(di, 1 << (di->bus_num >> 4), 8, -1, 0x0, -1, 0x0, 0, 0);
}

int board_detect_gt911_common(struct display_info_t const *di, int sub_bus, int sub_bus2, int gp_reset, int gp_irq)
{
	return detect_common(di, sub_bus, sub_bus2, -1, 0x0, -1, 0x0, gp_reset, gp_irq);
}
