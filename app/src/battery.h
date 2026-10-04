/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * Battery indicator: reads the charge level from the fuel gauge behind the
 * devicetree alias fuel-gauge0 and shows it on the screen. Compiled in only
 * when the board has such a fuel gauge.
 */

#ifndef ZSPOT_SAMPLE_BATTERY_H_
#define ZSPOT_SAMPLE_BATTERY_H_

#if defined(CONFIG_ZSPOT_BATTERY)

/** Starts reading the fuel gauge periodically. Call after ui_init(). */
void battery_init(void);

#else

static inline void battery_init(void)
{
}

#endif /* CONFIG_ZSPOT_BATTERY */

#endif /* ZSPOT_SAMPLE_BATTERY_H_ */
