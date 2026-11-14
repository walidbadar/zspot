# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_base62_test)

set(ZSPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# The helpers of the protocol core, which need nothing of the library around them
target_sources(app PRIVATE src/main.cpp ${ZSPOT_ROOT}/lib/zspot/src/core/Utils.cpp)
target_include_directories(app PRIVATE ${ZSPOT_ROOT}/lib/zspot/src)
