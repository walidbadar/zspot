..
   Copyright (c) 2026 Muhammad Waleed Badar
   SPDX-License-Identifier: GPL-3.0-only

Troubleshooting
###############

The device does not appear in the Spotify app
*********************************************

- The phone or computer must be on the same network as the device. Guest
  networks and client isolation on the router prevent discovery.
- Check that the device advertises itself:

  .. code-block:: sh

     avahi-browse -rt _spotify-connect._tcp
     curl "http://<device>:8080/spotify_info?action=getInfo"

  ``activeUser`` in the answer is empty until a user picked the device.
- The desktop app relies on multicast mDNS answers. When only the phone finds
  the device, the multicast answers are probably lost on the way, often on the
  router. Opening Spotify on the phone makes the device show up on the desktop
  too, because the desktop app overhears the answers to the phone.
- On Android, Spotify needs the *Nearby devices* permission; on iOS, *Local
  Network*.

The device cannot join Wi-Fi
****************************

- Hold the Wi-Fi symbol for one second to open the settings. Stored networks
  are listed first; hold one for one second to forget it.
- At most two networks are stored (``CONFIG_WIFI_CREDENTIALS_MAX_ENTRIES``).
  Forget one before adding a third.
- Stored networks survive reflashing; ``west flash --erase`` removes them.

The device should play for another account
******************************************

The device stays signed in to the account that picked it last. After a
restart it signs in with the stored credentials and is not advertised on the
network, so another account cannot pick it. To make it forget the account:

- ``west flash --erase`` removes the stored credentials, together with the
  stored Wi-Fi networks. The device then waits to be picked in the Spotify
  app again.
- Building with ``-DCONFIG_ZSPOT_CREDENTIALS_STORED=n`` makes the device wait
  to be picked at every start, without storing anything.

The song shown after a restart is not the last one
**************************************************

The device stores the song only while no audio goes out, because writing to
the flash would interrupt it: when the song is paused or moved, when it was
just started, at the end of the queue and when another device takes over.
Songs that the queue moved on to by itself are not stored until then. Pause
before switching the device off to come back to the same song and position.

Play on the song shown after a restart answers "Cannot play the last song"
when Spotify turned the request down, for example because the song is no
longer available. Start something from the Spotify app, the search or Your
Library instead.

No sound
********

- Build with ``-DCONFIG_ZSPOT_PCM_TAP=y`` and listen to the copy of the audio
  (see :doc:`player`). If it sounds right, check the wiring and the DAC
  (see :doc:`hardware`).
- An I2S amplifier with its shutdown pin pulled low stays silent.
- On ``native_sim`` the log names the audio output at startup. When the ALSA
  device cannot be opened, the simulator prints a warning with the reason;
  pick another device with ``--i2s_tx_tx=alsa:<name>``.

Playing a podcast fails from Your Library or search
***************************************************

The on-device search and Your Library list songs and playlists only. Episodes
play when they are started from the Spotify app.

``native_sim`` aborts with ``terminate called``
***********************************************

Zephyr builds native targets with ``-fno-reorder-functions``. With GCC 16 this
breaks exception handling in functions that the compiler splits into hot and
cold parts, and a ``throw`` aborts the program. The library builds itself with
``-fno-reorder-blocks-and-partition`` on native targets to avoid it; projects
that copy the library's sources need the same option.

The player stays on "Signing in to Spotify"
*******************************************

The player verifies Spotify's servers against the root certificates in
``app/certs``. When a server has moved to another certificate authority, the
connection to it fails: with the log on (see below) as ``Connect to <host>:443
failed (errno 103)``. ``app/certs/README.rst`` shows how to find the root that
the server uses now and where to add it. Building with
``-DCONFIG_ZSPOT_TLS_SEC_TAG=-1`` turns the verification off.

The same error for the host of the covers or the lyrics only costs the cover
or the lyrics.

Seeing what the player does
***************************

The player is built without a log. ``app/debug.conf`` turns it on, for the
application and the library, at the cost of processor time:

.. code-block:: sh

   west build -p -b <board> zspot/app -- -DEXTRA_CONF_FILE=debug.conf

On the ESP32-S3 the log comes over the USB port (``west espressif monitor``),
on ``native_sim`` on the terminal. A step that fails for good while the
player starts is also named on the screen, with its error number.

Reading a crash on ``native_sim``
*********************************

.. code-block:: sh

   coredumpctl list
   coredumpctl debug <pid>

then ``bt`` in gdb for the backtrace of the crashing thread.
