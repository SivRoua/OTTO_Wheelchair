// Simulates ATGM336H GPS output over USB serial
// Sends $GPRMC sentences at 1Hz, slowly moving position
// Uses integer arithmetic to avoid avr-libc float printf limitation

// Position stored as fixed-point: degrees * 1e6
static long lat_e6 = 30572800L;   // 30.5728°N 成都附近
static long lon_e6 = 104066800L;  // 104.0668°E
static int step = 0;

// Convert decimal degrees (e6 fixed-point) to NMEA ddmm.mmmm
// e.g. 30572800 → "3034.3680"
static void formatCoord(long deg_e6, char *buf, int deg_digits) {
  long deg = deg_e6 / 1000000L;
  long frac = deg_e6 % 1000000L;          // fractional degrees * 1e6
  long min_e4 = frac * 60L / 100L;        // minutes * 1e4
  long min_int = min_e4 / 10000L;
  long min_frac = min_e4 % 10000L;
  if (deg_digits == 2)
    sprintf(buf, "%02ld%02ld.%04ld", deg, min_int, min_frac);
  else
    sprintf(buf, "%03ld%02ld.%04ld", deg, min_int, min_frac);
}

// NMEA checksum: XOR of all bytes between $ and *
static uint8_t nmea_checksum(const char *s) {
  uint8_t cs = 0;
  while (*s) cs ^= (uint8_t)*s++;
  return cs;
}

void setup() {
  Serial.begin(9600);
  while (!Serial);
}

void loop() {
  // Simulate slow movement: ~11m per step
  const long dlat[] = { 100L,  100L, -100L, -100L};
  const long dlon[] = { 100L, -100L, -100L,  100L};
  lat_e6 += dlat[step % 4];
  lon_e6 += dlon[step % 4];
  step++;

  char lat_buf[12], lon_buf[12];
  formatCoord(lat_e6, lat_buf, 2);
  formatCoord(lon_e6, lon_buf, 3);

  unsigned long t = millis() / 1000;
  int hh = (t / 3600) % 24;
  int mm = (t / 60) % 60;
  int ss = t % 60;

  char body[80];
  snprintf(body, sizeof(body),
    "GPRMC,%02d%02d%02d,A,%s,N,%s,E,1.2,0.0,110526,,,A",
    hh, mm, ss, lat_buf, lon_buf);

  uint8_t cs = nmea_checksum(body);

  char sentence[100];
  snprintf(sentence, sizeof(sentence), "$%s*%02X", body, cs);

  Serial.println(sentence);
  delay(1000);
}
