#!/usr/bin/env python3
# Copyright (c) 2026 Muhammad Waleed Badar
#
# SPDX-License-Identifier: GPL-3.0-only
"""Bridge a LAN port to the native_sim device (default 192.0.2.1) and log HTTP request/response heads.

Usage: native_sim_lan_bridge.py [port] [target-ip]
Advertise the device on the LAN with:
  avahi-publish-service CSpot _spotify-connect._tcp 8080 VERSION=1.0 CPath=/spotify_info Stack=SP"""
import asyncio, sys

LISTEN_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
TARGET = (sys.argv[2] if len(sys.argv) > 2 else "192.0.2.1", LISTEN_PORT)

async def pipe(reader, writer, tag):
    first = True
    try:
        while data := await reader.read(65536):
            if first:
                head = data[:400].decode("latin1").replace("\r", "")
                print(f"[{tag}] {len(data)}B: " + head.replace("\n", " | ")[:400], flush=True)
                first = False
            writer.write(data)
            await writer.drain()
    except (ConnectionError, asyncio.CancelledError):
        pass
    finally:
        print(f"[{tag}] closed", flush=True)
        try:
            writer.close()
        except Exception:
            pass

async def handle(client_r, client_w):
    peer = client_w.get_extra_info("peername")
    try:
        target_r, target_w = await asyncio.open_connection(*TARGET)
    except OSError as e:
        print(f"[fwd] {peer} -> {TARGET} failed: {e}", flush=True)
        client_w.close()
        return
    print(f"[fwd] {peer} -> {TARGET}", flush=True)
    await asyncio.gather(pipe(client_r, target_w, f"req {peer[1]}"), pipe(target_r, client_w, f"rsp {peer[1]}"))

async def main():
    server = await asyncio.start_server(handle, host=None, port=LISTEN_PORT, reuse_address=True)
    print(f"[fwd] listening on *:{LISTEN_PORT} -> {TARGET}", flush=True)
    async with server:
        await server.serve_forever()

asyncio.run(main())
