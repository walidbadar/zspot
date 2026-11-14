# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_track_test)

set(ZSPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# src/main.c includes app/src/track.c, to get at the store of the last song,
# which is internal to it. The file is built as in a player with the UI, with
# stand-ins in main.c for the UI and the Web API.
target_sources(app PRIVATE src/main.c ../stubs/player_log.c)
target_include_directories(app PRIVATE ${ZSPOT_ROOT}/app/src ${ZSPOT_ROOT}/include)
target_compile_definitions(app PRIVATE CONFIG_ZSPOT_UI=1)
