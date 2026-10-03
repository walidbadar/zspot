/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Logging glue on top of the Zephyr logging subsystem.
 *
 * Every translation unit that logs places ZSPOT_LOG_MODULE_DECLARE(); at file
 * scope. The module itself is registered once in port/log.cpp.
 */

#ifndef ZSPOT_PORT_LOG_H_
#define ZSPOT_PORT_LOG_H_

#include <zephyr/logging/log.h>

#define ZSPOT_LOG_MODULE_DECLARE() LOG_MODULE_DECLARE(zspot, CONFIG_ZSPOT_LOG_LEVEL)

#define CSPOT_LOG_LEVEL_info  LOG_INF
#define CSPOT_LOG_LEVEL_debug LOG_DBG
#define CSPOT_LOG_LEVEL_error LOG_ERR
#define CSPOT_LOG_LEVEL_warn  LOG_WRN

/** Keeps the upstream cspot call style: CSPOT_LOG(info, "fmt", ...). */
#define CSPOT_LOG(level, ...) CSPOT_LOG_LEVEL_##level(__VA_ARGS__)

#endif /* ZSPOT_PORT_LOG_H_ */
