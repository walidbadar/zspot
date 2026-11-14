#!/usr/bin/env python3
# Copyright (c) 2026 Muhammad Waleed Badar
#
# SPDX-License-Identifier: GPL-3.0-only
"""One-time authorisation of the search and "Your Library" of the zspot player.

Create an application at https://developer.spotify.com/dashboard with the
Web API enabled and the redirect URI http://127.0.0.1:8888/callback, export
its credentials as ZSPOT_WEB_CLIENT_ID and ZSPOT_WEB_CLIENT_SECRET, then run:

  spotify_authorize.py --device <host>     send the credentials to the device
  spotify_authorize.py [--save <env-file>] make them available to the build

The script opens the Spotify consent page in the browser, receives the
authorisation code on the redirect URI and exchanges it for a refresh token.

With --device the client ID, the client secret and the refresh token go to
the player (e.g. zspot.local), which keeps them. It takes them while it has
none; to replace them, open its Wi-Fi settings first (hold the Wi-Fi symbol).
They cross the local network unencrypted. Add --send to pass on a refresh
token that is in ZSPOT_WEB_REFRESH_TOKEN already, without asking Spotify.

Otherwise the build reads the token from ZSPOT_WEB_REFRESH_TOKEN: the script
prints the export line, or with --save stores it in the given shell file
(e.g. the one that already exports the other two variables)."""
import argparse, base64, http.server, json, os, re, secrets, sys
import urllib.error, urllib.parse, urllib.request, webbrowser

SETUP_PORT = 8081  # CONFIG_ZSPOT_WEB_SETUP_PORT

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "playlist-read-private playlist-read-collaborative user-library-read user-modify-playback-state"

parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--save", metavar="ENV-FILE", help="store the refresh token in this shell file")
parser.add_argument("--device", metavar="HOST[:PORT]", help="send the credentials to this player")
parser.add_argument("--send", action="store_true",
                    help="with --device: send the token of the environment, do not authorise")
args = parser.parse_args()

client_id = os.environ.get("ZSPOT_WEB_CLIENT_ID")
client_secret = os.environ.get("ZSPOT_WEB_CLIENT_SECRET")
save_to = args.save
if not client_id or not client_secret or (args.send and not args.device):
    sys.exit(__doc__)


def send_to_device(refresh_token):
    """Hands the three values to the setup endpoint of the player."""
    host, _, port = args.device.partition(":")
    url = "http://%s:%s/webapi" % (host, port or SETUP_PORT)
    form = urllib.parse.urlencode({"client_id": client_id, "client_secret": client_secret,
                                   "refresh_token": refresh_token}).encode()
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=form), timeout=15) as response:
            print("%s: %s" % (args.device, response.read().decode(errors="replace").strip()))
    except urllib.error.HTTPError as e:
        sys.exit("%s: %s" % (args.device, e.read().decode(errors="replace").strip()))
    except OSError as e:
        sys.exit("Cannot reach %s: %s" % (url, e))


if args.send:
    if not os.environ.get("ZSPOT_WEB_REFRESH_TOKEN"):
        sys.exit("--send needs ZSPOT_WEB_REFRESH_TOKEN")
    send_to_device(os.environ["ZSPOT_WEB_REFRESH_TOKEN"])
    sys.exit(0)

state = secrets.token_urlsafe(16)
result = {}

class Callback(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        if query.get("state") == [state]:
            result.update(code=query.get("code", [None])[0], error=query.get("error", [None])[0])
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"zspot: you can close this page and return to the terminal.\n")

    def log_message(self, *args):
        pass

url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
    "response_type": "code", "client_id": client_id, "scope": SCOPES,
    "redirect_uri": REDIRECT_URI, "state": state})
print("Open this page and approve the access if the browser does not start:\n\n  " + url + "\n")
webbrowser.open(url)

with http.server.HTTPServer(("127.0.0.1", 8888), Callback) as server:
    while not result:
        server.handle_request()
if not result.get("code"):
    sys.exit("Authorisation failed: %s" % result.get("error"))

request = urllib.request.Request(
    "https://accounts.spotify.com/api/token",
    data=urllib.parse.urlencode({"grant_type": "authorization_code", "code": result["code"],
                                 "redirect_uri": REDIRECT_URI}).encode(),
    headers={"Authorization": "Basic " + base64.b64encode(
        ("%s:%s" % (client_id, client_secret)).encode()).decode()})
try:
    with urllib.request.urlopen(request) as response:
        tokens = json.load(response)
except urllib.error.HTTPError as e:
    sys.exit("Token exchange failed: HTTP %d %s" % (e.code, e.read().decode(errors="replace")))

line = 'export ZSPOT_WEB_REFRESH_TOKEN="%s"' % tokens["refresh_token"]
if args.device:
    send_to_device(tokens["refresh_token"])
if save_to:
    try:
        with open(save_to) as f:
            content = f.read()
    except FileNotFoundError:
        content = ""
    content = re.sub(r"^export ZSPOT_WEB_REFRESH_TOKEN=.*\n", "", content, flags=re.M)
    fd = os.open(save_to, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(content + ("" if content.endswith("\n") or not content else "\n") + line + "\n")
    print("Authorised. The refresh token is stored in %s; source that file (or open a new" % save_to)
    print("shell) and rebuild with \"west build -p\" so that the build picks it up.")
elif not args.device:
    print("Authorised. Make this available to the build (keep it out of version control):\n")
    print("  " + line)
