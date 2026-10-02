/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Logging glue on top of the Zephyr logging subsystem.
 *
 * Every translation unit that logs places CSPOT_LOG_MODULE_DECLARE(); at file
 * scope. The module itself is registered once in port/log.cpp.
 */

#ifndef CSPOT_PORT_LOG_H_
#define CSPOT_PORT_LOG_H_

#include <zephyr/logging/log.h>

#define CSPOT_LOG_MODULE_DECLARE() LOG_MODULE_DECLARE(cspot, CONFIG_CSPOT_LOG_LEVEL)

#define CSPOT_LOG_LEVEL_info  LOG_INF
#define CSPOT_LOG_LEVEL_debug LOG_DBG
#define CSPOT_LOG_LEVEL_error LOG_ERR
#define CSPOT_LOG_LEVEL_warn  LOG_WRN

/** Keeps the upstream cspot call style: CSPOT_LOG(info, "fmt", ...). */
#define CSPOT_LOG(level, ...) CSPOT_LOG_LEVEL_##level(__VA_ARGS__)

#endif /* CSPOT_PORT_LOG_H_ */
