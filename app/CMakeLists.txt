# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

# When built from another west workspace, point Zephyr at this module.
if(NOT DEFINED ZEPHYR_EXTRA_MODULES)
  set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

# The binding of the native_sim fuel gauge driver in drivers/, unless this
# Zephyr brings the driver and its binding itself.
if(NOT EXISTS "$ENV{ZEPHYR_BASE}/drivers/fuel_gauge/linux_fuel_gauge")
  list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_player)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED app PRIVATE src/wifi.c)
target_sources_ifdef(CONFIG_ZSPOT_PCM_TAP app PRIVATE src/pcm_tap.c)
target_sources_ifdef(CONFIG_ZSPOT_BATTERY app PRIVATE src/battery.c)
target_sources_ifdef(CONFIG_ZSPOT_UI app PRIVATE
  src/ui.c src/cover.c src/library.c src/webapi.c src/lyrics.c src/json_scan.c)
if(NOT "${CONFIG_ZSPOT_PCM_COMMAND}${CONFIG_ZSPOT_PCM_FILE}" STREQUAL "")
  target_sources(app PRIVATE src/pcm_file_sink.c)
  # Host side, built against the host libc in the runner context
  target_sources(native_simulator INTERFACE src/pcm_file_sink_bottom.c)
  target_compile_definitions(app PRIVATE ZSPOT_SAMPLE_HAVE_PCM_FILE=1)

  # The audio is piped into a player on the host: tell when it is missing.
  if(NOT "${CONFIG_ZSPOT_PCM_COMMAND}" STREQUAL "")
    string(REGEX MATCH "^[^ ]+" pcm_player "${CONFIG_ZSPOT_PCM_COMMAND}")
    find_program(PCM_PLAYER_${pcm_player} ${pcm_player})
    if(NOT PCM_PLAYER_${pcm_player})
      message(WARNING "\"${pcm_player}\" (CONFIG_ZSPOT_PCM_COMMAND) was not found "
                      "on this host: the simulator will run without sound.")
    endif()
  endif()
endif()
