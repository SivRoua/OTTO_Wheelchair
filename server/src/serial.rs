use std::io::{BufRead, BufReader};
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{broadcast, mpsc, RwLock};
use sqlx::SqlitePool;
use crate::gps::{GpsPoint, parse_nmea};
use crate::wheelchair::Wheelchair;

fn find_port() -> Option<String> {
    let fixed = ["/dev/ttyLeonardo", "/dev/serial/by-id/usb-Arduino"];
    for path in &fixed {
        if Path::new(path).exists() {
            tracing::info!("Found port: {}", path);
            return Some(path.to_string());
        }
    }
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
        let serial = match serialport::new(&port, baud)
            .timeout(Duration::from_millis(500))
            .open()
        {
            Ok(s) => s,
            Err(e) => {
                tracing::warn!("Cannot open {}: {}", port, e);
                wheelchair.write().await.online = false;
                tokio::time::sleep(Duration::from_secs(3)).await;
                continue;
            }
        };

        let mut reader = BufReader::new(serial);
        let (line_tx, mut line_rx) = mpsc::channel::<String>(64);

        // Sync I/O in a blocking thread — tokio-serial panics on USB unplug
        tokio::task::spawn_blocking(move || {
            let mut buf = String::new();
            loop {
                buf.clear();
                match reader.read_line(&mut buf) {
                    Ok(0) => break,
                    Ok(_) => {
                        if line_tx.blocking_send(buf.clone()).is_err() {
                            break;
                        }
                    }
                    Err(e) => {
                        tracing::warn!("Serial read error: {}", e);
                        break;
                    }
                }
            }
        });

        while let Some(line) = line_rx.recv().await {
            let line = line.trim().to_string();
            if line.is_empty() {
                continue;
            }
            tracing::info!("RAW: {}", line);

            if let Some(mut point) = parse_nmea(&line) {
                tracing::debug!("GPS: {:.6},{:.6}", point.lat, point.lon);

                {
                    let mut chair = wheelchair.write().await;
                    chair.update(point.lat, point.lon, point.speed_knots);
                    point.total_distance_km = chair.total_distance_km;
                    point.speed_kmh = chair.current_speed_kmh;
                }

                let chair_id = { wheelchair.read().await.id.clone() };
                crate::db::insert_point(
                    &db, &chair_id, point.lat, point.lon,
                    point.speed_knots, &point.timestamp,
                ).await;

                let _ = tx.send(point);
            }
        }

        // spawn_blocking thread exited → port disconnected
        wheelchair.write().await.online = false;
        tokio::time::sleep(Duration::from_secs(3)).await;
    }
}
