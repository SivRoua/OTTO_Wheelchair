#!/usr/bin/env python3
"""Otto sub-board protocol test."""

import serial
import time

PORT = "/dev/ttyUSB0"
BAUD = 115200


def checksum(flag):
    return ord("S") ^ ord("T") ^ flag


def frame(flag):
    cs = checksum(flag)
    return bytes([ord("S"), ord("T"), flag, cs, ord("E"), ord("D")])


def send(ser, name, flag):
    f = frame(flag)
    ser.write(f)
    ser.flush()
    reply = ser.read(3)  # "OK\n" or "Err"
    print(f"  {name:20s}  {f.hex(' '):20s}  ->  {reply.decode()}")


ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.1)  # let device settle

# flag 构造: step[7:6] | left[5:4] | right[3:2] | 00
UP = 0b10 << 6
DOWN = 0b01 << 6
STOP = 0b00
FWD = 0b10 << 4
BACK = 0b01 << 4
FWD_R = 0b10 << 2
BACK_R = 0b01 << 2

print("=== Otto Sub-Board Protocol Test ===\n")
print(f"Port: {PORT}, Baud: {BAUD}\n")

# Test 1: All stop
send(ser, "All Stop", STOP | STOP | STOP)
# Test 2: All forward + step up
send(ser, "All Fwd + Step Up", UP | FWD | FWD_R)
# Test 3: Step down, left back, right fwd
send(ser, "StepDn L-Back R-Fwd", DOWN | BACK | FWD_R)
# Test 4: Bad checksum
bad = frame(STOP | STOP | STOP)
bad = bytes([bad[0], bad[1], bad[2], 0xFF, bad[4], bad[5]])
ser.write(bad)
ser.flush()
reply = ser.read(3)
print(f"  {'Bad Checksum':20s}  {bad.hex(' '):20s}  ->  {reply.decode()}")

# Test 5: Garbage bytes
ser.write(b"\x00\xff\xaa\x55\x12\x34")
ser.flush()
time.sleep(0.1)
print(f"  {'Garbage (no reply)':20s}  {'--':20s}  ->  (ignored)")

ser.close()
print("\nDone.")
