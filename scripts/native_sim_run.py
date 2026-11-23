#!/usr/bin/env python3
# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
"""Runs the native_sim player with the TAP interface it talks through.

Creates the interface with Zephyr's net-tools (net-setup.sh and nat.conf:
192.0.2.2 on the host, NAT to the internet), runs the player, and removes the
interface again once the player has ended: by itself, by Ctrl+C, or killed.
An interface that is there already is used and left as it is.

  native_sim_run.py --net-tools <dir> <zephyr.exe> [options of zephyr.exe]

"west build -t run_tap" runs it with the net-tools next to the Zephyr tree of
the build (<west workspace>/tools/net-tools), or those in ZEPHYR_NET_TOOLS.
Creating and removing the interface takes root: sudo asks for the password.
"""
import argparse
import os
import pwd
import signal
import subprocess
import sys
import time

IFACE = "zeth"
CONFIG = "nat.conf"
# The player is given this long to end after SIGINT before it is killed
STOP_TIMEOUT_S = 3


def main_thread_ended(pid):
    """Whether the main thread of process pid has exited while others still run.

    zephyr.exe can stay like that when it ends: a zombie that is not reaped
    until the threads that its host libraries started are gone too.
    """
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().rsplit(")", 1)[1].split()[0] == "Z"
    except OSError:
        return False


def net_setup(tools, action):
    # With a path: net-setup.sh sources <config>.stop as given, and a name
    # without one is looked for in PATH, not in the current directory
    cmd = ["./net-setup.sh", "--config", "./" + CONFIG, "--iface", IFACE, action]
    if os.geteuid() != 0:
        cmd.insert(0, "sudo")
        # The player runs as this user, and a process without root rights
        # can only attach to a TAP interface that belongs to its user
        if action == "start":
            cmd += ["user", pwd.getpwuid(os.geteuid()).pw_name]
    # nat.conf starts dnsmasq with a configuration file named relative to
    # the net-tools directory
    return subprocess.run(cmd, cwd=tools, stdout=subprocess.DEVNULL).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--net-tools", required=True, help="directory of net-setup.sh")
    parser.add_argument("exe", help="the native_sim program, zephyr.exe")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="its options")
    opts = parser.parse_args()

    tools = os.path.abspath(os.path.expanduser(opts.net_tools))
    created = False
    if not os.path.isdir("/sys/class/net/" + IFACE):
        if not os.path.isfile(os.path.join(tools, "net-setup.sh")):
            sys.exit("No net-setup.sh in %s: set ZEPHYR_NET_TOOLS to the net-tools "
                     "directory of Zephyr" % tools)
        print("Creating %s" % IFACE, flush=True)
        if net_setup(tools, "start") != 0 or not os.path.isdir("/sys/class/net/" + IFACE):
            sys.exit("Cannot create %s" % IFACE)
        created = True

    proc = None
    stop_asked_at = []

    def stop_player(signum, frame):
        if proc is not None and proc.poll() is None and not stop_asked_at:
            stop_asked_at.append(time.monotonic())
            proc.send_signal(signal.SIGINT)

    # Ctrl+C reaches the player from the terminal; this script waits for it
    # to end. Asked to stop itself, it stops the player first.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, stop_player)
    signal.signal(signal.SIGHUP, stop_player)

    status = 1
    lingered = False
    try:
        proc = subprocess.Popen([opts.exe] + opts.args,
                                preexec_fn=lambda: signal.signal(signal.SIGINT,
                                                                 signal.SIG_DFL))
        while True:
            try:
                status = proc.wait(timeout=0.2)
                break
            except subprocess.TimeoutExpired:
                # Asked to stop: the player is given some time, then killed
                if stop_asked_at and time.monotonic() - stop_asked_at[0] > STOP_TIMEOUT_S:
                    proc.kill()
                # It has ended, but threads of it linger
                elif main_thread_ended(proc.pid):
                    lingered = True
                    proc.kill()
    finally:
        if proc is not None and proc.poll() is None:
            proc.kill()
            proc.wait()
        if created:
            print("Removing %s" % IFACE, flush=True)
            net_setup(tools, "stop")

    if lingered:
        # Its own exit status is lost with the threads that were killed
        status = 0
    sys.exit(status if status >= 0 else 128 - status)


if __name__ == "__main__":
    main()
