#!/usr/bin/env python3
# Copyright (c) 2026 Muhammad Waleed Badar
#
# SPDX-License-Identifier: GPL-3.0-only
"""One-time authorisation of the "Your Library" view of the zspot sample.

Create an application at https://developer.spotify.com/dashboard with the
Web API enabled and the redirect URI http://127.0.0.1:8888/callback, export
its credentials as ZSPOT_WEB_CLIENT_ID and ZSPOT_WEB_CLIENT_SECRET, then run:

  spotify_authorize.py [--save <env-file>]

The script opens the Spotify consent page in the browser, receives the
authorisation code on the redirect URI and exchanges it for a refresh token.
The build reads that from ZSPOT_WEB_REFRESH_TOKEN: the script prints the export
line, or with --save stores it in the given shell file (e.g. the one that
already exports the other two variables)."""
import base64, http.server, json, os, re, secrets, sys, urllib.parse, urllib.request, webbrowser

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "playlist-read-private playlist-read-collaborative user-library-read user-modify-playback-state"

client_id = os.environ.get("ZSPOT_WEB_CLIENT_ID")
client_secret = os.environ.get("ZSPOT_WEB_CLIENT_SECRET")
save_to = sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--save" else None
if not client_id or not client_secret or (len(sys.argv) > 1 and not save_to):
    sys.exit(__doc__)
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
else:
    print("Authorised. Make this available to the build (keep it out of version control):\n")
    print("  " + line)
