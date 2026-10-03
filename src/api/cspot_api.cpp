/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * C API facade over the protocol core.
 */

#include <cspot/cspot.h>

#include <zephyr/kernel.h>
#if CONFIG_CSPOT_EXTERNAL_TLS_HEAP_SIZE > 0
#include <mbedtls/memory_buffer_alloc.h>
#endif

#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include "core/Context.h"
#include "core/LoginBlob.h"
#include "core/MercurySession.h"
#include "core/SpircHandler.h"
#include "core/TrackPlayer.h"
#include "core/TrackQueue.h"
#include "port/log.h"
#include "port/mem.h"
#include "port/thread.h"

#ifdef CONFIG_CSPOT_MDNS
#include "port/mdns.h"
#endif
#ifdef CONFIG_CSPOT_ZEROCONF
#include "port/zeroconf.h"
#endif

CSPOT_LOG_MODULE_DECLARE();

namespace
{

class SessionTask;

struct state {
	bool initialised = false;
	std::string device_name;
	AudioFormat audio_format = AudioFormat_OGG_VORBIS_160;

	std::shared_ptr<cspot::LoginBlob> blob;
	std::shared_ptr<cspot::Context> ctx;
	std::shared_ptr<cspot::SpircHandler> handler;
	std::unique_ptr<SessionTask> task;

	cspot_event_cb_t event_cb = nullptr;
	cspot_pcm_cb_t pcm_cb = nullptr;
	void *user_data = nullptr;

	std::atomic<bool> running = false;
	std::atomic<bool> connected = false;
	std::atomic<bool> have_credentials = false;

	struct k_sem credentials_sem;
	struct k_sem connect_sem;
	int connect_result = 0;
};

struct state g;

AudioFormat to_audio_format(enum cspot_audio_format format)
{
	switch (format) {
	case CSPOT_FORMAT_OGG_VORBIS_96:
		return AudioFormat_OGG_VORBIS_96;
	case CSPOT_FORMAT_OGG_VORBIS_320:
		return AudioFormat_OGG_VORBIS_320;
	case CSPOT_FORMAT_OGG_VORBIS_160:
	default:
		return AudioFormat_OGG_VORBIS_160;
	}
}

void emit(const struct cspot_event &event)
{
	if (g.event_cb != nullptr) {
		g.event_cb(&event, g.user_data);
	}
}

void on_spirc_event(std::unique_ptr<cspot::SpircHandler::Event> ev)
{
	using EventType = cspot::SpircHandler::EventType;
	struct cspot_event out = {};

	switch (ev->eventType) {
	case EventType::PLAY_PAUSE:
		out.type = CSPOT_EVENT_PLAY_PAUSE;
		out.paused = std::get<bool>(ev->data);
		break;
	case EventType::VOLUME:
		out.type = CSPOT_EVENT_VOLUME;
		out.volume = static_cast<uint16_t>(std::get<int>(ev->data));
		break;
	case EventType::TRACK_INFO: {
		const auto &info = std::get<cspot::TrackInfo>(ev->data);

		out.type = CSPOT_EVENT_TRACK_INFO;
		out.track.name = info.name.c_str();
		out.track.album = info.album.c_str();
		out.track.artist = info.artist.c_str();
		out.track.image_url = info.imageUrl.c_str();
		out.track.track_id = info.trackId.c_str();
		out.track.duration_ms = info.duration;
		out.track.number = info.number;
		out.track.disc_number = info.discNumber;
		break;
	}
	case EventType::DISC:
		out.type = CSPOT_EVENT_DISCONNECT;
		break;
	case EventType::NEXT:
		out.type = CSPOT_EVENT_NEXT;
		break;
	case EventType::PREV:
		out.type = CSPOT_EVENT_PREV;
		break;
	case EventType::SEEK:
		out.type = CSPOT_EVENT_SEEK;
		out.position_ms = static_cast<uint32_t>(std::get<int>(ev->data));
		break;
	case EventType::DEPLETED:
		out.type = CSPOT_EVENT_DEPLETED;
		break;
	case EventType::FLUSH:
		out.type = CSPOT_EVENT_FLUSH;
		break;
	case EventType::PLAYBACK_START:
		out.type = CSPOT_EVENT_PLAYBACK_START;
		out.position_ms = static_cast<uint32_t>(std::get<int>(ev->data));
		break;
	default:
		return;
	}
	emit(out);
}

size_t on_pcm(uint8_t *data, size_t len, std::string_view)
{
	if (g.pcm_cb == nullptr) {
		return len; /* nobody listening: discard */
	}
	return g.pcm_cb(data, len, g.user_data);
}

/*
 * Owns the session: connects and authenticates (reported back through
 * connect_sem), then dispatches Mercury packets until disconnected.
 */
class SessionTask : public cspot::Task
{
public:
	SessionTask() : cspot::Task("cspot_main", CONFIG_CSPOT_MAIN_STACK_SIZE, 1)
	{
	}

protected:
	void runTask() override
	{
		g.connect_result = connect();
		k_sem_give(&g.connect_sem);
		if (g.connect_result != 0) {
			return;
		}

		while (g.running) {
			try {
				g.ctx->session->handlePacket();
			} catch (const std::exception &e) {
				LOG_ERR("Mercury dispatch error: %s", e.what());
			}
		}
	}

private:
	int connect()
	{
		try {
			g.ctx = cspot::Context::createFromBlob(g.blob);
			g.ctx->config.audioFormat = g.audio_format;

			LOG_INF("Connecting to Spotify as %s", g.blob->getUserName().c_str());
			g.ctx->session->connectWithRandomAp();

			auto token = g.ctx->session->authenticate(g.blob);

			if (token.empty()) {
				LOG_ERR("Spotify rejected the credentials");
				return -EACCES;
			}

			/* Keep the reusable credentials for login5 and for persisting. */
			g.ctx->config.authData = token;

			g.ctx->session->startTask();

			g.handler = std::make_shared<cspot::SpircHandler>(g.ctx);
			g.handler->setEventHandler(on_spirc_event);
			g.handler->getTrackPlayer()->setDataCallback(on_pcm);

			g.connected = true;
			LOG_INF("Spotify session established");
			return 0;
		} catch (const std::exception &e) {
			LOG_ERR("Connection failed: %s", e.what());
			return -EIO;
		}
	}
};

} /* namespace */

