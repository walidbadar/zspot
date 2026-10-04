/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery.h"
#include "ui.h"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

static const struct device *const gauge = DEVICE_DT_GET(DT_ALIAS(fuel_gauge0));

static void battery_poll(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(battery_work, battery_poll);

static void battery_poll(struct k_work *work)
{
	union fuel_gauge_prop_val val;
	int ret = fuel_gauge_get_prop(gauge, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE_PCT, &val);

	if (ret == 0) {
		ui_set_battery(val.relative_state_of_charge_pct);
	} else {
		/* No reading, e.g. the battery was removed: no indicator */
		LOG_DBG("Fuel gauge read failed: %d", ret);
		ui_set_battery(-1);
	}

	k_work_reschedule(&battery_work, K_SECONDS(CONFIG_ZSPOT_BATTERY_POLL_SECONDS));
}

void battery_init(void)
{
	if (!device_is_ready(gauge)) {
		LOG_WRN("Fuel gauge %s is not ready", gauge->name);
		return;
	}

	k_work_reschedule(&battery_work, K_NO_WAIT);
}
