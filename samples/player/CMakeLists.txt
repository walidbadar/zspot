# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

# When built outside a west workspace that lists this module, point Zephyr at it.
if(NOT DEFINED ZEPHYR_EXTRA_MODULES)
  set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cspot_player)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED app PRIVATE src/wifi.c)
if(NOT "${CONFIG_CSPOT_SAMPLE_PCM_FILE}" STREQUAL "")
  target_sources(app PRIVATE src/pcm_file_sink.c)
  target_compile_definitions(app PRIVATE CSPOT_SAMPLE_HAVE_PCM_FILE=1)
endif()
