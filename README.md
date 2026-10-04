# zspot

<p align="center">
  <img src="doc/images/zspot-banner.svg" alt="zspot: Zephyr RTOS and Spotify Connect" width="720">
</p>

A Spotify Connect player for devices running
[Zephyr RTOS](https://zephyrproject.org), with the Spotify protocol packaged
as a Zephyr module.

<p align="center">
  <img src="doc/images/zspot-ui.png" alt="The player on native_sim: Now Playing, lyrics, a playlist from Your Library and the Wi-Fi password entry" width="840">
</p>
<p align="center"><em>Now Playing, lyrics, a playlist from Your Library and the Wi-Fi password entry, on native_sim with demo data.</em></p>

- Shows up as a device in the Spotify app and plays through an I2S DAC.
- Now Playing screen with cover art, progress, seeking, transport and volume
  controls.
- Search, Your Library (Liked Songs and playlists) and synced lyrics on the
  device.
- Wi-Fi setup on the screen.
- Runs on the ESP32-S3 (VIEWE UEDX32480035E-WB-A) and on `native_sim`.
- The protocol library (`lib/zspot`) has a plain C API and can be used
  without the player.

A Spotify Premium account is required.

## Requirements

- Zephyr 4.5 and a Zephyr SDK with a full libstdc++ (C++20).
- `protoc` on the `PATH` at build time. Without a system package:

  ```sh
  python3 -m venv ~/.venvs/protoc && ~/.venvs/protoc/bin/pip install grpcio-tools
  printf '#!/bin/sh\nexec "$(dirname "$0")/python" -m grpc_tools.protoc "$@"\n' > ~/.venvs/protoc/bin/protoc
  chmod +x ~/.venvs/protoc/bin/protoc
  export PATH=~/.venvs/protoc/bin:$PATH
  ```

- A board with IPv4 networking and about 300 KB of heap for the protocol.
  With PSRAM (`CONFIG_ZSPOT_EXTERNAL_HEAP`) most of it comes from external
  memory.

## Getting started

The repository is a west manifest repository and a Zephyr module. Either
create a workspace from it:

```sh
west init -m https://github.com/walidbadar/zspot --mr main zspot-workspace
cd zspot-workspace && west update
```

or link it into an existing workspace:

```sh
cd ~/zephyrproject
ln -s /path/to/zspot zspot
west config manifest.path zspot
west update zephyr
```

To use only the library from another project, add the repository to that
project's west manifest or pass `-DZEPHYR_EXTRA_MODULES=/path/to/zspot`.

## The player

### ESP32-S3

The supported board is the VIEWE UEDX32480035E-WB-A (ESP32-S3, 8 MB PSRAM,
480x320 touch display). It has no DAC on board; connect an I2S DAC to
BCK = GPIO5, WS = GPIO6 and DATA = GPIO7.

```sh
west build -p -b uedx32480035e_wb_a/esp32s3/procpu zspot/app
west flash
```

### Using it

| Control                         | Action                                         |
|---------------------------------|------------------------------------------------|
| Wi-Fi symbol, held 3 seconds    | Wi-Fi settings: pick a network, type the password. The symbol is red while disconnected. |
| Search bar                      | Search for a song or artist and play a hit     |
| List button                     | Your Library: Liked Songs and playlists        |
| Microphone button               | Switch between cover and lyrics                |

Wi-Fi credentials are stored on the device. Once it is connected, select it
in the Spotify app; the layout adapts to the resolution and orientation of
the display.

Lyrics come from [LRCLIB](https://lrclib.net) and need no account. Lists
show the first 30 entries, and the fonts cover Latin text only.

### Search and Your Library

These use the Spotify Web API and need a one-time setup:

1. Create an application at <https://developer.spotify.com/dashboard> with
   the Web API enabled and the redirect URI `http://127.0.0.1:8888/callback`.
2. Export its credentials as `ZSPOT_WEB_CLIENT_ID` and
   `ZSPOT_WEB_CLIENT_SECRET`, e.g. from a private file that `~/.bashrc`
   sources.
3. Run `scripts/spotify_authorize.py --save <that file>` and approve the
   access in the browser; it adds `ZSPOT_WEB_REFRESH_TOKEN`.
4. Rebuild with `west build -p` from a shell that has the three variables.

The values give access to the account: keep them out of version control and
do not share the build directory.

### native_sim

The display is an SDL window with the mouse as the touch screen, and the
audio plays on the host through `aplay`. It needs the SDL2 development
package and alsa-utils.

Create the TAP interface once per boot and give it internet access:

```sh
sudo tools/net-tools/net-setup.sh start --config nat.conf
```

Then build and run:

```sh
west build -p -b native_sim/native/64 zspot/app
./build/zephyr/zephyr.exe
```

The Spotify app finds devices through mDNS on its own network. To make the
simulator visible there:

```sh
scripts/native_sim_lan_bridge.py 8080 192.0.2.1 &
avahi-publish-service zspot _spotify-connect._tcp 8080 \
    VERSION=1.0 CPath=/spotify_info Stack=SP &
```

### Checking the audio without a DAC

Build with `-DCONFIG_ZSPOT_PCM_TAP=y` and the player serves a copy of its
audio on TCP port 5000:

```sh
nc zspot.local 5000 | aplay -f S16_LE -r 44100 -c 2 -B 1000000 -R 1000000
```

## The library

Enable `CONFIG_ZSPOT=y` with `CONFIG_CPP=y` and `CONFIG_STD_CPP20=y`;
`app/prj.conf` has a working network and TLS configuration.

```c
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

struct zspot_config cfg = { .device_name = "Living room", .audio_format = ZSPOT_FORMAT_OGG_VORBIS_160 };
zspot_init(&cfg);
zspot_i2s_sink_init(DEVICE_DT_GET(DT_ALIAS(zspot_i2s)), 44100, 2, 16);

zspot_zeroconf_start();          /* advertise the device                         */
zspot_zeroconf_wait(-1);         /* until the Spotify app hands over credentials */
zspot_connect(on_event, on_pcm, NULL);
```

Credentials can be saved and restored as JSON to skip the hand-over on the
next boot. Playback control, track metadata, the play queue and an HTTP(S)
client are documented in [`include/zspot/zspot.h`](include/zspot/zspot.h).
Callbacks run on the library's threads.

| Kconfig                          | Purpose                                        |
|----------------------------------|------------------------------------------------|
| `ZSPOT_DEVICE_NAME`              | Name shown in the Spotify app                  |
| `ZSPOT_I2S_SINK`                 | Ready-made PCM sink using the I2S driver       |
| `ZSPOT_I2S_BUFFER_MS`            | Audio buffered ahead of the I2S output         |
| `ZSPOT_TLS_SEC_TAG`              | CA credential tag for server verification (-1: no verification) |
| `ZSPOT_EXTERNAL_HEAP`            | Large buffers from external memory (PSRAM)     |

The remaining options are described in `lib/zspot/Kconfig`.

The Spotify protocol core is derived from
[cspot](https://github.com/feelfreelinux/cspot). Threads, sockets, TLS,
crypto (PSA), mDNS, JSON, protocol buffers (nanopb) and audio output come
from Zephyr; the only vendored code is the Tremor Vorbis decoder.

## Tests

```sh
west twister -T zspot/app -T zspot/tests --integration
```

## Licence

GPL-3.0, like cspot. Tremor is BSD-licensed (see `third_party/tremor/COPYING`).

Zephyr and the Zephyr logo are trademarks of The Linux Foundation; the logo
comes from the Zephyr documentation assets (Apache-2.0). Spotify and the
Spotify icon are trademarks of Spotify AB and identify the Spotify Connect
protocol only; this project is not affiliated with or endorsed by Spotify.
