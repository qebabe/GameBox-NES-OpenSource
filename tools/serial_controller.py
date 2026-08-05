#!/usr/bin/env python3
"""Keyboard-to-serial controller for DIJI-NES.

Install dependencies:
    python3 -m pip install pyserial pynput

Run:
    python3 tools/serial_controller.py --port /dev/cu.usbmodemXXXX
"""

import argparse
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Missing pyserial. Install with: python3 -m pip install pyserial pynput")
    sys.exit(1)

try:
    from pynput import keyboard
except ImportError:
    print("Missing pynput. Install with: python3 -m pip install pyserial pynput")
    sys.exit(1)


BTN_A = 0x01
BTN_B = 0x02
BTN_SELECT = 0x04
BTN_START = 0x08
BTN_UP = 0x10
BTN_DOWN = 0x20
BTN_LEFT = 0x40
BTN_RIGHT = 0x80


CHAR_KEY_MAP = {
    "w": BTN_UP,
    "s": BTN_DOWN,
    "a": BTN_LEFT,
    "d": BTN_RIGHT,
    "o": BTN_A,
    "p": BTN_B,
}

SPECIAL_KEY_MAP = {
    keyboard.Key.enter: BTN_START,
    keyboard.Key.backspace: BTN_SELECT,
    keyboard.Key.shift_r: BTN_SELECT,
    keyboard.Key.esc: BTN_START | BTN_SELECT,
}


def autodetect_port():
    candidates = []
    for port in list_ports.comports():
        text = " ".join(
            str(value or "")
            for value in (port.device, port.description, port.manufacturer)
        ).lower()
        if "usb" in text or "wch" in text or "cp210" in text or "serial" in text:
            candidates.append(port.device)

    if len(candidates) == 1:
        return candidates[0]
    return None


def key_to_mask(key):
    if key in SPECIAL_KEY_MAP:
        return SPECIAL_KEY_MAP[key]

    char = getattr(key, "char", None)
    if not char:
        return 0
    return CHAR_KEY_MAP.get(char.lower(), 0)


def main():
    parser = argparse.ArgumentParser(description="DIJI-NES serial keyboard controller")
    parser.add_argument("--port", help="Serial port, for example /dev/cu.usbmodem101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--interval", type=float, default=0.02, help="Send interval in seconds")
    parser.add_argument(
        "--no-monitor",
        action="store_true",
        help="Do not print serial output from the board",
    )
    args = parser.parse_args()

    port = args.port or autodetect_port()
    if not port:
        print("Could not auto-detect a serial port.")
        print("Available ports:")
        for item in list_ports.comports():
            print(f"  {item.device}  {item.description}")
        print("Run again with: python3 tools/serial_controller.py --port <PORT>")
        return 2

    state = 0
    state_lock = threading.Lock()
    print_lock = threading.Lock()
    running = True
    pending_commands = []

    def log(message):
        with print_lock:
            print(message, flush=True)

    def on_press(key):
        nonlocal state, running
        mask = key_to_mask(key)
        if mask:
            with state_lock:
                state |= mask
            return

        char = getattr(key, "char", None)
        if char and char.lower() == "t":
            with state_lock:
                pending_commands.append("C:AUDIO\n")
            return
        if char and char.lower() == "f":
            with state_lock:
                pending_commands.append("C:DISPLAY\n")
            return
        if char and char.lower() == "c":
            with state_lock:
                pending_commands.append("C:TOUCHCAL\n")
            return
        if char and char.lower() == "q":
            running = False
            return False

    def on_release(key):
        nonlocal state
        mask = key_to_mask(key)
        if mask:
            with state_lock:
                state &= ~mask

    def serial_monitor(ser, stop_event):
        buffer = bytearray()
        while not stop_event.is_set():
            waiting = ser.in_waiting
            if waiting:
                buffer.extend(ser.read(waiting))
                while b"\n" in buffer:
                    line, _, buffer = buffer.partition(b"\n")
                    text = line.decode("utf-8", errors="replace").rstrip("\r")
                    if text:
                        log(f"[serial] {text}")
            else:
                time.sleep(0.01)

        if buffer:
            text = buffer.decode("utf-8", errors="replace").rstrip("\r\n")
            if text:
                log(f"[serial] {text}")

    log("DIJI-NES serial controller")
    log(f"Port: {port}, baud: {args.baud}")
    log("Keys: WASD=direction, O=A, P=B, Enter=START, Backspace/RightShift=SELECT, Esc=START+SELECT")
    log("Commands: T=audio test, F=toggle fullscreen/native display, C=touch calibration, Q=quit")
    log("Serial monitor: enabled" if not args.no_monitor else "Serial monitor: disabled")
    log("On macOS, allow Terminal/Codex accessibility permission if key capture is blocked.")

    with serial.Serial(port, args.baud, timeout=0) as ser:
        stop_monitor = threading.Event()
        monitor_thread = None
        if not args.no_monitor:
            monitor_thread = threading.Thread(
                target=serial_monitor,
                args=(ser, stop_monitor),
                daemon=True,
            )
            monitor_thread.start()

        time.sleep(1.0)
        try:
            with keyboard.Listener(on_press=on_press, on_release=on_release) as listener:
                last_sent = None
                while running and listener.running:
                    with state_lock:
                        current = state
                        commands = pending_commands[:]
                        pending_commands.clear()
                    for command in commands:
                        ser.write(command.encode("ascii"))
                        log(f"[tx] {command.strip()}")
                    ser.write(f"K:{current:02X}\n".encode("ascii"))
                    if current != last_sent:
                        log(f"[tx] state=0x{current:02X}")
                        last_sent = current
                    time.sleep(args.interval)
        finally:
            ser.write(b"K:00\n")
            stop_monitor.set()
            if monitor_thread:
                monitor_thread.join(timeout=0.5)

    return 0


if __name__ == "__main__":
    sys.exit(main())
