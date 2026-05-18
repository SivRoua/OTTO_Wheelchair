#!/usr/bin/env python3
"""Loop-send 'Ciallo~' via DX-LR22-433T22D LoRa module (transparent mode)."""

import serial
import time
import sys

PORT = "/dev/ttyUSB0"
BAUD = 9600
MESSAGE = "Ciallo~"
INTERVAL = 2  # seconds between sends


def main():
    print(f"[*] Opening {PORT} at {BAUD} 8N1...")
    ser = serial.Serial(PORT, BAUD, timeout=0.5)

    # Module defaults: MODE0 (transparent), CHANNEL00, LEVEL2
    # Power-on state is already ready to transmit — no AT config needed.
    print("[*] Module ready (default: MODE0 / CHANNEL00 / LEVEL2)")
    print(f"[*] Looping: '{MESSAGE}' every {INTERVAL}s\n")

    count = 0
    try:
        while True:
            ser.write(MESSAGE.encode())
            count += 1
            sys.stdout.write(f"\r[{count:05d}] Sent: {MESSAGE}    ")
            sys.stdout.flush()
            time.sleep(INTERVAL)
    except KeyboardInterrupt:
        print(f"\n\n[*] Stopped. Total sent: {count}")
    finally:
        ser.close()
        print("[*] Serial closed.")


if __name__ == "__main__":
    main()
