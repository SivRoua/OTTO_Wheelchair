use std::path::Path;
use std::sync::Arc;
use tokio::sync::{broadcast, RwLock};
use tokio_serial::SerialPortBuilderExt;
use tokio::io::{AsyncBufReadExt, BufReader};
use sqlx::SqlitePool;
use crate::gps::{GpsPoint, parse_gprmc};
use crate::wheelchair::Wheelchair;

/// Find a usable serial port: try udev symlink, then scan ttyACM/ttyUSB
fn find_port() -> Option<String> {
    // Prefer udev symlink
    let fixed = ["/dev/ttyLeonardo", "/dev/serial/by-id/usb-Arduino"];
    for path in &fixed {
        if Path::new(path).exists() {
            tracing::info!("Found port: {}", path);
            return Some(path.to_string());
        }
    }

    // Scan /dev/ttyACM* then /dev/ttyUSB*
    for prefix in &["ttyACM", "ttyUSB"] {
        if let Ok(entries) = std::fs::read_dir("/dev") {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().into_owned();
                if name.starts_with(prefix) {
                    let path = format!("/dev/{}", name);
                    tracing::info!("Found port scanning: {}", path);
                    return Some(path);
                }
            }
        }
    }

    None
}

pub async fn run_serial(
    default_port: String,
    baud: u32,
    tx: Arc<broadcast::Sender<GpsPoint>>,
    wheelchair: Arc<RwLock<Wheelchair>>,
    db: SqlitePool,
) {
    loop {
        let port = find_port().unwrap_or_else(|| {
            tracing::warn!("No ttyACM/ttyUSB found, retrying {} in 3s...", default_port);
            default_port.clone()
        });

        tracing::info!("Opening serial port {}", port);
        match tokio_serial::new(&port, baud).open_native_async() {
            Ok(serial) => {
                let reader = BufReader::new(serial);
                let mut lines = reader.lines();
                loop {
                    match lines.next_line().await {
                        Ok(Some(line)) => {
                            tracing::info!("RAW: {}", line);
                            if let Some(mut point) = parse_gprmc(&line) {
                                tracing::debug!("GPS: {:.6},{:.6}", point.lat, point.lon);

                                {
                                    let mut chair = wheelchair.write().await;
                                    chair.update(point.lat, point.lon, point.speed_knots);
                                    point.total_distance_km = chair.total_distance_km;
                                    point.speed_kmh = chair.current_speed_kmh;
                                }

                                let chair_id = {
                                    wheelchair.read().await.id.clone()
                                };
                                crate::db::insert_point(
                                    &db,
                                    &chair_id,
                                    point.lat,
                                    point.lon,
                                    point.speed_knots,
                                    &point.timestamp,
                                ).await;

                                let _ = tx.send(point);
                            }
                        }
                        Ok(None) => {
                            tracing::warn!("Serial port EOF");
                            break;
                        }
                        Err(e) => {
                            tracing::warn!("Serial read error: {}", e);
                            break;
                        }
                    }
                }
            }
            Err(e) => {
                tracing::warn!("Cannot open {}: {}", port, e);
            }
        }

        // Mark offline while disconnected
        wheelchair.write().await.online = false;
        tokio::time::sleep(std::time::Duration::from_secs(3)).await;
    }
}
