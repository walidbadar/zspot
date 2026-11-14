# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_lyrics_test)

set(ZSPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# src/main.c includes app/src/lyrics.c, to get at what is internal to it. The
# file is built as in a player with the UI, with stand-ins for LVGL (../stubs)
# and, in main.c, for the HTTP client and the UI.
target_sources(app PRIVATE src/main.c ${ZSPOT_ROOT}/app/src/json_scan.c)
target_include_directories(app PRIVATE ../stubs ${ZSPOT_ROOT}/app/src ${ZSPOT_ROOT}/include)
target_compile_definitions(app PRIVATE CONFIG_ZSPOT_UI=1)
