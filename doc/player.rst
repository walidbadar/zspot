..
   Copyright (c) 2026 Muhammad Waleed Badar
   SPDX-License-Identifier: GPL-3.0-only

The player
##########

The application in ``app/`` turns a board with a display and an I2S DAC into
a Spotify Connect speaker with its own touch UI.

ESP32-S3
********

The supported board is the VIEWE UEDX32480035E-WB-A (ESP32-S3, 8 MB PSRAM,
480x320 touch display). It has no DAC on board; see :doc:`hardware` for the
wiring.

.. code-block:: sh

   west build -p -b uedx32480035e_wb_a/esp32s3/procpu zspot/app
   west flash

Using it
********

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Control
     - Action
   * - Wi-Fi symbol, held 1 second
     - Wi-Fi settings: pick a network and type the password. Stored networks
       are listed first; hold one for 1 second to forget it. The symbol is
       red while the network or the Spotify connection is down.
   * - Search bar
     - Search for a song or artist and play a hit
   * - List button
     - Your Library: Liked Songs and playlists
   * - Microphone button
     - Switch between cover and lyrics

Wi-Fi credentials are stored on the device. Once it is connected, select it
in the Spotify app; the layout adapts to the resolution and orientation of
the display.

The screen turns off after ``CONFIG_ZSPOT_SCREEN_TIMEOUT_SECONDS`` (60 by
default, 0 to keep it on) without a touch, together with the backlight when
the board has the devicetree alias ``zspot-backlight``. The next touch wakes
it and does nothing else; playback is not affected.

Lyrics come from `LRCLIB <https://lrclib.net>`_ and need no account. Lists
show the first 30 entries, and the fonts cover Latin text only.

A battery symbol appears next to the Wi-Fi symbol when the board has a fuel
gauge behind the devicetree alias ``fuel-gauge0``.

Search and Your Library
***********************

These use the Spotify Web API and need a one-time setup:

#. Create an application at https://developer.spotify.com/dashboard with the
   Web API enabled and the redirect URI ``http://127.0.0.1:8888/callback``.
#. Export its credentials as ``ZSPOT_WEB_CLIENT_ID`` and
   ``ZSPOT_WEB_CLIENT_SECRET``, e.g. from a private file that ``~/.bashrc``
   sources.
#. Run ``scripts/spotify_authorize.py --save <that file>`` and approve the
   access in the browser; it adds ``ZSPOT_WEB_REFRESH_TOKEN``.
#. Rebuild with ``west build -p`` from a shell that has the three variables.

.. warning::

   The values give access to the account: keep them out of version control
   and do not share the build directory.

Search covers songs only; podcasts and episodes are not searched.

native_sim
**********

The display is an SDL window with the mouse as the touch screen, and the
audio plays on the host through ``aplay``. It needs the SDL2 development
package and alsa-utils.

Create the TAP interface once per boot and give it internet access:

.. code-block:: sh

   sudo tools/net-tools/net-setup.sh start --config nat.conf

Then build and run:

.. code-block:: sh

   west build -p -b native_sim/native/64 zspot/app
   ./build/zephyr/zephyr.exe

The Spotify app finds devices through mDNS on its own network. To make the
simulator visible there:

.. code-block:: sh

   scripts/native_sim_lan_bridge.py 8080 192.0.2.1 &
   avahi-publish-service zspot _spotify-connect._tcp 8080 \
       VERSION=1.0 CPath=/spotify_info Stack=SP &

Checking the audio without a DAC
********************************

Build with ``-DCONFIG_ZSPOT_PCM_TAP=y`` and the player serves a copy of its
audio on TCP port 5000:

.. code-block:: sh

   nc zspot.local 5000 | aplay -f S16_LE -r 44100 -c 2 -B 1000000 -R 1000000

When the copy sounds right but the DAC does not, the fault is in the I2S
configuration, the wiring or the DAC.

Application options
*******************

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Kconfig
     - Purpose
   * - ``ZSPOT_CREDENTIALS_JSON``
     - Stored credentials; skips the zeroconf hand-over
   * - ``ZSPOT_SCREEN_TIMEOUT_SECONDS``
     - Idle time before the screen turns off (0: never)
   * - ``ZSPOT_BATTERY_POLL_SECONDS``
     - Interval between two fuel gauge readings
   * - ``ZSPOT_PCM_TAP`` / ``ZSPOT_PCM_TAP_PORT``
     - Copy of the audio over TCP
   * - ``ZSPOT_PCM_COMMAND`` / ``ZSPOT_PCM_FILE``
     - Host audio output on ``native_sim``
   * - ``ZSPOT_WEB_CLIENT_ID``, ``ZSPOT_WEB_CLIENT_SECRET``,
       ``ZSPOT_WEB_REFRESH_TOKEN``
     - Web API access for search and Your Library

The full list is in ``app/Kconfig``.
