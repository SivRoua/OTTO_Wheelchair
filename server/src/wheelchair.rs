use chrono::{DateTime, Utc};
use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct Wheelchair {
    pub id: String,
    pub name: String,
    pub lat: f64,
    pub lon: f64,
    pub last_seen: DateTime<Utc>,
    pub online: bool,
    pub total_distance_km: f64,
    pub current_speed_kmh: f64,
    pub trail: Vec<(f64, f64)>,
    prev_lat: f64,
    prev_lon: f64,
    prev_seen: DateTime<Utc>,
}

impl Wheelchair {
    pub fn new(id: impl Into<String>, name: impl Into<String>) -> Self {
        let now = Utc::now();
        Self {
            id: id.into(),
            name: name.into(),
            lat: 0.0,
            lon: 0.0,
            last_seen: now,
            online: false,
            total_distance_km: 0.0,
            current_speed_kmh: 0.0,
            trail: Vec::new(),
            prev_lat: 0.0,
            prev_lon: 0.0,
            prev_seen: now,
        }
    }

    pub fn update(&mut self, lat: f64, lon: f64, speed_knots: f64) {
        let now = Utc::now();
        let dist_km = if self.online {
            haversine_km(self.prev_lat, self.prev_lon, lat, lon)
        } else {
            0.0
        };
        self.total_distance_km += dist_km;

        // Speed: prefer GPS-reported, else estimate from displacement
        if speed_knots > 0.1 {
            self.current_speed_kmh = speed_knots * 1.852;
        } else if dist_km > 0.01 {
            let dt_secs = (now - self.prev_seen).num_seconds() as f64;
            if dt_secs > 0.0 && dt_secs < 30.0 {
                self.current_speed_kmh = dist_km / (dt_secs / 3600.0);
            } else {
                self.current_speed_kmh = 0.0;
            }
        } else {
            self.current_speed_kmh = 0.0;
        }

        self.prev_lat = lat;
        self.prev_lon = lon;
        self.prev_seen = now;
        self.lat = lat;
        self.lon = lon;
        self.last_seen = now;
        self.online = true;
        self.trail.push((lat, lon));
    }

    // Mark offline if no data for more than 10 seconds
    pub fn refresh_online_status(&mut self) {
        let elapsed = Utc::now()
            .signed_duration_since(self.last_seen)
            .num_seconds();
        self.online = elapsed < 10;
    }
}

fn haversine_km(lat1: f64, lon1: f64, lat2: f64, lon2: f64) -> f64 {
    let r = 6371.0;
    let dlat = (lat2 - lat1).to_radians();
    let dlon = (lon2 - lon1).to_radians();
    let a = (dlat / 2.0).sin().powi(2)
        + lat1.to_radians().cos() * lat2.to_radians().cos() * (dlon / 2.0).sin().powi(2);
    r * 2.0 * a.sqrt().atan2((1.0 - a).sqrt())
}
