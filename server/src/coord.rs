use std::f64::consts::PI;

const A: f64 = 6378245.0;
const EE: f64 = 0.00669342162296594323;

fn is_out_of_china(lat: f64, lon: f64) -> bool {
    lon < 72.004 || lon > 137.8347 || lat < 0.8293 || lat > 55.8271
}

fn transform_lat(lon: f64, lat: f64) -> f64 {
    let mut ret = -100.0 + 2.0 * lon + 3.0 * lat + 0.2 * lat * lat + 0.1 * lon * lat
        + 0.2 * (lon.abs()).sqrt();
    ret += (20.0 * (6.0 * lon * PI).sin() + 20.0 * (2.0 * lon * PI).sin()) * 2.0 / 3.0;
    ret += (20.0 * (lat * PI).sin() + 40.0 * (lat / 3.0 * PI).sin()) * 2.0 / 3.0;
    ret += (160.0 * (lat / 12.0 * PI).sin() + 320.0 * (lat * PI / 30.0).sin()) * 2.0 / 3.0;
    ret
}

fn transform_lon(lon: f64, lat: f64) -> f64 {
    let mut ret = 300.0 + lon + 2.0 * lat + 0.1 * lon * lon + 0.1 * lon * lat
        + 0.1 * (lon.abs()).sqrt();
    ret += (20.0 * (6.0 * lon * PI).sin() + 20.0 * (2.0 * lon * PI).sin()) * 2.0 / 3.0;
    ret += (20.0 * (lon * PI).sin() + 40.0 * (lon / 3.0 * PI).sin()) * 2.0 / 3.0;
    ret += (150.0 * (lon / 12.0 * PI).sin() + 300.0 * (lon / 30.0 * PI).sin()) * 2.0 / 3.0;
    ret
}

pub fn wgs84_to_gcj02(lat: f64, lon: f64) -> (f64, f64) {
    if is_out_of_china(lat, lon) {
        return (lat, lon);
    }
    let dlat = transform_lat(lon - 105.0, lat - 35.0);
    let dlon = transform_lon(lon - 105.0, lat - 35.0);
    let radlat = lat.to_radians();
    let magic = 1.0 - EE * radlat.sin().powi(2);
    let sqrt_magic = magic.sqrt();
    let dlat_m = (dlat * 180.0) / ((A * (1.0 - EE)) / (magic * sqrt_magic) * std::f64::consts::PI);
    let dlon_m = (dlon * 180.0) / (A / sqrt_magic * radlat.cos() * std::f64::consts::PI);
    (lat + dlat_m, lon + dlon_m)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gps_point() {
        // Our actual GPS data
        let (lat, lon) = wgs84_to_gcj02(33.7506925, 113.2020015);
        // Expected from Python eviltransform reference
        assert!((lat - 33.749237).abs() < 0.000001, "lat mismatch: {}", lat);
        assert!((lon - 113.208252).abs() < 0.000001, "lon mismatch: {}", lon);
    }

    #[test]
    fn test_beijing() {
        let (lat, lon) = wgs84_to_gcj02(39.9042, 116.4074);
        assert!((lat - 39.905603).abs() < 0.000001, "lat mismatch: {}", lat);
        assert!((lon - 116.413642).abs() < 0.000001, "lon mismatch: {}", lon);
    }

    #[test]
    fn test_out_of_china() {
        let (lat, lon) = wgs84_to_gcj02(35.6762, 139.6503); // Tokyo
        assert!((lat - 35.6762).abs() < 0.000001);
        assert!((lon - 139.6503).abs() < 0.000001);
    }
}
