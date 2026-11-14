# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_gain_test)

set(ZSPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# src/main.c includes lib/zspot/src/audio/i2s_sink.c. These are the options of
# the library that the sink reads, with the sink writing straight to the I2S
# blocks so that no feeder thread is created.
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${ZSPOT_ROOT}/lib/zspot/src ${ZSPOT_ROOT}/include)
target_compile_definitions(app PRIVATE
  CONFIG_ZSPOT_I2S_BLOCK_SIZE=4096
  CONFIG_ZSPOT_I2S_BLOCK_COUNT=8
  CONFIG_ZSPOT_I2S_TIMEOUT_MS=2000
  CONFIG_ZSPOT_I2S_BUFFER_MS=0
  CONFIG_ZSPOT_LOG_LEVEL=0
)
