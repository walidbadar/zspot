# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_webapi_test)

set(ZSPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# src/main.c includes app/src/webapi.c, to get at what is internal to it, with
# stand-ins for LVGL (../stubs) and, in main.c, for the HTTP client. The file
# is built without the setup endpoint and without credentials of the build.
target_sources(app PRIVATE src/main.c ../stubs/player_log.c ${ZSPOT_ROOT}/app/src/json_scan.c)
target_include_directories(app PRIVATE ../stubs ${ZSPOT_ROOT}/app/src ${ZSPOT_ROOT}/include)
target_compile_definitions(app PRIVATE
  "CONFIG_ZSPOT_WEB_CLIENT_ID=\"\""
  "CONFIG_ZSPOT_WEB_CLIENT_SECRET=\"\""
  "CONFIG_ZSPOT_WEB_REFRESH_TOKEN=\"\""
)
