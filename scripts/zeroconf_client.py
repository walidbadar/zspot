#!/usr/bin/env python3
# Copyright (c) 2026 Muhammad Waleed Badar
#
# SPDX-License-Identifier: GPL-3.0-only
"""Emulates the Spotify app's zeroconf addUser hand-over (librespot blob format)."""
import base64, hashlib, hmac, os, secrets, sys, urllib.parse, urllib.request
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.primitives import hashes

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://192.0.2.1:8080"
USER = sys.argv[2] if len(sys.argv) > 2 else "cspot-zeroconf-test"
AUTH_TYPE = 1  # AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS
AUTH_DATA = b"not-a-real-token"

DH_PRIME = int(
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A63A3620FFFFFFFFFFFFFFFF", 16)

def varint(n):
    return bytes([n]) if n < 0x80 else bytes([(n & 0x7F) | 0x80, n >> 7])

info = urllib.request.urlopen(f"{BASE}/spotify_info?action=getInfo", timeout=5).read()
import json
info = json.loads(info)
device_id = info["deviceID"]
device_pub = int.from_bytes(base64.b64decode(info["publicKey"]), "big")
print("device:", info["remoteName"], device_id)

priv = secrets.randbits(95 * 8)
client_pub = pow(2, priv, DH_PRIME).to_bytes(96, "big")
shared = pow(device_pub, priv, DH_PRIME).to_bytes(96, "big")

# Inner blob: 'I' <len:0> 'P' <authType> 'Q' <len> <authData>, zero padded to 16
plain = b"I" + varint(0) + b"P" + varint(AUTH_TYPE) + b"Q" + varint(len(AUTH_DATA)) + AUTH_DATA
plain += b"\0" * (-len(plain) % 16)
data = bytearray(plain)
for j in range(16, len(data)):  # inverse of the device's unchaining loop
    data[j] ^= data[j - 16]

secret = hashlib.sha1(device_id.encode()).digest()
base_key = PBKDF2HMAC(algorithm=hashes.SHA1(), length=20, salt=USER.encode(), iterations=256).derive(secret)
key = hashlib.sha1(base_key).digest() + b"\x00\x00\x00\x14"  # AES-192
enc = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
inner = base64.b64encode(enc.update(bytes(data)) + enc.finalize())

# Outer blob: iv + AES-128-CTR(inner) + HMAC-SHA1 checksum
base = hashlib.sha1(shared).digest()[:16]
checksum_key = hmac.new(base, b"checksum", hashlib.sha1).digest()
encryption_key = hmac.new(base, b"encryption", hashlib.sha1).digest()[:16]
iv = os.urandom(16)
ctr = Cipher(algorithms.AES(encryption_key), modes.CTR(iv)).encryptor()
encrypted = ctr.update(inner) + ctr.finalize()
mac = hmac.new(checksum_key, encrypted, hashlib.sha1).digest()
blob = base64.b64encode(iv + encrypted + mac).decode()

form = urllib.parse.urlencode({
    "action": "addUser", "userName": USER, "blob": blob,
    "clientKey": base64.b64encode(client_pub).decode(), "deviceName": "zeroconf_client.py",
}).encode()
req = urllib.request.Request(f"{BASE}/spotify_info", data=form,
                             headers={"Content-Type": "application/x-www-form-urlencoded"})
print("addUser response:", urllib.request.urlopen(req, timeout=10).read().decode())
