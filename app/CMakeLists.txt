# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

# When built from another west workspace, point Zephyr at this module.
if(NOT DEFINED ZEPHYR_EXTRA_MODULES)
  set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_player)

# Mbed TLS is built with zspot_mbedtls_config.h (CONFIG_MBEDTLS_USER_CONFIG_FILE)
zephyr_include_directories(mbedtls)

target_sources(app PRIVATE src/main.c src/certs.c)

# The root certificates that the servers are verified against
foreach(root digicert_global_root_g2 starfield_root_g2 digicert_global_root_g3
             globalsign_root_r3 gts_root_r4)
  generate_inc_file_for_target(app certs/${root}.der
    ${ZEPHYR_BINARY_DIR}/include/generated/certs/${root}.der.inc)
endforeach()
target_sources_ifdef(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED app PRIVATE src/wifi.c)
target_sources_ifdef(CONFIG_ZSPOT_CREDENTIALS_STORED app PRIVATE src/credentials.c)
target_sources_ifdef(CONFIG_ZSPOT_LAST_TRACK app PRIVATE src/last_track.c src/resume.c)
target_sources_ifdef(CONFIG_ZSPOT_PCM_TAP app PRIVATE src/pcm_tap.c)
target_sources_ifdef(CONFIG_ZSPOT_BATTERY app PRIVATE src/battery.c)
target_sources_ifdef(CONFIG_ZSPOT_UI app PRIVATE
  src/ui.c src/cover.c src/library.c src/webapi.c src/lyrics.c src/json_scan.c)
