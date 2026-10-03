# cspot-zephyr

Spotify Connect receiver library for [Zephyr RTOS](https://zephyrproject.org),
packaged as a Zephyr module with a plain C API.

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
| Logging            | Zephyr logging, module `cspot`                           |
| Audio output       | Zephyr I2S driver API (optional helper sink)             |

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
- A board with IPv4 networking and, for the sample, an I2S DAC. The
  protocol needs roughly 300 KB of heap at runtime (TLS buffers, decoder,
  track buffers). With `CONFIG_CSPOT_EXTERNAL_HEAP` (default when
  `CONFIG_SHARED_MULTI_HEAP` is available, e.g. `CONFIG_ESP_SPIRAM=y`) the
  C++ free store, the decoder buffers, the protocol thread stacks and the
  Mbed TLS heap are taken from external memory, which keeps the internal
  RAM footprint of the sample small enough for the ESP32 family.

A `protoc` is needed only at build time. Without a system package:

```sh
python3 -m venv ~/.venvs/protoc && ~/.venvs/protoc/bin/pip install grpcio-tools
printf '#!/bin/sh\nexec "$(dirname "$0")/python" -m grpc_tools.protoc "$@"\n' > ~/.venvs/protoc/bin/protoc
chmod +x ~/.venvs/protoc/bin/protoc
export PATH=~/.venvs/protoc/bin:$PATH
```

## Adding the module

In your `west.yml`:

```yaml
manifest:
  projects:
    - name: cspot-zephyr
      url: https://github.com/<you>/cspot-zephyr
      revision: main
      path: modules/lib/cspot
```

or, without west, pass `-DZEPHYR_EXTRA_MODULES=/path/to/cspot-zephyr` to the
build.

## Configuration

Enable `CONFIG_CSPOT=y` together with `CONFIG_CPP=y` and
`CONFIG_STD_CPP20=y`. Useful options:

| Kconfig                          | Purpose                                        |
|----------------------------------|------------------------------------------------|
| `CSPOT_DEVICE_NAME`              | Name shown in the Spotify app                  |
| `CSPOT_ZEROCONF` / `_PORT`       | Zeroconf credential hand-over endpoint         |
| `CSPOT_MDNS`                     | DNS-SD advertisement (`_spotify-connect._tcp`) |
| `CSPOT_I2S_SINK`                 | Ready-made PCM sink using the I2S driver       |
| `CSPOT_TLS_SEC_TAG`              | CA credential tag for server verification (-1: no verification) |
| `CSPOT_EXTERNAL_HEAP`            | Large buffers from the shared multi heap (PSRAM) |
| `CSPOT_STACKS_EXTERNAL`          | Protocol thread stacks in external memory      |
| `CSPOT_EXTERNAL_TLS_HEAP_SIZE`   | Mbed TLS heap relocated to external memory     |
| `CSPOT_*_STACK_SIZE`             | Stacks of the protocol threads                 |
| `CSPOT_LOG_LEVEL_*`              | Log level of the `cspot` module                |

TLS towards Spotify needs TLS 1.2 with ECDHE-RSA and AES-GCM; see
`samples/player/prj.conf` for a working Mbed TLS / PSA configuration.

## API

```c
#include <cspot/cspot.h>
#include <cspot/cspot_i2s_sink.h>

static size_t on_pcm(const uint8_t *pcm, size_t len, void *user)
{
    return cspot_i2s_sink_write(pcm, len, user);   /* 0 = try again later */
}

static void on_event(const struct cspot_event *ev, void *user)
{
    if (ev->type == CSPOT_EVENT_VOLUME) {
        cspot_i2s_sink_set_volume(ev->volume);
    }
}

struct cspot_config cfg = { .device_name = "Living room", .audio_format = CSPOT_FORMAT_OGG_VORBIS_160 };
cspot_init(&cfg);
cspot_i2s_sink_init(DEVICE_DT_GET(DT_ALIAS(cspot_i2s)), 44100, 2, 16);

cspot_zeroconf_start();          /* advertise + serve /spotify_info          */
cspot_zeroconf_wait(-1);         /* until the Spotify app hands over credentials */
cspot_connect(on_event, on_pcm, NULL);

char json[1024];
cspot_credentials_save_json(json, sizeof(json));  /* persist for next boot  */
```

On the next boot `cspot_credentials_load_json()` skips the zeroconf step.
Playback control (`cspot_set_pause`, `cspot_next`, `cspot_previous`,
`cspot_set_volume`) and position feedback (`cspot_update_position_ms`) are
available for local buttons and displays.

Callbacks run on the library's threads.

## Sample

`samples/player` joins Wi-Fi, advertises the device, waits for the Spotify
app and plays through I2S. The supported board is the VIEWE
UEDX32480035E-WB-A (`uedx32480035e_wb_a/esp32s3/procpu`, ESP32-S3 with 8 MB
PSRAM). It has no audio DAC on board, so connect an external I2S DAC to
BCK = GPIO5, WS/LRCK = GPIO6 and DATA = GPIO7. The console and shell are on
the native USB serial port.

```sh
west build -p -b uedx32480035e_wb_a/esp32s3/procpu samples/player
west flash
```

Wi-Fi credentials are handled by Zephyr's `wifi_credentials` library with the
settings/NVS backend. Add them once on the shell; they persist across reboots
and the sample connects with `NET_REQUEST_WIFI_CONNECT_STORED`, retrying
whenever the link drops:

```
uart:~$ wifi cred add -s MyNetwork -k 1 -p secret
uart:~$ wifi cred auto_connect
```

To bake the credentials into the image instead:

```sh
west build -p -b uedx32480035e_wb_a/esp32s3/procpu samples/player -- \
    -DEXTRA_CONF_FILE=wifi_static.conf \
    -DCONFIG_WIFI_CREDENTIALS_STATIC_SSID=\"MyNetwork\" \
    -DCONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD=\"secret\"
```

Stored Spotify credentials can be passed with
`-DCONFIG_CSPOT_SAMPLE_CREDENTIALS_JSON='"..."'` (the JSON printed by the
sample after its first connection) to skip the zeroconf hand-over.

### native_sim

The sample also builds for `native_sim`, using the Zephyr IP stack over the
host TAP interface. Decoded audio is appended to `/tmp/cspot.pcm`
(`CONFIG_CSPOT_SAMPLE_PCM_FILE`), playable with
`aplay -f S16_LE -r 44100 -c 2 /tmp/cspot.pcm`.

Create the TAP device once per boot (root required) and give the simulator
internet access through NAT:

```sh
sudo tools/net-tools/net-setup.sh          # creates zeth, host side 192.0.2.2
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -s 192.0.2.0/24 -j MASQUERADE
```

Then build and run:

```sh
west build -p -b native_sim/native/64 samples/player
./build/zephyr/zephyr.exe
curl 'http://192.0.2.1:8080/spotify_info?action=getInfo'
```

The Spotify app only discovers devices through mDNS on its own network, so
on native_sim either route the 192.0.2.0/24 subnet to the phone's LAN or
provide credentials at build time: `CONFIG_CSPOT_SAMPLE_CREDENTIALS_JSON`
(stored credentials) or, for protocol testing only,
`CONFIG_CSPOT_SAMPLE_USERNAME` / `CONFIG_CSPOT_SAMPLE_PASSWORD`.

## Layout

```
include/cspot/      public C API
src/core/           Spotify protocol (C++, from cspot)
src/port/           Zephyr glue: crypto, HTTP, JSON, threads, mDNS, zeroconf
src/audio/          I2S sink
protobuf/           Spotify protocol buffer definitions (nanopb)
third_party/tremor/ Ogg Vorbis decoder
samples/player/     reference application (C)
```

## Status

- Builds for `uedx32480035e_wb_a/esp32s3/procpu` and `native_sim/native/64`
  against Zephyr 4.5.0-rc1 with Zephyr SDK 1.0.1.
- Verified on native_sim over the TAP interface: mDNS/DNS-SD advertisement
  (visible to avahi), the zeroconf endpoint, access point resolution over
  TLS, the access point handshake (Diffie-Hellman, HMAC challenge, Shannon
  keys) and the encrypted login exchange up to Spotify's reply. Playback
  needs real credentials and has not been exercised yet; neither has the
  ESP32-S3 target on hardware.
- TLS peer verification is off unless `CSPOT_TLS_SEC_TAG` names a CA
  credential.
- Thread stacks in external memory (`CSPOT_STACKS_EXTERNAL`) must not be
  combined with flash writes that disable the cache while playing.

## Coding style

New code (public headers, the Zephyr port layer, the C API, the sink and the
sample) follows the Zephyr coding style. `src/core/` keeps the upstream cspot
formatting and class names so changes can be tracked against cspot; it is
the only place where C++ is used, and it is never exposed to applications.
Every source file carries an SPDX license header.

## Licence

GPL-3.0, like cspot. Tremor is BSD-licensed (see `third_party/tremor/COPYING`).
