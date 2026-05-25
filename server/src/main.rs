mod coord;
mod db;
mod gps;
mod serial;
mod web;
mod wheelchair;

use std::sync::Arc;
use tokio::sync::{broadcast, RwLock};
use web::AppState;
use wheelchair::Wheelchair;

fn main() {
    tokio::runtime::Builder::new_multi_thread()
        .thread_stack_size(8 * 1024 * 1024)
        .enable_all()
        .build()
        .unwrap()
        .block_on(async_main());
}

async fn async_main() {
    tracing_subscriber::fmt::init();

    let serial_port = std::env::var("SERIAL_PORT").unwrap_or("/dev/ttyUSB0".into());
    let baud: u32 = std::env::var("BAUD").ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(9600);
    let http_port: u16 = std::env::var("PORT").ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(3000);
    let db_path = std::env::var("DB_PATH").unwrap_or("hearth.db".into());
    let static_dir = std::env::var("STATIC_DIR").unwrap_or("/usr/local/share/hearth/static".into());

    let pool = db::init(&db_path).await;

    let chair = Arc::new(RwLock::new(Wheelchair::new("chair-01", "轮椅 01")));

    let (tx, _) = broadcast::channel::<gps::GpsPoint>(32);
    let tx = Arc::new(tx);

    let state = AppState {
        tx: tx.clone(),
        wheelchair: chair.clone(),
    };

    tokio::spawn(async move {
        serial::run_serial(serial_port, baud, tx, chair, pool).await;
    });

    web::run_web(http_port, &static_dir, state).await;
}
