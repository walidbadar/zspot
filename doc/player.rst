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
   * - Repeat button
     - Cycle through off, repeat the queue and repeat the track (shown with
       a "1"). The Spotify app shows either repeat as on, and its repeat
       toggle turns the button on and off. Skipping ahead ends repeating the
       track, as in the app.

Wi-Fi credentials are stored on the device. Once it is connected, select it
in the Spotify app; the layout adapts to the resolution and orientation of
the display.

The device remembers the account that picked it and signs in on its own after
a restart, so it only has to be picked once
(``CONFIG_ZSPOT_CREDENTIALS_STORED``). When Spotify no longer accepts the
stored credentials, they are removed and the device waits to be picked again.

Another account takes the device over by picking it in its Spotify app, on
the same network: the device signs out, signs in with that account and
remembers it instead. Should Spotify reject the new account, the device goes
back to the one before.

When the last track of the queue has been played, the device reports the end
to Spotify and shows that track paused at its start.

After a restart the Now Playing screen shows the last song, paused where it
was paused, instead of the notice to pick the device
(``CONFIG_ZSPOT_LAST_TRACK``). Play starts it on this device and goes on with
its playlist or album when it was played from one; the progress slider picks
where it starts. This uses the Web API set up for
:ref:`search and Your Library <search-and-your-library>`; without that the
notice is shown as before.

Writing to the flash interrupts the audio, so the song is stored while none
goes out: when it is paused or moved, when it was just started, at the end
of the queue and when another device takes over. A song that the queue moved
on to without a break is stored at the next such moment. When the power is
cut in the middle of a queue, the device therefore comes back with the song
stored last, usually the one the queue was started with.

The screen turns off after ``CONFIG_ZSPOT_SCREEN_TIMEOUT_SECONDS`` (60 by
default, 0 to keep it on) without a touch, together with the backlight when
the board has the devicetree alias ``zspot-backlight``. The next touch wakes
it and does nothing else; playback is not affected.

Lyrics come from `LRCLIB <https://lrclib.net>`_ and need no account. Lists
show the first 30 entries, and the fonts cover Latin text only.

A battery symbol appears next to the Wi-Fi symbol when the board has a fuel
gauge behind the devicetree alias ``fuel-gauge0``.

.. _search-and-your-library:

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

The display is an SDL window with the mouse as the touch screen. The audio
goes through Zephyr's native_sim I2S driver to the host's default ALSA device
(``CONFIG_I2S_NATIVE_SIM_ALSA``). It needs the SDL2 and ALSA development
packages (``SDL2-devel`` and ``alsa-lib-devel`` on Fedora, ``libsdl2-dev`` and
``libasound2-dev`` on Debian and Ubuntu). Another ALSA device or a file is
picked with ``--i2s_tx_tx=<path>``, e.g. ``--i2s_tx_tx=alsa:hw:0,0`` or
``--i2s_tx_tx=out.pcm`` for raw 16-bit little-endian stereo PCM at 44.1 kHz.

Create the TAP interface once per boot and give it internet access:

.. code-block:: sh

   sudo tools/net-tools/net-setup.sh start --config nat.conf

Then build and run:

.. code-block:: sh

   west build -p -b native_sim/native/64 zspot/app
   ./build/zephyr/zephyr.exe

The simulator has no settings storage, so it does not remember the account
and waits to be picked in the Spotify app at every start. To skip that, build
once with ``-DCONFIG_LOG=y``, copy the reusable credentials that the log
prints after signing in, and set ``CONFIG_ZSPOT_CREDENTIALS_JSON`` to them.

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

Without the screen
******************

``CONFIG_ZSPOT_UI=n`` builds the player for a device without a display. Only
what draws on the screen or feeds it goes with that option: the Now Playing
screen, the covers, Your Library, the search, the lyrics, the battery
indicator and the Wi-Fi settings. The rest is the same player: Spotify
Connect, the stored credentials, the last song and the Web API.

The last song is kept as with the screen. ``track_pause(false)`` starts it
(``app/src/track.h``), which is what the Play button calls; a device with
keys of its own calls it from their handler. ``webapi_play()`` starts any
track on the device, and ``webapi_submit()`` runs other Web API requests
(``app/src/webapi.h``).

Application options
*******************

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Kconfig
     - Purpose
   * - ``ZSPOT_CREDENTIALS_STORED``
     - Keep the Spotify credentials across restarts (on by default where the
       settings subsystem is enabled)
   * - ``ZSPOT_LAST_TRACK``
     - Keep the last song across restarts, to show it and start it with
       Play (on by default with ``ZSPOT_CREDENTIALS_STORED``, with or
       without the screen)
   * - ``ZSPOT_CREDENTIALS_JSON``
     - Credentials given to the build; skips the zeroconf hand-over and
       takes precedence over stored ones
   * - ``ZSPOT_AUDIO_QUALITY_96``, ``ZSPOT_AUDIO_QUALITY_160``,
       ``ZSPOT_AUDIO_QUALITY_320``
     - Bit rate of the stream that is asked for (160 kbit/s by default)
   * - ``ZSPOT_SCREEN_TIMEOUT_SECONDS``
     - Idle time before the screen turns off (0: never)
   * - ``ZSPOT_BATTERY_POLL_SECONDS``
     - Interval between two fuel gauge readings
   * - ``ZSPOT_PCM_TAP`` / ``ZSPOT_PCM_TAP_PORT``
     - Copy of the audio over TCP
   * - ``ZSPOT_WEB_CLIENT_ID``, ``ZSPOT_WEB_CLIENT_SECRET``,
       ``ZSPOT_WEB_REFRESH_TOKEN``
     - Web API access for search and Your Library

The full list is in ``app/Kconfig``.
