# SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: Apache-2.0

# Copy of the native_sim fuel gauge driver proposed for Zephyr in
# https://github.com/zephyrproject-rtos/zephyr/pull/116982, used while the
# Zephyr tree does not have it.
if(NOT EXISTS ${ZEPHYR_BASE}/drivers/fuel_gauge/linux_fuel_gauge)
  add_subdirectory_ifdef(CONFIG_NATIVE_LINUX_FUEL_GAUGE fuel_gauge/linux_fuel_gauge)
endif()
