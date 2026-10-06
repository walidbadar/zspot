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

No sound
********

- Build with ``-DCONFIG_ZSPOT_PCM_TAP=y`` and listen to the copy of the audio
  (see :doc:`player`). If it sounds right, check the wiring and the DAC
  (see :doc:`hardware`).
- An I2S amplifier with its shutdown pin pulled low stays silent.

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

Reading a crash on ``native_sim``
*********************************

.. code-block:: sh

   coredumpctl list
   coredumpctl debug <pid>

then ``bt`` in gdb for the backtrace of the crashing thread.
