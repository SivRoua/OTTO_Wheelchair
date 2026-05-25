use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GpsPoint {
    pub lat: f64,
    pub lon: f64,
    pub speed_knots: f64,
    pub speed_kmh: f64,
    pub timestamp: String,
    pub total_distance_km: f64,
}

pub fn parse_nmea(line: &str) -> Option<GpsPoint> {
    parse_gprmc(line).or_else(|| parse_gga(line)).map(|mut p| {
        let (gcj_lat, gcj_lon) = crate::coord::wgs84_to_gcj02(p.lat, p.lon);
        p.lat = gcj_lat;
        p.lon = gcj_lon;
        p
    })
}

// $GPRMC / $GNRMC
pub fn parse_gprmc(line: &str) -> Option<GpsPoint> {
    let line = line.trim();
    if !line.starts_with("$GPRMC") && !line.starts_with("$GNRMC") {
        return None;
    }
    verify_checksum(line)?;

    let fields: Vec<&str> = line.split(',').collect();
    if fields.len() < 8 {
        return None;
    }
    if fields[2] != "A" {
        return None;
    }

    let lat = parse_nmea_coord(fields[3], fields[4])?;
    let lon = parse_nmea_coord(fields[5], fields[6])?;
    let speed_knots = fields[7].parse::<f64>().unwrap_or(0.0);
    let timestamp = fields[1].to_string();

    Some(GpsPoint { lat, lon, speed_knots, speed_kmh: 0.0, timestamp, total_distance_km: 0.0 })
}

// $GPGGA / $GNGGA
fn parse_gga(line: &str) -> Option<GpsPoint> {
    let line = line.trim();
    if !line.starts_with("$GPGGA") && !line.starts_with("$GNGGA") {
        return None;
    }
    verify_checksum(line)?;

    let fields: Vec<&str> = line.split(',').collect();
    if fields.len() < 6 {
        return None;
    }
    // Field 6: fix quality — 0 = no fix
    if fields[6] == "0" || fields[6].is_empty() {
        return None;
    }

    let lat = parse_nmea_coord(fields[2], fields[3])?;
    let lon = parse_nmea_coord(fields[4], fields[5])?;
    let timestamp = fields[1].to_string();

    Some(GpsPoint { lat, lon, speed_knots: 0.0, speed_kmh: 0.0, timestamp, total_distance_km: 0.0 })
}

fn verify_checksum(line: &str) -> Option<()> {
    let star = line.rfind('*')?;
    let body = &line[1..star];
    let cs_str = line[star + 1..].trim();
    let expected = u8::from_str_radix(cs_str, 16).ok()?;
    let actual = body.bytes().fold(0u8, |acc, b| acc ^ b);
    if actual == expected { Some(()) } else { None }
}

fn parse_nmea_coord(value: &str, dir: &str) -> Option<f64> {
    if value.is_empty() {
        return None;
    }
    let dot = value.find('.')?;
    if dot < 2 {
        return None;
    }
    let degrees: f64 = value[..dot - 2].parse().ok()?;
    let minutes: f64 = value[dot - 2..].parse().ok()?;
    let mut decimal = degrees + minutes / 60.0;
    if dir == "S" || dir == "W" {
        decimal = -decimal;
    }
    Some(decimal)
}
