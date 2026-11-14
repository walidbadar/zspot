# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_webapi_test)

set(ZSPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# src/main.c includes app/src/webapi.c, to get at what is internal to it, with
# a stand-in for the HTTP client in main.c. The file is built as in a player
# that keeps credentials, and as by a build that carries made-up ones in the
# form of the real ones.
target_sources(app PRIVATE src/main.c ../stubs/player_log.c ${ZSPOT_ROOT}/app/src/json_scan.c)
target_include_directories(app PRIVATE ${ZSPOT_ROOT}/app/src ${ZSPOT_ROOT}/include)
target_compile_definitions(app PRIVATE
  CONFIG_ZSPOT_CREDENTIALS_STORED=1
  CONFIG_ZSPOT_WEB_CREDENTIALS_BUILT_IN=1
  "CONFIG_ZSPOT_WEB_CLIENT_ID=\"id\""
  "CONFIG_ZSPOT_WEB_CLIENT_SECRET=\"secret\""
  "CONFIG_ZSPOT_WEB_REFRESH_TOKEN=\"AQBx-Example_refresh-token_0123456789abcdefghijklmnopqrstuvwxyz\""
)
