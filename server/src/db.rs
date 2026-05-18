use sqlx::{SqlitePool, sqlite::SqlitePoolOptions};

pub async fn init(path: &str) -> SqlitePool {
    let url = format!("sqlite://{}?mode=rwc", path);
    let pool = SqlitePoolOptions::new()
        .max_connections(4)
        .connect(&url)
        .await
        .expect("Failed to open SQLite database");

    sqlx::query(
        "CREATE TABLE IF NOT EXISTS track (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            chair_id  TEXT    NOT NULL,
            lat       REAL    NOT NULL,
            lon       REAL    NOT NULL,
            speed     REAL    NOT NULL,
            ts        TEXT    NOT NULL
        )",
    )
    .execute(&pool)
    .await
    .expect("Failed to create track table");

    pool
}

pub async fn insert_point(
    pool: &SqlitePool,
    chair_id: &str,
    lat: f64,
    lon: f64,
    speed: f64,
    ts: &str,
) {
    if let Err(e) = sqlx::query(
        "INSERT INTO track (chair_id, lat, lon, speed, ts) VALUES (?, ?, ?, ?, ?)",
    )
    .bind(chair_id)
    .bind(lat)
    .bind(lon)
    .bind(speed)
    .bind(ts)
    .execute(pool)
    .await
    {
        tracing::warn!("DB insert failed: {}", e);
    }
}
