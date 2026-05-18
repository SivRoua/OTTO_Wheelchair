#!/usr/bin/env python3
"""Send NMEA $GPRMC via LoRa TTL (CH340) — random walk around a fixed point."""
import random
import serial
import time

PORT = "/dev/ttyUSB0"
BAUD = 9600

CENTER_LAT = 33.749
CENTER_LON = 113.2082
STEP = 0.0005  # ~50m per step
MAX_RANGE = 0.005  # stay within ~500m radius


def nmea_checksum(s: str) -> str:
    cs = 0
    for c in s:
        cs ^= ord(c)
    return f"{cs:02X}"


def deg_to_nmea(deg: float, lat: bool) -> str:
    sign = ""
    if deg < 0:
        sign = "S" if lat else "W"
        deg = -deg
    else:
        sign = "N" if lat else "E"
    d = int(deg)
    minutes = (deg - d) * 60.0
    if lat:
        return f"{d:02d}{minutes:07.4f},{sign}"
    else:
        return f"{d:03d}{minutes:07.4f},{sign}"


def build_gprmc(lat: float, lon: float) -> str:
    from datetime import datetime, timezone, timedelta
    tz = timezone(timedelta(hours=8))
    now = datetime.now(tz)
    ts = now.strftime("%H%M%S")
    lat_s = deg_to_nmea(lat, lat=True)
    lon_s = deg_to_nmea(lon, lat=False)
    body = f"GPRMC,{ts},A,{lat_s},{lon_s},0.0,0.0,{now.strftime('%d%m%y')},,,A"
    return f"${body}*{nmea_checksum(body)}"


def main():
    lat = CENTER_LAT
    lon = CENTER_LON

    print(f"[*] Opening {PORT} at {BAUD} 8N1...")
    ser = serial.Serial(PORT, BAUD, timeout=0.5)
    print(f"[*] Center: {deg_to_nmea(CENTER_LAT, True)} {deg_to_nmea(CENTER_LON, False)}")
    print(f"[*] Random walk, step={STEP}° range={MAX_RANGE}°, Ctrl-C to stop\n")

    count = 0
    try:
        while True:
            dlat = random.uniform(-STEP, STEP)
            dlon = random.uniform(-STEP, STEP)
            lat += dlat
            lon += dlon

            if abs(lat - CENTER_LAT) > MAX_RANGE:
                lat = CENTER_LAT + (MAX_RANGE if lat > CENTER_LAT else -MAX_RANGE)
            if abs(lon - CENTER_LON) > MAX_RANGE:
                lon = CENTER_LON + (MAX_RANGE if lon > CENTER_LON else -MAX_RANGE)

            count += 1
            sentence = build_gprmc(lat, lon)
            ser.write((sentence + "\r\n").encode())
            print(f"\r[{count:05d}] {sentence}  ", end="", flush=True)
            time.sleep(1)
    except KeyboardInterrupt:
        print(f"\n\n[*] Stopped. Total: {count}")


if __name__ == "__main__":
    main()
