#!/usr/bin/env python3
"""Publish explicitly simulated monitor snapshots; never access radio hardware.

This is a development fixture for the separate passive monitor, not a live
test_app adapter. Existing socket paths are never removed on startup.
"""

import argparse
import json
import math
import os
from pathlib import Path
import selectors
import socket
import stat
import sys
import time

from cariboulite_monitor import MAX_PACKET_BYTES, default_socket_path, positive_seconds
from at86rf215_monitor import COMMON_REGISTERS, RADIO_REGISTERS


def simulated_snapshot(sequence, session, channel="RF24"):
    now = time.monotonic()
    noise = 0.16 + 0.08 * (1 + math.sin(sequence / 15))
    rx_queue = dict(capacity=64, depth=2, puts=sequence * 20,
                    gets=sequence * 20 - 2, drops=0, timeouts_put=0,
                    timeouts_get=0, min_depth=0, max_depth=3)
    tx_queue = dict(capacity=64, depth=0, puts=0, gets=0, drops=0,
                    timeouts_put=0, timeouts_get=0, min_depth=0, max_depth=0)
    # These are illustrative virtual register bytes, not hardware readbacks.
    common = dict.fromkeys(COMMON_REGISTERS, 0)
    common.update(IQIFC0=0x14, IQIFC1=0x12, PN=0x34, VN=0x01)
    radio = dict.fromkeys(RADIO_REGISTERS, 0)
    radio.update(IRQM=0x3F, STATE=5, CMD=5, RXBWC=0x10, RXDFE=4,
                 AGCC=1, AGCS=21, RSSI=(-110 & 255), EDV=(-110 & 255),
                 EDC=3, TXDFE=4, PAC=0x60, PLL=2, RNDV=sequence & 255)
    active = "rf09" if channel == "RF09" else "rf24"
    inactive = "rf24" if active == "rf09" else "rf09"
    return {
        "version": 1, "source": "SIMULATED DATA — no radio connection",
        "session": session, "sequence": sequence, "owner_pid": os.getpid(),
        "monotonic_s": now, "realtime_s": time.time(), "hardware_monotonic_s": now,
        "channel": "S1G/RF09" if active == "rf09" else "HiF/RF24",
        "state": "rx", "rx_mode": "NBFM",
        "config": dict(tx_frequency_hz=430125000, rx_frequency_hz=430125000,
                       sample_rate_hz=1000000, tx_power_dbm=-13,
                       rx_bandwidth_hz=160000, tx_bandwidth_hz=80000),
        "rx": dict(running=True, queue=rx_queue, noise_enabled=True,
                   carrier_enabled=False, gate_open=noise < .25,
                   noise=dict(valid=True, rms=noise, open_rms=.2,
                              close_rms=.3, detector_open=noise < .25)),
        "tx": dict(running=False, queue=tx_queue),
        "hardware": {
            "common": dict(valid=True, registers=common),
            active: dict(valid=True, registers=radio),
            inactive: dict(valid=False),
            "fpga": dict(valid=True, raw_status=8, io_control=1, debug=0),
        },
        "loopback": dict(armed=False, active=False, samples=0, timeouts=0),
    }


def prepare_parent(path):
    parent = path.parent
    parent.mkdir(mode=0o700, exist_ok=True)
    info = parent.lstat()
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid()
            or stat.S_IMODE(info.st_mode) != 0o700):
        raise ValueError("socket parent must be a real, owned directory with mode 0700")
    if path.exists() or path.is_symlink():
        raise ValueError(f"socket path already exists: {path}; choose another --socket path")


def run_source(path, seconds=None, channel="RF24"):
    """Bounded, nonblocking test publisher; clients cannot issue commands."""
    prepare_parent(path)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    selector = selectors.DefaultSelector()
    clients = set()
    owned_inode = None
    try:
        server.bind(str(path))
        owned_inode = path.lstat().st_ino
        path.chmod(0o600)
        server.listen(8)
        server.setblocking(False)
        selector.register(server, selectors.EVENT_READ)
        started = time.monotonic()
        deadline = started + seconds if seconds is not None else None
        session = f"demo-{os.getpid()}-{time.monotonic_ns()}"
        next_update = started
        sequence = 0
        latest = None
        print(f"SIMULATED DATA only; no radio connection. Socket: {path}",
              file=sys.stderr, flush=True)

        def remove(client):
            selector.unregister(client)
            clients.remove(client)
            client.close()

        def send(client, packet):
            try:
                client.send(packet)
            except BlockingIOError:
                pass  # A slow monitor may miss this simulated display update.
            except OSError:
                remove(client)

        while deadline is None or time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_update:
                sequence += 1
                latest = json.dumps(simulated_snapshot(sequence, session, channel),
                                    separators=(",", ":"), allow_nan=False).encode()
                if len(latest) > MAX_PACKET_BYTES:
                    raise ValueError("simulated snapshot exceeds the packet limit")
                for client in tuple(clients):
                    send(client, latest)
                next_update = now + .2
            wait = max(0, min(.2, next_update - time.monotonic()))
            if deadline is not None:
                wait = max(0, min(wait, deadline - time.monotonic()))
            for key, _ in selector.select(wait):
                if key.fileobj is server:
                    try:
                        client, _ = server.accept()
                    except BlockingIOError:
                        continue
                    if len(clients) >= 8:
                        client.close()
                        continue
                    client.setblocking(False)
                    client.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 16384)
                    clients.add(client)
                    selector.register(client, selectors.EVENT_READ)
                    if latest is not None:
                        send(client, latest)
                else:
                    # Incoming data and EOF both end this receive-only session.
                    try:
                        key.fileobj.recv(1)
                    except BlockingIOError:
                        continue
                    except OSError:
                        pass
                    remove(key.fileobj)
    finally:
        for client in tuple(clients):
            client.close()
        selector.close()
        server.close()
        try:
            info = path.lstat()
            if stat.S_ISSOCK(info.st_mode) and info.st_ino == owned_inode:
                path.unlink()
        except FileNotFoundError:
            pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=default_socket_path(), metavar="PATH")
    parser.add_argument("--seconds", type=positive_seconds, metavar="SECONDS",
                        help="stop automatically after this duration")
    parser.add_argument("--channel", choices=("RF09", "RF24"), default="RF24",
                        help="simulated active radio (default: RF24)")
    args = parser.parse_args(argv)
    try:
        run_source(Path(args.socket), args.seconds, args.channel)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError) as exc:
        print(f"Demo source: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
