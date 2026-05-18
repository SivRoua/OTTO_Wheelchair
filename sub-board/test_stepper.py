#!/usr/bin/env python3
"""Stepper: forward 1s, stop 1s, backward 1s, repeat."""

import serial
import time

PORT = "/dev/ttyUSB0"
BAUD = 115200

STEP_FWD  = 0b10 << 6
STEP_BACK = 0b01 << 6
STEP_STOP = 0b00


def frame(flag):
    cs = ord("S") ^ ord("T") ^ flag
    return bytes([ord("S"), ord("T"), flag, cs, ord("E"), ord("D")])


def send(ser, flag):
    ser.write(frame(flag))
    ser.flush()
    ser.read(2)  # consume "OK"


ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.1)
print("Stepper: fwd 1s → stop 1s → back 1s → repeat. Ctrl-C to stop.")

STEP_INTERVAL = 0.05  # 20 steps/sec

try:
    while True:
        # Forward 1s
        t = time.time()
        while time.time() - t < 1.0:
            send(ser, STEP_FWD)
            time.sleep(STEP_INTERVAL)

        # Stop 1s
        send(ser, STEP_STOP)
        time.sleep(1.0)

        # Backward 1s
        t = time.time()
        while time.time() - t < 1.0:
            send(ser, STEP_BACK)
            time.sleep(STEP_INTERVAL)

        # Stop 1s
        send(ser, STEP_STOP)
        time.sleep(1.0)
except KeyboardInterrupt:
    send(ser, STEP_STOP)

ser.close()
print("Done.")
