# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)

# When built from another west workspace, point Zephyr at this module.
if(NOT DEFINED ZEPHYR_EXTRA_MODULES)
  set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zspot_player)

# --- Sources --------------------------------------------------------------

target_sources(app PRIVATE
  src/main.c
  src/certs.c
  src/webapi.c
  src/json_scan.c
)

target_sources_ifdef(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED app PRIVATE src/wifi.c)
target_sources_ifdef(CONFIG_ZSPOT_CREDENTIALS_STORED        app PRIVATE src/credentials.c)
target_sources_ifdef(CONFIG_SETTINGS                        app PRIVATE src/track.c)
target_sources_ifdef(CONFIG_ZSPOT_PCM_TAP                   app PRIVATE src/pcm_tap.c)
target_sources_ifdef(CONFIG_ZSPOT_BATTERY                   app PRIVATE src/battery.c)

target_sources_ifdef(CONFIG_ZSPOT_UI app PRIVATE
  src/ui.c
  src/cover.c
  src/library.c
  src/lyrics.c
)

# --- TLS ------------------------------------------------------------------

# Mbed TLS is built with zspot_mbedtls_config.h (CONFIG_MBEDTLS_USER_CONFIG_FILE)
zephyr_include_directories(mbedtls)

# The root certificates that the servers are verified against
set(root_certs
  digicert_global_root_g2
  digicert_global_root_g3
  globalsign_root_r3
  gts_root_r4
  starfield_root_g2
)
foreach(cert ${root_certs})
  generate_inc_file_for_target(app
    certs/${cert}.der
    ${ZEPHYR_BINARY_DIR}/include/generated/certs/${cert}.der.inc
  )
endforeach()

# --- native_sim: west build -t run_tap ------------------------------------

if(CONFIG_ETH_NATIVE_TAP)
  if(DEFINED ENV{ZEPHYR_NET_TOOLS})
    set(net_tools $ENV{ZEPHYR_NET_TOOLS})
  else()
    set(net_tools ${ZEPHYR_BASE}/../tools/net-tools)
  endif()

  set(run_script ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/native_sim_run.py)
  set(player     ${APPLICATION_BINARY_DIR}/zephyr/${KERNEL_EXE_NAME})

  add_custom_target(run_tap
    COMMAND ${PYTHON_EXECUTABLE} ${run_script} --net-tools ${net_tools} ${player}
    DEPENDS
      ${logical_target_for_zephyr_elf}
      $<$<TARGET_EXISTS:native_runner_executable>:native_runner_executable>
    WORKING_DIRECTORY ${APPLICATION_BINARY_DIR}
    USES_TERMINAL
  )
endif()
