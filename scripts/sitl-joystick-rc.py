#!/usr/bin/env python3
"""
Feed a USB joystick/gamepad into Wingflight SITL as RC input, with a small
GUI for binding joystick axes/buttons/hats to RC channels.

Wingflight SITL (TARGET=SITL) defaults to FEATURE_RX_MSP, i.e. it expects RC
data over MSP (MSP_SET_RAW_RC) on its TCP MSP port (see
scripts/sitl-rc-check.ps1 for the same transport used by the RC test script).
This tool opens that same connection and streams live joystick state as RC
channels, continuously, instead of a scripted test sequence.

Requires: pygame (`pip install pygame`), used for both joystick input (SDL2)
and the mapping GUI. See docs/development/SITL Joystick RC Input.md for the
full setup guide.
"""

import argparse
import json
import os
import select
import socket
import sys
import threading
import time

# Keep reading the joystick while another window (FlightGear, the
# configurator) has focus; SDL drops joystick input for unfocused windows by
# default. Must be set before pygame/SDL initializes.
os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")
os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

try:
    import pygame
except ImportError:
    sys.exit("This tool requires pygame. Install it with: pip install pygame")

# --- MSP v1 protocol (matches scripts/sitl-rc-check.ps1) --------------------

MSP_API_VERSION = 1
MSP_SET_RAW_RC = 200

# RC channel order sent to the firmware MUST match the payload index that
# rxConfig()->rcmap resolves to for SITL's default rcmap ("AETR1234", see
# parseRcChannels() in src/main/rx/rx.c): payload[0]=Roll(A), [1]=Pitch(E),
# [2]=Throttle(T), [3]=Yaw(R), [4..]=AUX1.. Sending channels in any other
# order will scramble Throttle/Yaw/AUX1 on the firmware side.
CHANNEL_NAMES = ["Roll", "Pitch", "Throttle", "Yaw"] + [f"AUX{i}" for i in range(1, 15)]
DEFAULT_CHANNEL_COUNT = 8
MAX_CHANNEL_COUNT = len(CHANNEL_NAMES)

DEFAULT_US = {"Roll": 1500, "Pitch": 1500, "Throttle": 1000, "Yaw": 1500}


def default_us_for(name):
    return DEFAULT_US.get(name, 1500)


def send_msp(sock, cmd, payload=b""):
    length = len(payload)
    checksum = length ^ cmd
    for b in payload:
        checksum ^= b
    frame = bytes([0x24, 0x4D, 0x3C, length, cmd]) + payload + bytes([checksum & 0xFF])
    sock.sendall(frame)


def recv_msp(sock, timeout=1.0):
    """Blocking read of one MSP v1 response frame ($M> or $M!). None on timeout."""
    sock.settimeout(timeout)
    buf = bytearray()
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            idx = buf.find(b"$M")
            if idx != -1 and len(buf) >= idx + 5 and buf[idx + 2] in (0x3E, 0x21):
                length = buf[idx + 3]
                end = idx + 5 + length + 1
                if len(buf) >= end:
                    cmd = buf[idx + 4]
                    payload = bytes(buf[idx + 5:idx + 5 + length])
                    return cmd, payload
            elif idx != -1:
                del buf[:idx + 2]
                continue
            chunk = sock.recv(256)
            if not chunk:
                return None
            buf.extend(chunk)
    except socket.timeout:
        return None
    return None


def try_connect(host, port_candidates, explicit_port, timeout=0.5):
    """Try to open a persistent MSP TCP connection, verified with MSP_API_VERSION."""
    ports = [explicit_port] if explicit_port else list(port_candidates)
    for port in ports:
        try:
            sock = socket.create_connection((host, port), timeout=timeout)
        except OSError:
            continue
        try:
            send_msp(sock, MSP_API_VERSION)
            resp = recv_msp(sock, timeout=timeout)
            if resp is not None:
                sock.settimeout(None)
                return sock, port
            sock.close()
        except OSError:
            try:
                sock.close()
            except OSError:
                pass
    return None, 0


