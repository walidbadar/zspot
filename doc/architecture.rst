..
   Copyright (c) 2026 Muhammad Waleed Badar
   SPDX-License-Identifier: GPL-3.0-only

Architecture
############

Layers
******

.. code-block:: none

   +--------------------------------------------------------------+
   |  app/        LVGL UI, Wi-Fi, library view, lyrics, battery   |
   +--------------------------------------------------------------+
   |  include/zspot/zspot.h               plain C API             |
   +--------------------------------------------------------------+
   |  lib/zspot/src/core   Spotify protocol (derived from cspot)  |
   |  lib/zspot/src/port   threads, sync, sockets, TLS, crypto,   |
   |                       mDNS, zeroconf, JSON, protobuf         |
   |  lib/zspot/src/audio  I2S sink    third_party/tremor  Vorbis |
   +--------------------------------------------------------------+
   |  Zephyr: kernel, networking, Mbed TLS / PSA, nanopb, LVGL,   |
   |          I2S, display and input drivers                      |
   +--------------------------------------------------------------+

The protocol core is C++20. Zephyr's SDK ships libstdc++ without thread
support, so ``std::mutex``, ``std::condition_variable`` and ``std::thread``
are replaced by small classes on Zephyr kernel objects in
``lib/zspot/src/port`` (``sync.h``, ``thread.h``, ``queue.h``).

Threads
*******

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Thread
     - Role
   * - ``zspot_main``
     - Connects and authenticates, then dispatches Mercury packets
   * - ``zspot_mercury``
     - Receives packets from the Spotify access point, answers pings and
       reconnects when the connection is lost
   * - ``zspot_queue``
     - Loads track metadata, audio keys and CDN URLs ahead of playback
   * - ``zspot_player``
     - Streams and decodes the audio and hands PCM to the application
   * - ``zspot_zeroconf``
     - Serves the credential hand-over endpoint
   * - LVGL work queue
     - Draws the UI and runs the touch handlers of the player

Connection handling
*******************

- The session drops the connection when a network interface goes down, or at
  the latest when Spotify's ping has been missing for about two minutes.
- Reconnection retries with a backoff from 1 to 30 seconds. An interface or
  an IPv4 address that appears starts the next attempt at once.
- Requests issued while the connection is down fail on the dispatch thread
  instead of using a connection that is being replaced.
- A track whose metadata or audio key request was cut off is loaded again
  once the connection is back, instead of being skipped.
- The application receives ``ZSPOT_EVENT_CONNECTION_LOST`` and
  ``ZSPOT_EVENT_CONNECTION_RESTORED``; the player turns its network symbol red
  in between.

Memory
******

On the ESP32-S3 the large buffers live in PSRAM: the Mbed TLS heap, the
protocol thread stacks, the decoder buffers and the LVGL memory pool, which
also holds the cover art and the Web API responses. The display's render
buffer stays in internal RAM because the SPI DMA cannot read from PSRAM.

Discovery and hand-over
***********************

#. The device advertises ``_spotify-connect._tcp`` with mDNS and answers the
   Spotify app's ``getInfo`` request on ``/spotify_info``.
#. When a user picks the device, the app posts encrypted credentials to the
   same endpoint (``addUser``).
#. The library logs in with them and keeps Spotify's reusable credentials,
   which the application can store to skip the hand-over on the next boot.
