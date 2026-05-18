#!/usr/bin/env python3
"""Wheel motor test: fwd 1s → stop 1s → back 1s → stop 1s, repeat."""

import serial
import time

PORT = "/dev/ttyUSB0"
BAUD = 115200

# flag bits: step[7:6] | left[5:4] | right[3:2] | 00
LEFT_FWD  = 0b10 << 4
LEFT_BACK = 0b01 << 4
RIGHT_FWD = 0b10 << 2
RIGHT_BACK= 0b01 << 2
STOP      = 0x00


def frame(flag):
    cs = ord("S") ^ ord("T") ^ flag
    return bytes([ord("S"), ord("T"), flag, cs, ord("E"), ord("D")])


def send(ser, flag, label=""):
    ser.write(frame(flag))
    ser.flush()
    reply = ser.read(2)
    if label:
        print(f"  {label:30s} flag=0x{flag:02x}  reply={reply!r}")
    return reply


ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.1)
print("Wheel motor test. Ctrl-C to stop.\n")

HOLD = 1.0

try:
    while True:
        print("--- Both forward ---")
        send(ser, LEFT_FWD | RIGHT_FWD, "both fwd")
        time.sleep(HOLD)

        print("--- Stop ---")
        send(ser, STOP, "stop")
        time.sleep(HOLD)

        print("--- Both backward ---")
        send(ser, LEFT_BACK | RIGHT_BACK, "both back")
        time.sleep(HOLD)

        print("--- Stop ---")
        send(ser, STOP, "stop")
        time.sleep(HOLD)

        print("--- Spin left (L-back, R-fwd) ---")
        send(ser, LEFT_BACK | RIGHT_FWD, "spin left")
        time.sleep(HOLD)

        print("--- Stop ---")
        send(ser, STOP, "stop")
        time.sleep(HOLD)

        print("--- Spin right (L-fwd, R-back) ---")
        send(ser, LEFT_FWD | RIGHT_BACK, "spin right")
        time.sleep(HOLD)

        print("--- Stop ---")
        send(ser, STOP, "stop")
        time.sleep(HOLD)

except KeyboardInterrupt:
    send(ser, STOP)

ser.close()
print("Done.")
