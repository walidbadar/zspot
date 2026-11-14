# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_credentials_test)

set(ZSPOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# The store of the player application, with the library replaced by fakes
target_sources(app PRIVATE src/main.c ${ZSPOT_ROOT}/app/src/credentials.c)
target_include_directories(app PRIVATE ${ZSPOT_ROOT}/app/src ${ZSPOT_ROOT}/include)
target_compile_definitions(app PRIVATE CONFIG_ZSPOT_STORE_CREDENTIALS=1)
