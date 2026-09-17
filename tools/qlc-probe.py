#!/usr/bin/env python3
"""Talk to a QLC+ 5.2.2 web interface the way the tablet desk will.

This is the harness for the first desk experiment: it reads the Virtual
Console, sends the frames the desk sends, and prints every frame that comes
back with the milliseconds it took. No dependencies beyond the standard
library, because it also has to run from a rescue shell.

    tools/qlc-probe.py --host 192.168.2.1 vc
    tools/qlc-probe.py --host 192.168.2.1 widget 98
    tools/qlc-probe.py send '98|255' '98|0'
    tools/qlc-probe.py rtt --seconds 30

Nothing here writes to QLC+'s project. `send` does drive the rig, so unplug
the DMX interface before pointing it at a show machine.
"""

import argparse
import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.request

TYPE_NAMES = {
    1: "Button", 2: "Slider", 3: "XYPad", 4: "Frame", 5: "SoloFrame",
    6: "SpeedDial", 8: "Label", 9: "AudioTriggers", 10: "Animation",
}
ACTION_NAMES = {0: "Toggle", 1: "Flash", 2: "Blackout", 3: "StopAll"}


class Socket:
    """A WebSocket text client: handshake, masked sends, frame reads."""

    def __init__(self, host, port, path="/qlcplusWS", timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.send(
            f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\n"
            f"Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
            .encode())
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed during the handshake")
            self.buf += chunk
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        if b"101" not in head.split(b"\r\n")[0]:
            raise ConnectionError(head.decode(errors="replace"))

    def send(self, text):
        data = text.encode()
        mask = os.urandom(4)
        n = len(data)
        if n < 126:
            header = bytes([0x81, 0x80 | n])
        elif n < 65536:
            header = bytes([0x81, 0x80 | 126]) + struct.pack(">H", n)
        else:
            header = bytes([0x81, 0x80 | 127]) + struct.pack(">Q", n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self.sock.send(header + mask + masked)

    def frames(self, until):
        """Yield (timestamp, text) for every frame arriving before `until`."""
        self.sock.settimeout(0.2)
        while time.monotonic() < until:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    return
                self.buf += chunk
            except socket.timeout:
                pass
            while True:
                if len(self.buf) < 2:
                    break
                op, length, offset = self.buf[0] & 0x0F, self.buf[1] & 0x7F, 2
                if length == 126:
                    if len(self.buf) < 4:
                        break
                    length, offset = struct.unpack(">H", self.buf[2:4])[0], 4
                elif length == 127:
                    if len(self.buf) < 10:
                        break
                    length, offset = struct.unpack(">Q", self.buf[2:10])[0], 10
                if len(self.buf) < offset + length:
                    break
                payload = self.buf[offset:offset + length]
                self.buf = self.buf[offset + length:]
                if op == 8:
                    return
                if op == 1:
                    yield time.monotonic(), payload.decode(errors="replace")

    def close(self):
        self.sock.close()


def fetch_vc(host, port):
    url = f"http://{host}:{port}/vc.json?ts={int(time.time())}"
    with urllib.request.urlopen(url, timeout=5) as response:
        return json.load(response)


def widgets(vc):
    """Every widget, flattened, each with the id of the frame holding it."""
    out = []

    def walk(node, parent):
        out.append((node, parent))
        for child in node.get("children") or []:
            walk(child, node.get("id"))

    for page in vc.get("pages", []):
        walk(page, None)
    return out


def describe(node, parent):
    type_id = node.get("typeId")
    bits = [
        f"id={node.get('id')}",
        f"type={TYPE_NAMES.get(type_id, type_id)}",
    ]
    if node.get("caption"):
        bits.append(f"caption={node['caption']!r}")
    if node.get("functionId") is not None:
        bits.append(f"function={node['functionId']}")
    if type_id == 1:
        action = node.get("actionType", 0)
        bits.append(f"action={ACTION_NAMES.get(action, action)}")
    if parent is not None:
        bits.append(f"in={parent}")
    return "  ".join(bits)


def cmd_vc(args):
    vc = fetch_vc(args.host, args.port)
    app = vc.get("app", {})
    print(f"{app.get('name')} {app.get('version')}  pages={len(vc.get('pages', []))}")
    counts = {}
    for node, _ in widgets(vc):
        name = TYPE_NAMES.get(node.get("typeId"), node.get("typeId"))
        counts[name] = counts.get(name, 0) + 1
    print("  ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    flash = [describe(n, p) for n, p in widgets(vc)
             if n.get("typeId") == 1 and n.get("actionType") == 1]
    if flash:
        print("\nFlash buttons (held from the tablet, these cannot be made safe):")
        for line in flash:
            print("  " + line)
    return 0


def cmd_widget(args):
    vc = fetch_vc(args.host, args.port)
    for node, parent in widgets(vc):
        if node.get("id") == args.id:
            print(describe(node, parent))
            print(json.dumps({k: v for k, v in node.items() if k != "children"},
                             indent=2, ensure_ascii=False))
            return 0
    print(f"no widget {args.id} in the loaded workspace", file=sys.stderr)
    return 1


def cmd_send(args):
    link = Socket(args.host, args.port)
    try:
        for message in args.message:
            sent = time.monotonic()
            print(f">> {message}")
            link.send(message)
            for at, text in link.frames(sent + args.wait):
                print(f"<< {(at - sent) * 1000:7.1f} ms  {text}")
    finally:
        link.close()
    return 0


def cmd_rtt(args):
    """Round trip of a query the engine always answers, as a latency floor."""
    link = Socket(args.host, args.port)
    samples = []
    try:
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            sent = time.monotonic()
            link.send("QLC+API|isProjectLoaded")
            for at, text in link.frames(sent + 2.0):
                if text.startswith("QLC+API|isProjectLoaded"):
                    samples.append((at - sent) * 1000)
                    break
            time.sleep(args.interval)
    finally:
        link.close()
    if not samples:
        print("no replies", file=sys.stderr)
        return 1
    samples.sort()
    def pct(p):
        return samples[min(len(samples) - 1, int(len(samples) * p / 100))]
    print(f"n={len(samples)}  p50={pct(50):.1f} ms  p95={pct(95):.1f} ms  "
          f"p99={pct(99):.1f} ms  max={samples[-1]:.1f} ms")
    return 0


def cmd_watch(args):
    link = Socket(args.host, args.port)
    start = time.monotonic()
    try:
        for at, text in link.frames(start + args.seconds):
            print(f"{at - start:8.3f}  {text}")
    finally:
        link.close()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("vc", help="summarise the loaded Virtual Console").set_defaults(run=cmd_vc)

    one = sub.add_parser("widget", help="print one widget's record")
    one.add_argument("id", type=int)
    one.set_defaults(run=cmd_widget)

    send = sub.add_parser("send", help="send frames and print what comes back")
    send.add_argument("message", nargs="+")
    send.add_argument("--wait", type=float, default=1.0,
                      help="seconds to listen after each message")
    send.set_defaults(run=cmd_send)

    rtt = sub.add_parser("rtt", help="measure application round trip")
    rtt.add_argument("--seconds", type=float, default=30.0)
    rtt.add_argument("--interval", type=float, default=0.1)
    rtt.set_defaults(run=cmd_rtt)

    watch = sub.add_parser("watch", help="print pushes as they arrive")
    watch.add_argument("--seconds", type=float, default=60.0)
    watch.set_defaults(run=cmd_watch)

    args = parser.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