/* Lifecycle --------------------------------------------------------------- */

int cspot_init(const struct cspot_config *config)
{
	if (g.initialised) {
		return -EALREADY;
	}

	g.device_name = (config != nullptr && config->device_name != nullptr)
				? config->device_name
				: CONFIG_CSPOT_DEVICE_NAME;
	g.audio_format = to_audio_format(config != nullptr ? config->audio_format
							   : CSPOT_FORMAT_OGG_VORBIS_160);

	k_sem_init(&g.credentials_sem, 0, 1);
	k_sem_init(&g.connect_sem, 0, 1);

#if CONFIG_CSPOT_EXTERNAL_TLS_HEAP_SIZE > 0
	/*
	 * Move the Mbed TLS buffer allocator to external memory; the static
	 * heap configured with CONFIG_MBEDTLS_HEAP_SIZE is then only a stub.
	 */
	static void *tls_heap = cspot_mem_alloc(CONFIG_CSPOT_EXTERNAL_TLS_HEAP_SIZE);

	if (tls_heap != nullptr) {
		mbedtls_memory_buffer_alloc_free();
		mbedtls_memory_buffer_alloc_init(static_cast<unsigned char *>(tls_heap),
						 CONFIG_CSPOT_EXTERNAL_TLS_HEAP_SIZE);
	} else {
		LOG_WRN("No external memory for the TLS heap, using the static one");
	}
#endif

	try {
		g.blob = std::make_shared<cspot::LoginBlob>(g.device_name);
	} catch (const std::exception &e) {
		LOG_ERR("Initialisation failed: %s", e.what());
		return -EIO;
	}

	g.initialised = true;
	LOG_INF("cspot initialised, device id %s", g.blob->getDeviceId().c_str());
	return 0;
}

const char *cspot_device_id(void)
{
	return g.blob ? g.blob->getDeviceId().c_str() : "";
}

const char *cspot_device_name(void)
{
	return g.device_name.c_str();
}

/* Credentials ------------------------------------------------------------- */

int cspot_credentials_set_user_pass(const char *username, const char *password)
{
	if (!g.initialised || username == nullptr || password == nullptr) {
		return -EINVAL;
	}
	g.blob->loadUserPass(username, password);
	g.have_credentials = true;
	return 0;
}

int cspot_credentials_load_json(const char *json)
{
	if (!g.initialised || json == nullptr) {
		return -EINVAL;
	}
	try {
		g.blob->loadJson(json);
	} catch (const std::exception &e) {
		LOG_ERR("Invalid credentials: %s", e.what());
		return -EINVAL;
	}
	g.have_credentials = !g.blob->authData.empty();
	return g.have_credentials ? 0 : -EINVAL;
}