class MspLink(threading.Thread):
    """Owns the SITL MSP socket: (re)connects and streams RC frames.

    Runs off the GUI thread because connecting blocks: a refused or silent
    candidate port costs up to the connect/MSP timeout each attempt, which
    froze the GUI (and joystick polling) for ~1 s out of every second.
    """

    def __init__(self, host, port_candidates, explicit_port, rate, neutral):
        super().__init__(daemon=True)
        self.host = host
        self.port_candidates = port_candidates
        self.explicit_port = explicit_port
        self.interval = 1.0 / rate
        self.neutral = list(neutral)
        self.outputs = list(neutral)
        self.port = 0
        self.lock = threading.Lock()
        self.stop_event = threading.Event()

    def set_outputs(self, outputs):
        with self.lock:
            self.outputs = list(outputs)

    @property
    def connected(self):
        return self.port != 0

    def stop(self):
        self.stop_event.set()
        self.join(timeout=2.0)

    def run(self):
        sock = None
        next_send = time.monotonic()
        while not self.stop_event.is_set():
            if sock is None:
                sock, port = try_connect(self.host, self.port_candidates, self.explicit_port)
                if sock is None:
                    self.stop_event.wait(1.0)
                    continue
                sock.settimeout(0.5)
                self.port = port
                print(f"[joystick-rc] Connected to SITL MSP on {self.host}:{port}")
            try:
                # discard replies so SITL's send buffer never fills up
                while select.select([sock], [], [], 0)[0]:
                    if not sock.recv(4096):
                        raise OSError("connection closed")
                with self.lock:
                    outputs = self.outputs
                send_msp(sock, MSP_SET_RAW_RC, pack_channels(outputs))
            except OSError:
                print("[joystick-rc] Lost connection to SITL, will retry")
                sock.close()
                sock = None
                self.port = 0
                continue
            next_send += self.interval
            delay = next_send - time.monotonic()
            if delay < 0:
                next_send = time.monotonic()
                delay = 0
            self.stop_event.wait(delay)

        if sock is not None:
            for _ in range(5):
                try:
                    send_msp(sock, MSP_SET_RAW_RC, pack_channels(self.neutral))
                except OSError:
                    break
                time.sleep(0.02)
            sock.close()
        self.port = 0


# --- Joystick selection --------------------------------------------------------

def open_all_joysticks():
    joysticks = []
    for i in range(pygame.joystick.get_count()):
        j = pygame.joystick.Joystick(i)
        j.init()
        joysticks.append(j)
    return joysticks


def describe_joystick(i, j):
    return (f"[{i}] {j.get_name()} (axes={j.get_numaxes()} buttons={j.get_numbuttons()} "
            f"hats={j.get_numhats()} guid={j.get_guid()})")


def select_joystick(joysticks, spec, saved_name):
    """Pick a device index from --joystick (index or name substring) or the saved mapping.

    Returns the index, or raises SystemExit with the device list when the
    request can't be matched.
    """
    listing = "\n".join(describe_joystick(i, j) for i, j in enumerate(joysticks))
    if spec is not None:
        if spec.isdigit():
            idx = int(spec)
            if idx < len(joysticks):
                return idx
            sys.exit(f"--joystick {idx}: no such device. Detected:\n{listing}")
        matches = [i for i, j in enumerate(joysticks) if spec.lower() in j.get_name().lower()]
        if not matches:
            sys.exit(f"--joystick '{spec}': no device name contains that. Detected:\n{listing}")
        if len(matches) > 1:
            print(f"[joystick-rc] --joystick '{spec}' matches {len(matches)} devices, using [{matches[0]}]")
        return matches[0]
    if saved_name:
        for i, j in enumerate(joysticks):
            if j.get_name() == saved_name:
                return i
        print(f"[joystick-rc] Saved mapping device '{saved_name}' not found, using [0]")
    return 0


# --- Mapping model -----------------------------------------------------------

def pack_channels(values_us):
    payload = bytearray()
    for v in values_us:
        v = int(max(1000, min(2000, round(v))))
        payload += v.to_bytes(2, "little")
    return bytes(payload)


