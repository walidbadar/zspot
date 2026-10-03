# zspot

<p align="center">
  <img src="doc/images/zspot-banner.svg" alt="zspot: Zephyr RTOS and Spotify Connect" width="720">
</p>

Spotify Connect player for [Zephyr RTOS](https://zephyrproject.org): a
touchscreen player application and the Spotify Connect receiver library it is
built on, packaged as a Zephyr module.

<p align="center">
  <img src="doc/images/zspot-ui.png" alt="The player on native_sim: Now Playing, lyrics, a playlist from Your Library and the Wi-Fi password entry" width="840">
</p>
<p align="center"><em>Now Playing, lyrics, a playlist from Your Library and the Wi-Fi password entry, on native_sim with demo data.</em></p>

**The player** (`app/`) turns a board with a display into a Spotify Connect
speaker with a screen of its own:

- Shows up as a device in the Spotify app and plays through an I2S DAC.
- Now Playing screen with cover art, progress, seeking, transport and volume
  controls.
- Search: type a song or artist on the screen and play a hit.
- Your Library: browse Liked Songs and playlists on the device and start a
  track from there.
- Lyrics that follow the music.
- Wi-Fi setup on the screen: pick a network, type the password.
- Runs on the ESP32-S3 (VIEWE UEDX32480035E-WB-A) and on `native_sim`, where
  the screen is a window and the audio plays on the host.

**The library** (`lib/zspot`, API in `include/zspot/zspot.h`) is the Spotify
Connect receiver on its own, with a plain C API, for applications that bring
their own user interface or none at all.

Spotify Premium is required; see [Player application](#player-application)
for the hardware and the setup, and [API](#api) for the library.

## How it is built

Naming: everything Zephyr-facing is `zspot` (module and library name,
`CONFIG_ZSPOT_*`, the `zspot_*` C API, the `zspot` log module, the port
layer namespace). The protocol core keeps its upstream `cspot` namespace and
`CSPOT_LOG()` call style.

The Spotify protocol implementation (access point handshake, Shannon
transport, Mercury, Spirc remote control, track queue, CDN streaming and
decryption) is derived from [cspot](https://github.com/feelfreelinux/cspot) and
stays internal to the library. Everything that is not Spotify-specific comes
from Zephyr:

| Concern            | Provided by                                              |
|--------------------|----------------------------------------------------------|
| Threads, sync      | Zephyr kernel (`k_thread`, `k_mutex`, `k_condvar`, `k_sem`) |
| Sockets, TLS       | Zephyr BSD sockets and TLS sockets (Mbed TLS)             |
| HTTP client        | `zephyr/net/http/client.h`                               |
| Zeroconf endpoint  | Minimal HTTP/1.1 responder on Zephyr sockets (see below) |
| Discovery          | Zephyr mDNS responder with DNS-SD                        |
| Crypto             | PSA Crypto API (SHA-1, HMAC, AES-CTR/ECB, RNG), `sys/base64.h` |
| JSON               | `zephyr/data/json.h`                                     |
| Protocol buffers   | Zephyr `nanopb` module (generated at build time)         |
| Logging            | Zephyr logging, module `zspot`                           |
| Audio output       | Zephyr I2S driver API (optional helper sink)             |
| Display (player)   | Zephyr `lvgl` module, display and input drivers          |

The only vendored third-party code is Tremor, the integer Ogg Vorbis decoder
(`third_party/tremor`, BSD licence), because Zephyr has no Vorbis decoder.

Only Spotify Premium accounts can use Spotify Connect; with a free account the app lists the device but refuses to hand over credentials.

Spotify clients only accept zeroconf replies framed with `Content-Length`
and a full status line. Zephyr's HTTP server sends dynamic resources with
chunked transfer encoding, which made the device invisible in the Spotify
app, so the `/spotify_info` endpoint is served by a small responder built
directly on Zephyr sockets instead.

## Requirements

- Zephyr 4.5 (uses Mbed TLS 4 / TF-PSA-Crypto, the 8-argument
  `HTTP_SERVICE_DEFINE` and 64-bit `net_mgmt` events).
- Zephyr SDK with a full libstdc++ (C++20) for the target; the library selects
  `CONFIG_REQUIRES_FULL_LIBCPP` and `CONFIG_CPP_EXCEPTIONS`.
- `protoc` on the `PATH` for the nanopb generator
  (`pip install grpcio-tools` and a small `protoc` wrapper, or a system
  package).
- A board with IPv4 networking and, for the player, an I2S DAC. The
  protocol needs roughly 300 KB of heap at runtime (TLS buffers, decoder,
  track buffers). With `CONFIG_ZSPOT_EXTERNAL_HEAP` (default when
  `CONFIG_SHARED_MULTI_HEAP` is available, e.g. `CONFIG_ESP_SPIRAM=y`) the
  C++ free store, the decoder buffers, the protocol thread stacks and the
  Mbed TLS heap are taken from external memory, which keeps the internal
  RAM footprint of the player small enough for the ESP32 family.

A `protoc` is needed only at build time. Without a system package:

```sh
python3 -m venv ~/.venvs/protoc && ~/.venvs/protoc/bin/pip install grpcio-tools
printf '#!/bin/sh\nexec "$(dirname "$0")/python" -m grpc_tools.protoc "$@"\n' > ~/.venvs/protoc/bin/protoc
chmod +x ~/.venvs/protoc/bin/protoc
export PATH=~/.venvs/protoc/bin:$PATH
```

## Getting started

The repository follows the layout of Zephyr's
[example-application](https://github.com/zephyrproject-rtos/example-application):
it is a west manifest repository and a Zephyr module at the same time. Its
`west.yml` pins Zephyr and the few modules the library needs.

### Shared Zephyr workspace (recommended)

Reuse an existing workspace instead of cloning Zephyr per project: link the
repository into the workspace and make it the manifest repository.

```sh
cd ~/workspace/zephyrproject                      # existing workspace
ln -s ~/workspace/firmware/zspot zspot     # repository checked out elsewhere
west config manifest.path zspot
west config manifest.file west.yml
west update zephyr                                # records manifest-rev, keeps the checkout
west build -b uedx32480035e_wb_a/esp32s3/procpu zspot/app
west twister -T zspot/tests -p native_sim/native/64
```

`west.yml` pins `zephyr` and `hal_espressif` to the revisions this project was
verified with, so `west update` does not move them. To switch the workspace
back to Zephyr's own manifest run `west config manifest.path zephyr`.

### Standalone workspace

```sh
west init -m https://github.com/<you>/zspot --mr main zspot-workspace
cd zspot-workspace && west update
west build -b uedx32480035e_wb_a/esp32s3/procpu zspot/app
```

### From another manifest

Add the repository as a project of that manifest:

```yaml
manifest:
  projects:
    - name: zspot
      url: https://github.com/<you>/zspot
      revision: main
      path: modules/lib/zspot
```

Without west, pass `-DZEPHYR_EXTRA_MODULES=/path/to/zspot` to the
build (`app/CMakeLists.txt` does this itself when the variable is unset).

## Configuration

Enable `CONFIG_ZSPOT=y` together with `CONFIG_CPP=y` and
`CONFIG_STD_CPP20=y`. Useful options:

| Kconfig                          | Purpose                                        |
|----------------------------------|------------------------------------------------|
| `ZSPOT_DEVICE_NAME`              | Name shown in the Spotify app                  |
| `ZSPOT_ZEROCONF` / `_PORT`       | Zeroconf credential hand-over endpoint         |
| `ZSPOT_MDNS`                     | DNS-SD advertisement (`_spotify-connect._tcp`) |
| `ZSPOT_I2S_SINK`                 | Ready-made PCM sink using the I2S driver       |
| `ZSPOT_I2S_BUFFER_MS`            | PCM buffered ahead of the I2S output, so it keeps running while the player fetches |
| `ZSPOT_TLS_SEC_TAG`              | CA credential tag for server verification (-1: no verification) |
| `ZSPOT_EXTERNAL_HEAP`            | Large buffers from the shared multi heap (PSRAM) |
| `ZSPOT_STACKS_EXTERNAL`          | Protocol thread stacks in external memory      |
| `ZSPOT_EXTERNAL_TLS_HEAP_SIZE`   | Mbed TLS heap relocated to external memory     |
| `ZSPOT_*_STACK_SIZE`             | Stacks of the protocol threads                 |
| `ZSPOT_LOG_LEVEL_*`              | Log level of the `zspot` module                |

TLS towards Spotify needs TLS 1.2 with ECDHE-RSA and AES-GCM; see
`app/prj.conf` for a working Mbed TLS / PSA configuration.

## API

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

zspot_zeroconf_start();          /* advertise + serve /spotify_info          */
zspot_zeroconf_wait(-1);         /* until the Spotify app hands over credentials */
zspot_connect(on_event, on_pcm, NULL);

char json[1024];
zspot_credentials_save_json(json, sizeof(json));  /* persist for next boot  */
```

On the next boot `zspot_credentials_load_json()` skips the zeroconf step.
Playback control (`zspot_set_pause`, `zspot_next`, `zspot_previous`,
`zspot_seek`, `zspot_set_volume`) and position feedback
(`zspot_update_position_ms`) are available for local buttons and displays.

Track metadata arrives with `ZSPOT_EVENT_TRACK_INFO` (title, artist, album,
cover URL, duration). The library raises `ZSPOT_EVENT_TRACK_BEGIN` when the
PCM of a new track starts; answer it with
`zspot_notify_audio_reached_playback()` once that audio is audible, which
advances the queue and triggers the track info event. `zspot_http_get()`
downloads a resource such as the cover art with the library's HTTP(S) client.

The play queue handed over by the controlling app can be inspected and
used: `zspot_queue_size()` and `zspot_queue_position()` describe it,
`zspot_queue_get_track()` looks up the metadata of one entry (the queue only
holds references) and `zspot_queue_play()` jumps to an entry.
`ZSPOT_EVENT_QUEUE_CHANGED` reports that the app replaced or edited it.

`zspot_http_request()` performs an arbitrary HTTP(S) request with the
library's client, for example against the Spotify Web API. The session's own
access token is of no use there: it belongs to Spotify's client and the
public Web API answers it with HTTP 429, so the application has to bring an
OAuth token of its own (see the player application).

Callbacks run on the library's threads.

## Player application

`app/` is the player application. It joins Wi-Fi, advertises the device, waits for the Spotify
app, plays through I2S and shows a "Now Playing" screen on the display. The supported board is the VIEWE
UEDX32480035E-WB-A (`uedx32480035e_wb_a/esp32s3/procpu`, ESP32-S3 with 8 MB
PSRAM). It has no audio DAC on board, so connect an external I2S DAC to
BCK = GPIO5, WS/LRCK = GPIO6 and DATA = GPIO7. The console is on the native
USB serial port.

```sh
west build -p -b uedx32480035e_wb_a/esp32s3/procpu app
west flash
```

Wi-Fi credentials are handled by Zephyr's `wifi_credentials` library with the
settings/NVS backend. Add them once in the Wi-Fi settings of the screen (see
below); they persist across reboots and the player connects with
`NET_REQUEST_WIFI_CONNECT_STORED`, retrying whenever the link drops.

Stored Spotify credentials can be passed with
`-DCONFIG_ZSPOT_CREDENTIALS_JSON='"..."'` (the JSON printed by the
player after its first connection) to skip the zeroconf hand-over.

### Now Playing screen

With `CONFIG_ZSPOT_UI=y` (the default in `app/prj.conf`) the player
drives the chosen display with LVGL. The layout follows the resolution of
the display: it is drawn for 320x480, scales up with larger fonts on bigger
displays, shrinks the cover on smaller ones and puts the cover beside the
controls in landscape. It
shows the cover art, title, artist and progress of the current track, with
touch controls for play/pause, previous/next, seeking and the volume. Before
playback starts it shows the connection status instead. The Wi-Fi symbol in
the top right corner is white while the network is connected and red while it
is not. Holding it for three seconds opens the Wi-Fi settings: the networks
found by a scan, and after picking one a password entry with an on-screen
keyboard. The credentials are stored with the `wifi_credentials` library and
the device connects with them.

The microphone button next to the speaker symbol switches between the cover
and the lyrics of the track. Synced lyrics scroll with the music with the current
line highlighted; when only plain lyrics exist they are shown as text. The
lyrics come from [LRCLIB](https://lrclib.net), a free service that needs no
account, looked up by title, artist and duration while the lyrics are shown.

The search bar at the top asks for a song or artist with the on-screen
keyboard and lists the matching songs; tapping one plays it, followed by the
hits after it.

The list button in the top left corner opens "Your Library": Liked Songs
and the user's playlists (up to 30 rows per list). Picking one lists its
tracks, and tapping a track starts playing it on the device, within that
playlist. Albums and paging beyond the first 30 entries are not implemented.

The library view and the search use the Spotify Web API and need a one-time
setup:

1. Create an application at <https://developer.spotify.com/dashboard> with
   the Web API enabled and the redirect URI `http://127.0.0.1:8888/callback`.
2. Export its credentials as `ZSPOT_WEB_CLIENT_ID` and
   `ZSPOT_WEB_CLIENT_SECRET`, e.g. from a private file that `~/.bashrc`
   sources.
3. Run `scripts/spotify_authorize.py --save <that file>` on the PC and
   approve the access in the browser; it adds `ZSPOT_WEB_REFRESH_TOKEN`.
4. Rebuild from a shell that has the three variables (`west build -p`): the
   `CONFIG_ZSPOT_WEB_*` options default to them. They give access to
   the account, so they stay in the environment and out of version control.

Without them the library view and the search only report that they are not
set up.

- `src/ui.c` builds the screen. The zspot callbacks only queue updates for
  it, and the touch handlers call the playback control API.
- `src/cover.c` downloads the cover (a 300x300 baseline JPEG) on its own
  thread and decodes it with the TJpgDec copy that ships with LVGL.
- `src/lyrics.c` fetches the lyrics on its own thread and splits them into
  timed lines.
- `src/library.c` talks to the Spotify Web API on its own thread:
  `/me/playlists`, `/me/tracks` and `/playlists/{id}/items` (or `/tracks`)
  for the listings, `/search` for the search, `/me/player/play` to start
  playback. `src/webapi.c`
  authorises the requests with an access token renewed from the refresh
  token, and `src/json_scan.c` picks the few fields needed out of the
  responses.
- The LVGL configuration lives in `app/prj.conf`. The memory pool
  (`CONFIG_LV_Z_MEM_POOL_SIZE`) holds the widgets and the cover art; the
  ESP32-S3 configuration (`app/socs/esp32s3_procpu.conf`) places the pool in
  PSRAM. The render buffer stays in internal RAM, because the display's SPI
  driver sends it with DMA, which cannot read from PSRAM.
- The built-in Montserrat fonts cover Latin text only; titles in other
  scripts show placeholder glyphs.

The screen was developed on `native_sim`. The ESP32-S3 configuration builds
but has not been verified on the hardware yet. The library view has only been
exercised with simulated responses, not against the live Web API.

### Checking the audio without a DAC

With `-DCONFIG_ZSPOT_PCM_TAP=y` the player also serves a copy of the audio it
hands to the output on TCP port 5000 (`CONFIG_ZSPOT_PCM_TAP_PORT`). On a
machine in the same network:

```sh
nc <device address> 5000 | aplay -f S16_LE -r 44100 -c 2 -B 1000000 -R 1000000
```

`-B` and `-R` make `aplay` hold one second of audio and wait for that much
before it starts (again after every interruption), so that the uneven
arrival over the network does not break up the sound. The device also
answers to `zspot.local` instead of its address.

The copy is taken before the volume is applied. When it sounds right but the
DAC does not, the fault is in the I2S configuration, the wiring or the DAC;
the tap cannot show the signal on the I2S pins itself. Without a client the
tap costs nothing but its buffer, and a client that cannot keep up only loses
audio on its own side.

### native_sim

The player also builds for `native_sim`, using the Zephyr IP stack over the
host TAP interface. The display is an SDL window (needs the SDL2 development
package on the host) with the mouse acting as the touch screen. Decoded audio
is played on the host: the simulator starts `aplay` (alsa-utils) and pipes the
PCM into it at playback speed (`CONFIG_ZSPOT_PCM_COMMAND`; the build
warns when the player is not installed). To capture the audio instead, clear
that option and set `CONFIG_ZSPOT_PCM_FILE` to a host file, playable
with `aplay -f S16_LE -r 44100 -c 2 <file>`.

Create the TAP device once per boot (root required) and give the simulator
internet access through NAT:

```sh
sudo tools/net-tools/net-setup.sh          # creates zeth, host side 192.0.2.2
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -s 192.0.2.0/24 -j MASQUERADE
```

Then build and run:

```sh
west build -p -b native_sim/native/64 app
./build/zephyr/zephyr.exe
curl 'http://192.0.2.1:8080/spotify_info?action=getInfo'
```

The Spotify app only discovers devices through mDNS on its own network. To
reach the simulator from the LAN without routing, bridge it from user space
(both tools need only Python 3; the second one uses `python3-cryptography`):

```sh
scripts/native_sim_lan_bridge.py 8080 192.0.2.1 &            # LAN:8080 -> simulator
avahi-publish-service CSpot _spotify-connect._tcp 8080 \
    VERSION=1.0 CPath=/spotify_info Stack=SP &             # advertise on the LAN
scripts/zeroconf_client.py http://192.0.2.1:8080             # emulate the app's hand-over
```

`scripts/zeroconf_client.py` performs the same addUser hand-over as the Spotify app
(Diffie-Hellman, blob encryption) with a dummy token, which exercises the
whole zeroconf and access point path; Spotify then declines the token.
Alternatively provide credentials at build time with
`CONFIG_ZSPOT_CREDENTIALS_JSON` (stored credentials) or, for protocol
testing only, `CONFIG_ZSPOT_USERNAME` / `CONFIG_ZSPOT_PASSWORD`.

## Layout

```
app/                 player application (C): CMakeLists, Kconfig, prj.conf, boards/, socs/, src/
include/zspot/       public C API
lib/zspot/           the library: Kconfig, CMakeLists, src/core (cspot C++ protocol core),
                     src/port (Zephyr glue), src/api (C facade), src/audio (I2S sink),
                     protobuf/ (nanopb definitions), third_party/tremor (Vorbis decoder)
doc/images/          README banner and screenshot
tests/lib/           ztest unit tests (run with west twister -T tests)
scripts/             zeroconf hand-over emulator, native_sim LAN bridge, Web API authorisation
zephyr/module.yml    Zephyr module descriptor (CMakeLists.txt and Kconfig at the root)
west.yml             west manifest (shared or standalone workspace, see Getting started)
.github/workflows/   CI: builds the app and runs the tests with twister
```

## Tests

```sh
west twister -T tests -p native_sim/native/64 --inline-logs
```

## Licence

GPL-3.0, like cspot. Tremor is BSD-licensed (see `third_party/tremor/COPYING`).

Zephyr and the Zephyr logo are trademarks of The Linux Foundation; the logo
comes from the Zephyr documentation assets (Apache-2.0). Spotify and the
Spotify icon are trademarks of Spotify AB and identify the Spotify Connect
protocol only; this project is not affiliated with or endorsed by Spotify.
