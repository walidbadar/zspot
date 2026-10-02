# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
#
# cspot-zephyr: Spotify Connect receiver library for Zephyr RTOS.

if(CONFIG_CSPOT)

zephyr_library_named(cspot)

# Public C API
zephyr_include_directories(include)

# Private headers (protocol core + Zephyr port) and the Tremor decoder
zephyr_library_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tremor
)

# Spotify protocol implementation (derived from cspot)
zephyr_library_sources(
  src/core/AccessKeyFetcher.cpp
  src/core/ApResolve.cpp
  src/core/AuthChallenges.cpp
  src/core/CDNAudioFile.cpp
  src/core/LoginBlob.cpp
  src/core/MercurySession.cpp
  src/core/PlainConnection.cpp
  src/core/PlaybackState.cpp
  src/core/Session.cpp
  src/core/Shannon.cpp
  src/core/ShannonConnection.cpp
  src/core/SpircHandler.cpp
  src/core/TimeProvider.cpp
  src/core/TrackPlayer.cpp
  src/core/TrackQueue.cpp
  src/core/TrackReference.cpp
  src/core/Utils.cpp
)

# Zephyr port layer
zephyr_library_sources(
  src/port/crypto.cpp
  src/port/http.cpp
  src/port/json.cpp
  src/port/json_glue.c
  src/port/log.cpp
  src/port/mem.c
  src/port/mod_exp.cpp
  src/port/protobuf.cpp
  src/port/thread.cpp
)
zephyr_library_sources_ifdef(CONFIG_CSPOT_EXTERNAL_HEAP src/port/new.cpp)
zephyr_library_sources_ifdef(CONFIG_CSPOT_MDNS src/port/mdns.c)
zephyr_library_sources_ifdef(CONFIG_CSPOT_ZEROCONF src/port/zeroconf.cpp)
if(CONFIG_CSPOT_ZEROCONF)
  # HTTP_SERVICE_DEFINE() needs a dedicated iterable section per service
  zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_SOURCE_DIR}/src/port/zeroconf_sections.ld)
  zephyr_linker_section(NAME http_resource_desc_cspot_zeroconf_service KVMA RAM_REGION
    GROUP RODATA_REGION SUBALIGN Z_LINK_ITERABLE_SUBALIGN)
endif()
zephyr_library_sources_ifdef(CONFIG_CSPOT_I2S_SINK src/audio/i2s_sink.c)

# C API facade
zephyr_library_sources(src/api/cspot_api.cpp)

# Tremor (integer Ogg Vorbis decoder, BSD licence)
file(GLOB CSPOT_TREMOR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tremor/*.c)
zephyr_library_sources(${CSPOT_TREMOR_SOURCES})
set_source_files_properties(${CSPOT_TREMOR_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-sign-compare;-Wno-maybe-uninitialized;-Dalloca=__builtin_alloca;$<$<BOOL:${CONFIG_CSPOT_EXTERNAL_HEAP}>:-DCSPOT_TREMOR_ALLOC>"
)

# Protocol buffers, generated at build time with Zephyr's nanopb module
list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)
file(GLOB CSPOT_PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/*.proto)
zephyr_nanopb_sources(cspot ${CSPOT_PROTO_FILES})

# Spotify's metadata messages exceed nanopb's 16-bit field limits. This has to
# apply to the nanopb runtime as well, hence the global definition.
zephyr_compile_definitions(PB_FIELD_32BIT)

# PSA Crypto headers
if(TARGET mbedtls_iface)
  zephyr_library_link_libraries(mbedtls_iface)
elseif(TARGET mbedtls)
  zephyr_library_link_libraries(mbedtls)
endif()

endif() # CONFIG_CSPOT