def read_source_value(source, joystick):
    """Return the raw value of a bound source, normalized to [-1, 1]."""
    if source is None:
        return None
    kind = source["type"]
    index = source["index"]
    # a mapping saved for a different device may reference controls this one lacks
    if kind == "axis":
        return joystick.get_axis(index) if index < joystick.get_numaxes() else None
    if kind == "button":
        if index >= joystick.get_numbuttons():
            return None
        return 1.0 if joystick.get_button(index) else -1.0
    if kind in ("hat_x", "hat_y"):
        if index >= joystick.get_numhats():
            return None
        hat = joystick.get_hat(index)
        return float(hat[0] if kind == "hat_x" else hat[1])
    return None


def compute_us(name, value):
    if value is None:
        return default_us_for(name)
    cfg = value["config"]
    raw = value["raw"]
    if cfg.get("invert"):
        raw = -raw
    deadband = cfg.get("deadband", 0.0)
    if abs(raw) < deadband:
        raw = 0.0
    if raw >= 0:
        us = 1500 + raw * (2000 - 1500)
    else:
        us = 1500 + raw * (1500 - 1000)
    return max(1000, min(2000, us))


def source_label(source):
    if source is None:
        return "-- unassigned --"
    kind = source["type"]
    if kind == "axis":
        return f"Axis {source['index']}" + (" (inv)" if source.get("invert") else "")
    if kind == "button":
        return f"Button {source['index']}"
    if kind == "hat_x":
        return f"Hat {source['index']} X"
    if kind == "hat_y":
        return f"Hat {source['index']} Y"
    return "?"


def load_mapping(path, channel_names):
    """Return (mapping, saved joystick name or None)."""
    mapping = {name: None for name in channel_names}
    saved_name = None
    if path and os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        saved_name = data.get("joystick_name")
        for ch in data.get("channels", []):
            if ch.get("name") in mapping:
                mapping[ch["name"]] = ch.get("source")
    return mapping, saved_name