int cspot_credentials_save_json(char *buf, size_t size)
{
	std::string json;

	if (!g.initialised || buf == nullptr) {
		return -EINVAL;
	}
	if (g.connected && g.ctx) {
		json = g.ctx->getCredentialsJson();
	} else if (g.have_credentials) {
		json = g.blob->toJson();
	} else {
		return -ENOENT;
	}
	if (json.size() + 1 > size) {
		return -ENOMEM;
	}
	memcpy(buf, json.c_str(), json.size() + 1);
	return static_cast<int>(json.size());
}

bool cspot_credentials_available(void)
{
	return g.have_credentials;
}

void cspot_credentials_clear(void)
{
	g.have_credentials = false;
	if (g.blob) {
		g.blob->authData.clear();
		g.blob->username.clear();
	}
	k_sem_reset(&g.credentials_sem);
}

/* Zeroconf ---------------------------------------------------------------- */

int cspot_zeroconf_start(void)
{
	if (!g.initialised) {
		return -EINVAL;
	}
#ifdef CONFIG_CSPOT_ZEROCONF
	int ret = cspot::zeroconf_start(g.blob, []() {
		g.have_credentials = true;
		k_sem_give(&g.credentials_sem);
	});

	if (ret < 0) {
		return ret;
	}
#ifdef CONFIG_CSPOT_MDNS
	ret = cspot_mdns_advertise(g.device_name.c_str(), CONFIG_CSPOT_ZEROCONF_PORT);
	if (ret < 0) {
		return ret;
	}
#endif
	return 0;
#else
	return -ENOTSUP;
#endif
}

int cspot_zeroconf_wait(int32_t timeout_ms)
{
	const k_timeout_t timeout = timeout_ms < 0 ? K_FOREVER : K_MSEC(timeout_ms);

	if (g.have_credentials) {
		return 0;
	}
	return k_sem_take(&g.credentials_sem, timeout) == 0 ? 0 : -EAGAIN;
}

void cspot_zeroconf_stop(void)
{
#ifdef CONFIG_CSPOT_MDNS
	cspot_mdns_withdraw();
#endif
#ifdef CONFIG_CSPOT_ZEROCONF
	cspot::zeroconf_stop();
#endif
}

/* Session ----------------------------------------------------------------- */

int cspot_connect(cspot_event_cb_t event_cb, cspot_pcm_cb_t pcm_cb, void *user_data)
{
	if (!g.initialised || !g.have_credentials) {
		return -EINVAL;
	}
	if (g.running) {
		return -EALREADY;
	}

	g.event_cb = event_cb;
	g.pcm_cb = pcm_cb;
	g.user_data = user_data;
	g.running = true;

	k_sem_reset(&g.connect_sem);
	g.task = std::make_unique<SessionTask>();
	if (!g.task->startTask()) {
		g.running = false;
		g.task.reset();
		return -ENOMEM;
	}

	k_sem_take(&g.connect_sem, K_FOREVER);
	if (g.connect_result != 0) {
		g.running = false;
		g.task.reset();
		g.handler.reset();
		g.ctx.reset();
	}
	return g.connect_result;
}

void cspot_disconnect(void)
{
	if (!g.running) {
		return;
	}
	g.running = false;
	g.connected = false;

	if (g.handler) {
		g.handler->disconnect();
	}
	g.task.reset(); /* joins the session thread */
	g.handler.reset();
	g.ctx.reset();
	LOG_INF("Disconnected from Spotify");
}

bool cspot_is_connected(void)
{
	return g.connected;
}

/* Playback control -------------------------------------------------------- */

void cspot_set_pause(bool paused)
{
	if (g.handler) {
		g.handler->setPause(paused);
	}
}

bool cspot_next(void)
{
	return g.handler ? g.handler->nextSong() : false;
}

bool cspot_previous(void)
{
	return g.handler ? g.handler->previousSong() : false;
}

void cspot_set_volume(uint16_t volume)
{
	if (g.handler) {
		g.handler->setRemoteVolume(volume);
	}
}

void cspot_notify_audio_reached_playback(void)
{
	if (g.handler) {
		g.handler->notifyAudioReachedPlayback();
	}
}

void cspot_notify_audio_ended(void)
{
	if (g.handler) {
		g.handler->notifyAudioEnded();
	}
}

void cspot_update_position_ms(uint32_t position_ms)
{
	if (g.handler) {
		g.handler->updatePositionMs(position_ms);
	}
}
