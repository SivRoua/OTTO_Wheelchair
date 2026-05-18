#!/usr/bin/env python3
"""Test all three motors: stepper + left/right wheels."""

import serial
import time

PORT = "/dev/ttyUSB0"
BAUD = 115200

STEP_FWD  = 0b10 << 6
STEP_BACK = 0b01 << 6
LEFT_FWD  = 0b10 << 4
LEFT_BACK = 0b01 << 4
RIGHT_FWD = 0b10 << 2
RIGHT_BACK= 0b01 << 2
STOP      = 0x00


def frame(flag):
    cs = ord("S") ^ ord("T") ^ flag
    return bytes([ord("S"), ord("T"), flag, cs, ord("E"), ord("D")])


def send(ser, flag):
    ser.write(frame(flag))
    ser.flush()
    reply = ser.read(2)
    if reply != b"OK":
        print(f"  [!] unexpected reply: {reply!r}")


def run(ser, flag, label, duration, step_interval=0.05):
    print(f"  {label}")
    t = time.time()
    while time.time() - t < duration:
        send(ser, flag)
        time.sleep(step_interval)


def stop(ser, duration=0.5):
    send(ser, STOP)
    time.sleep(duration)


ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.1)
print("=== All-motor test ===\n")

# --- Stepper ---
print("[Stepper]")
run(ser, STEP_FWD,  "forward  1s", 1.0)
stop(ser)
run(ser, STEP_BACK, "backward 1s", 1.0)
stop(ser)

# --- Left wheel ---
print("\n[Left wheel]")
run(ser, LEFT_FWD,  "forward  1s", 1.0)
stop(ser)
run(ser, LEFT_BACK, "backward 1s", 1.0)
stop(ser)

# --- Right wheel ---
print("\n[Right wheel]")
run(ser, RIGHT_FWD,  "forward  1s", 1.0)
stop(ser)
run(ser, RIGHT_BACK, "backward 1s", 1.0)
stop(ser)

# --- Both wheels together ---
print("\n[Both wheels]")
run(ser, LEFT_FWD  | RIGHT_FWD,  "both forward  1s", 1.0)
stop(ser)
run(ser, LEFT_BACK | RIGHT_BACK, "both backward 1s", 1.0)
stop(ser)
run(ser, LEFT_BACK | RIGHT_FWD,  "spin left     1s", 1.0)
stop(ser)
run(ser, LEFT_FWD  | RIGHT_BACK, "spin right    1s", 1.0)
stop(ser)

# --- All together ---
print("\n[All motors]")
run(ser, STEP_FWD | LEFT_FWD | RIGHT_FWD,   "all forward  1s", 1.0)
stop(ser)
run(ser, STEP_BACK | LEFT_BACK | RIGHT_BACK, "all backward 1s", 1.0)
stop(ser)

ser.close()
print("\nDone.")
