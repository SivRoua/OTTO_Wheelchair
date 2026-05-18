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

// Parse $GPRMC sentence, returns None if invalid or no fix
pub fn parse_gprmc(line: &str) -> Option<GpsPoint> {
    let line = line.trim();
    if !line.starts_with("$GPRMC") && !line.starts_with("$GNRMC") {
        return None;
    }

    // Verify checksum
    if let Some(star) = line.rfind('*') {
        let body = &line[1..star];
        let cs_str = &line[star + 1..];
        let expected: u8 = cs_str.trim().parse::<u8>().ok()
            .or_else(|| u8::from_str_radix(cs_str.trim(), 16).ok())?;
        let actual: u8 = body.bytes().fold(0u8, |acc, b| acc ^ b);
        if actual != expected {
            return None;
        }
    }

    let fields: Vec<&str> = line.split(',').collect();
    if fields.len() < 8 {
        return None;
    }

    // Field 2: status A=active, V=void
    if fields[2] != "A" {
        return None;
    }

    let lat = parse_nmea_coord(fields[3], fields[4])?;
    let lon = parse_nmea_coord(fields[5], fields[6])?;
    let speed_knots = fields[7].parse::<f64>().unwrap_or(0.0);
    let timestamp = fields[1].to_string();

    Some(GpsPoint { lat, lon, speed_knots, speed_kmh: 0.0, timestamp, total_distance_km: 0.0 })
}

// Convert NMEA ddmm.mmmm + N/S/E/W to decimal degrees
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