def save_mapping(path, channel_names, mapping, joystick_name):
    data = {
        "joystick_name": joystick_name,
        "channels": [{"name": name, "source": mapping[name]} for name in channel_names],
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


# --- GUI ---------------------------------------------------------------------

BIND_AXIS_THRESHOLD = 0.5
WIDTH, HEIGHT = 760, 560
ROW_H = 34
ROWS_TOP = 70


def run(args):
    pygame.init()
    pygame.joystick.init()

    joysticks = open_all_joysticks()
    if not joysticks:
        sys.exit("No joystick/gamepad detected. Plug one in and try again.")
    if args.list_joysticks:
        for i, j in enumerate(joysticks):
            print(describe_joystick(i, j))
        return

    channel_names = CHANNEL_NAMES[: args.channels]
    mapping, saved_name = load_mapping(args.mapping, channel_names)
    joy_index = select_joystick(joysticks, args.joystick, saved_name)
    joystick = joysticks[joy_index]
    current_id = (joystick.get_guid(), joystick.get_name())
    print(f"[joystick-rc] Using {describe_joystick(joy_index, joystick)}")

    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Wingflight SITL Joystick RC Bridge")
    font = pygame.font.SysFont("consolas,couriernew,monospace", 16)
    small = pygame.font.SysFont("consolas,couriernew,monospace", 13)
    clock = pygame.time.Clock()

    port_candidates = [int(p) for p in args.port_candidates.split(",") if p]
    link = MspLink(args.host, port_candidates, args.port, args.rate,
                   [default_us_for(n) for n in channel_names])
    link.start()

    bind_channel = None
    bind_initial = None

    def layout():
        rects = {"rows": {}}
        rects["joy_prev"] = pygame.Rect(WIDTH - 90, 12, 32, 24)
        rects["joy_next"] = pygame.Rect(WIDTH - 50, 12, 32, 24)
        y = ROWS_TOP
        for name in channel_names:
            rects["rows"][name] = {
                "bind": pygame.Rect(430, y, 70, ROW_H - 8),
                "invert": pygame.Rect(510, y, ROW_H - 8, ROW_H - 8),
                "clear": pygame.Rect(555, y, 60, ROW_H - 8),
            }
            y += ROW_H
        rects["save"] = pygame.Rect(20, HEIGHT - 40, 100, 28)
        rects["load"] = pygame.Rect(130, HEIGHT - 40, 100, 28)
        rects["quit"] = pygame.Rect(240, HEIGHT - 40, 100, 28)
        return rects

    rects = layout()

    def switch_joystick(new_index):
        nonlocal joy_index, joystick, current_id, bind_channel
        joy_index = new_index % len(joysticks)
        joystick = joysticks[joy_index]
        current_id = (joystick.get_guid(), joystick.get_name())
        bind_channel = None
        print(f"[joystick-rc] Using {describe_joystick(joy_index, joystick)}")

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type in (pygame.JOYDEVICEADDED, pygame.JOYDEVICEREMOVED):
                # hotplug: re-enumerate, staying on the same device if it's still there
                new_list = open_all_joysticks()
                if len(new_list) != len(joysticks) or event.type == pygame.JOYDEVICEREMOVED:
                    joysticks = new_list
                    if not joysticks:
                        print("[joystick-rc] All joysticks removed")
                        running = False
                        break
                    same = [i for i, j in enumerate(joysticks)
                            if (j.get_guid(), j.get_name()) == current_id]
                    switch_joystick(same[0] if same else 0)
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                bind_channel = None
            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                pos = event.pos
                if rects["quit"].collidepoint(pos):
                    running = False
                elif rects["joy_prev"].collidepoint(pos):
                    switch_joystick(joy_index - 1)
                elif rects["joy_next"].collidepoint(pos):
                    switch_joystick(joy_index + 1)
                elif rects["save"].collidepoint(pos):
                    save_mapping(args.mapping, channel_names, mapping, joystick.get_name())
                    print(f"[joystick-rc] Saved mapping to {args.mapping}")
                elif rects["load"].collidepoint(pos):
                    mapping, _ = load_mapping(args.mapping, channel_names)
                    print(f"[joystick-rc] Reloaded mapping from {args.mapping}")
                else:
                    for name, row in rects["rows"].items():
                        if row["bind"].collidepoint(pos):
                            bind_channel = name
                            bind_initial = {
                                "axes": [joystick.get_axis(i) for i in range(joystick.get_numaxes())],
                                "buttons": [joystick.get_button(i) for i in range(joystick.get_numbuttons())],
                                "hats": [joystick.get_hat(i) for i in range(joystick.get_numhats())],
                            }
                        elif row["clear"].collidepoint(pos):
                            mapping[name] = None
                            if bind_channel == name:
                                bind_channel = None
                        elif row["invert"].collidepoint(pos) and mapping[name] is not None:
                            mapping[name]["invert"] = not mapping[name].get("invert", False)

        # live bind-mode capture: move a control to bind it to bind_channel
        if bind_channel is not None:
            for i in range(joystick.get_numaxes()):
                if abs(joystick.get_axis(i) - bind_initial["axes"][i]) > BIND_AXIS_THRESHOLD:
                    mapping[bind_channel] = {"type": "axis", "index": i, "invert": False, "deadband": 0.02}
                    bind_channel = None
                    break
        if bind_channel is not None:
            for i in range(joystick.get_numbuttons()):
                if joystick.get_button(i) and not bind_initial["buttons"][i]:
                    mapping[bind_channel] = {"type": "button", "index": i}
                    bind_channel = None
                    break
        if bind_channel is not None:
            for i in range(joystick.get_numhats()):
                hat = joystick.get_hat(i)
                prev = bind_initial["hats"][i]
                if hat[0] != prev[0] and hat[0] != 0:
                    mapping[bind_channel] = {"type": "hat_x", "index": i}
                    bind_channel = None
                    break
                if hat[1] != prev[1] and hat[1] != 0:
                    mapping[bind_channel] = {"type": "hat_y", "index": i}
                    bind_channel = None
                    break

        # compute channel outputs; the link thread sends the latest set
        outputs = []
        for name in channel_names:
            source = mapping[name]
            raw = read_source_value(source, joystick)
            us = compute_us(name, None if raw is None else {"raw": raw, "config": source})
            outputs.append(us)
        link.set_outputs(outputs)

        # --- draw ---
        screen.fill((24, 24, 28))
        status = f"Joystick [{joy_index + 1}/{len(joysticks)}]: {joystick.get_name()}"
        port = link.port
        conn = f"SITL MSP: connected on port {port}" if port else "SITL MSP: disconnected (retrying...)"
        screen.blit(font.render(status, True, (230, 230, 230)), (20, 15))
        screen.blit(font.render(conn, True, (120, 220, 120) if port else (220, 140, 90)), (20, 38))
        for key, label in (("joy_prev", "<"), ("joy_next", ">")):
            pygame.draw.rect(screen, (70, 130, 180), rects[key])
            screen.blit(font.render(label, True, (0, 0, 0)), (rects[key].x + 11, rects[key].y + 3))

        y = ROWS_TOP
        for name, us in zip(channel_names, outputs):
            source = mapping[name]
            binding = bind_channel == name
            color = (255, 210, 90) if binding else (210, 210, 210)
            screen.blit(font.render(f"{name:<9}", True, color), (20, y + 4))

            bar_x, bar_w = 130, 220
            pygame.draw.rect(screen, (60, 60, 66), (bar_x, y + 6, bar_w, 16))
            frac = (us - 1000) / 1000.0
            pygame.draw.rect(screen, (90, 160, 220), (bar_x, y + 6, int(bar_w * frac), 16))
            screen.blit(small.render(f"{int(us)}", True, (255, 255, 255)), (bar_x + bar_w + 6, y + 6))

            row = rects["rows"][name]
            bind_color = (255, 210, 90) if binding else (70, 130, 180)
            pygame.draw.rect(screen, bind_color, row["bind"])
            screen.blit(small.render("bind" if not binding else "...", True, (0, 0, 0)), (row["bind"].x + 15, row["bind"].y + 5))

            inv_on = source is not None and source.get("invert")
            pygame.draw.rect(screen, (200, 90, 90) if inv_on else (70, 70, 76), row["invert"])
            screen.blit(small.render("I", True, (255, 255, 255)), (row["invert"].x + 6, row["invert"].y + 4))

            pygame.draw.rect(screen, (90, 90, 96), row["clear"])
            screen.blit(small.render("clear", True, (255, 255, 255)), (row["clear"].x + 6, row["clear"].y + 5))

            screen.blit(small.render(source_label(source), True, (170, 170, 170)), (630, y + 8))
            y += ROW_H

        for key, label in (("save", "Save"), ("load", "Load"), ("quit", "Quit")):
            pygame.draw.rect(screen, (80, 120, 80) if key != "quit" else (140, 70, 70), rects[key])
            screen.blit(small.render(label, True, (255, 255, 255)), (rects[key].x + 30, rects[key].y + 7))

        hint = "Click 'bind' then move an axis / press a button / tap a hat. Esc cancels."
        screen.blit(small.render(hint, True, (150, 150, 150)), (20, HEIGHT - 70))

        pygame.display.flip()
        clock.tick(60)

    link.stop()  # sends neutral frames before closing
    pygame.quit()


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1", help="SITL MSP host (default: 127.0.0.1)")
    p.add_argument("--port", type=int, default=0, help="Explicit MSP TCP port (default: auto-detect)")
    p.add_argument("--port-candidates", default="5760,5761", help="Comma-separated ports to try when --port is 0")
    p.add_argument("--rate", type=float, default=50.0, help="RC frame send rate in Hz (default: 50)")
    p.add_argument("--joystick", default=None,
                   help="Joystick index or case-insensitive name substring, e.g. 'FrSky' "
                        "(default: the device named in the mapping file, else index 0)")
    p.add_argument("--list-joysticks", action="store_true", help="List detected joysticks and exit")
    p.add_argument("--channels", type=int, default=DEFAULT_CHANNEL_COUNT,
                    help=f"Number of RC channels to send, 4-{MAX_CHANNEL_COUNT} (default: {DEFAULT_CHANNEL_COUNT})")
    p.add_argument("--mapping", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "sitl-joystick-mapping.json"),
                    help="Path to the channel-mapping JSON file (default: scripts/sitl-joystick-mapping.json)")
    args = p.parse_args()
    args.channels = max(4, min(MAX_CHANNEL_COUNT, args.channels))
    return args


if __name__ == "__main__":
    run(parse_args())
