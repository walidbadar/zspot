..
   Copyright (c) 2026 Muhammad Waleed Badar
   SPDX-License-Identifier: GPL-3.0-only

The library
###########

``lib/zspot`` is a Zephyr module with a plain C API. It can be used without
the player, for example in a headless speaker.

Enable ``CONFIG_ZSPOT=y`` with ``CONFIG_CPP=y`` and ``CONFIG_STD_CPP20=y``;
``app/prj.conf`` has a working network and TLS configuration.

Minimal example
***************

.. code-block:: c

   #include <zspot/zspot.h>
   #include <zspot/zspot_i2s_sink.h>

   static size_t on_pcm(const uint8_t *pcm, size_t len, void *user)
   {
       return zspot_i2s_sink_write(pcm, len, user);   /* 0 = try again later */
   }

   static void on_event(const struct zspot_event *ev, void *user)
   {
       if (ev->type == ZSPOT_EVENT_VOLUME) {
           zspot_i2s_sink_set_volume(ev->volume);
       }
   }

   struct zspot_config cfg = {
       .device_name = "Living room",
       .audio_format = ZSPOT_FORMAT_OGG_VORBIS_160,
   };
   zspot_init(&cfg);
   zspot_i2s_sink_init(DEVICE_DT_GET(DT_ALIAS(zspot_i2s)),
                       DEVICE_DT_GET_OR_NULL(DT_ALIAS(zspot_codec)), 44100, 2, 16);

   zspot_zeroconf_start();          /* advertise the device                         */
   zspot_zeroconf_wait(-1);         /* until the Spotify app hands over credentials */
   zspot_connect(on_event, on_pcm, NULL);

Callbacks run on the library's threads.

API overview
************

The reference is ``include/zspot/zspot.h`` and
``include/zspot/zspot_i2s_sink.h``.

Lifecycle and credentials
=========================

.. list-table::
   :widths: 45 55

   * - ``zspot_init()``
     - Initialises the library and generates the device identity
   * - ``zspot_credentials_load_json()`` / ``zspot_credentials_save_json()``
     - Restore or persist Spotify's reusable credentials, to skip the
       hand-over on the next boot
   * - ``zspot_credentials_set_user_pass()``
     - Username and password login (rejected for most accounts nowadays)
   * - ``zspot_credentials_available()`` / ``zspot_credentials_clear()``
     - Query or forget the current credentials

Zeroconf
========

.. list-table::
   :widths: 45 55

   * - ``zspot_zeroconf_start()``
     - Advertises the device with mDNS/DNS-SD and serves ``/spotify_info``
   * - ``zspot_zeroconf_wait()``
     - Blocks until the Spotify app delivered credentials
   * - ``zspot_zeroconf_stop()``
     - Stops the endpoint

Session and playback
====================

.. list-table::
   :widths: 45 55

   * - ``zspot_connect()`` / ``zspot_disconnect()``
     - Start or end the session; ``zspot_connect()`` blocks until it is up
   * - ``zspot_is_connected()``
     - False while the library reconnects
   * - ``zspot_set_pause()``, ``zspot_next()``, ``zspot_previous()``,
       ``zspot_seek()``, ``zspot_set_volume()``
     - Local playback control, reported to the Spotify app
   * - ``zspot_notify_audio_reached_playback()``,
       ``zspot_notify_audio_ended()``, ``zspot_update_position_ms()``
     - Keep Spotify's view of the playback in step with the output
   * - ``zspot_queue_size()``, ``zspot_queue_position()``,
       ``zspot_queue_get_track()``, ``zspot_queue_play()``
     - The play queue handed over by the controlling app

Utilities
=========

``zspot_http_get()`` and ``zspot_http_request()`` give the application the
library's HTTP(S) client, for cover art or the Spotify Web API.

Events
******

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Event
     - Meaning
   * - ``ZSPOT_EVENT_PLAY_PAUSE``
     - Playback paused or resumed (``paused``)
   * - ``ZSPOT_EVENT_VOLUME``
     - Volume changed, 0 to 65535 (``volume``)
   * - ``ZSPOT_EVENT_TRACK_INFO``
     - Metadata of the audible track (``track``, valid during the callback)
   * - ``ZSPOT_EVENT_TRACK_BEGIN``
     - The PCM that follows belongs to a new track
   * - ``ZSPOT_EVENT_PLAYBACK_START``, ``ZSPOT_EVENT_SEEK``
     - Playback starts at, or jumps to, ``position_ms``
   * - ``ZSPOT_EVENT_NEXT``, ``ZSPOT_EVENT_PREV``, ``ZSPOT_EVENT_FLUSH``
     - Drop buffered audio
   * - ``ZSPOT_EVENT_DEPLETED``
     - The queue finished
   * - ``ZSPOT_EVENT_QUEUE_CHANGED``
     - The controlling app replaced or edited the queue
   * - ``ZSPOT_EVENT_DISCONNECT``
     - Another device took over playback
   * - ``ZSPOT_EVENT_CONNECTION_LOST``, ``ZSPOT_EVENT_CONNECTION_RESTORED``
     - The connection to Spotify dropped or is back; the library reconnects
       on its own

PCM callback
************

The PCM callback receives 16-bit signed little-endian, interleaved stereo at
44.1 kHz and returns the number of bytes it accepted. Returning 0 makes the
player retry the same data shortly; that is how back-pressure and pausing
work.

Configuration
*************

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Kconfig
     - Purpose
   * - ``ZSPOT_DEVICE_NAME``
     - Name shown in the Spotify app (default ``ZSpot``)
   * - ``ZSPOT_THREAD_PRIORITY``
     - Base priority of the protocol threads
   * - ``ZSPOT_MAIN_STACK_SIZE``, ``ZSPOT_MERCURY_STACK_SIZE``,
       ``ZSPOT_TRACKQUEUE_STACK_SIZE``, ``ZSPOT_PLAYER_STACK_SIZE``
     - Stack sizes of the protocol threads
   * - ``ZSPOT_EXTERNAL_HEAP``, ``ZSPOT_STACKS_EXTERNAL``
     - Large buffers and thread stacks in external memory (PSRAM)
   * - ``ZSPOT_EXTERNAL_TLS_HEAP_SIZE``
     - Mbed TLS heap placed in external memory
   * - ``ZSPOT_TLS_SEC_TAG``
     - CA credential tag for server verification (-1: no verification)
   * - ``ZSPOT_HTTP_TIMEOUT_MS``, ``ZSPOT_HTTP_RECV_BUF_SIZE``
     - HTTP client settings
   * - ``ZSPOT_MDNS``, ``ZSPOT_ZEROCONF``, ``ZSPOT_ZEROCONF_PORT``
     - Discovery and the credential hand-over endpoint
   * - ``ZSPOT_I2S_SINK``, ``ZSPOT_I2S_BLOCK_SIZE``,
       ``ZSPOT_I2S_BLOCK_COUNT``, ``ZSPOT_I2S_BUFFER_MS``
     - Ready-made PCM sink using the I2S driver

The remaining options are described in ``lib/zspot/Kconfig``.
