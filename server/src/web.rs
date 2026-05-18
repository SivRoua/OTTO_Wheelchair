use axum::{
    Router,
    routing::get,
    response::{Sse, sse::Event, Json},
    extract::State,
};
use futures::stream::Stream;
use std::{convert::Infallible, sync::Arc, time::Duration};
use tokio::sync::{broadcast, RwLock};
use tokio_stream::{wrappers::BroadcastStream, StreamExt};
use tower_http::services::ServeDir;
use crate::gps::GpsPoint;
use crate::wheelchair::Wheelchair;

pub type WheelchairState = Arc<RwLock<Wheelchair>>;

#[derive(Clone)]
pub struct AppState {
    pub tx: Arc<broadcast::Sender<GpsPoint>>,
    pub wheelchair: WheelchairState,
}

pub async fn run_web(port: u16, static_dir: &str, state: AppState) {
    let app = Router::new()
        .route("/events", get(sse_handler))
        .route("/api/wheelchair", get(wheelchair_handler))
        .route("/api/trail", get(trail_handler))
        .nest_service("/", ServeDir::new(static_dir))
        .with_state(state);

    let addr = format!("0.0.0.0:{}", port);
    tracing::info!("Listening on http://{}", addr);
    let listener = tokio::net::TcpListener::bind(&addr).await.unwrap();
    axum::serve(listener, app).await.unwrap();
}

async fn sse_handler(
    State(state): State<AppState>,
) -> Sse<impl Stream<Item = Result<Event, Infallible>>> {
    let rx = state.tx.subscribe();
    let stream = BroadcastStream::new(rx)
        .filter_map(|msg: Result<GpsPoint, _>| msg.ok())
        .map(|point| {
            let data = serde_json::to_string(&point).unwrap_or_default();
            Ok(Event::default().data(data))
        });

    Sse::new(stream).keep_alive(
        axum::response::sse::KeepAlive::new().interval(Duration::from_secs(5)),
    )
}

async fn wheelchair_handler(
    State(state): State<AppState>,
) -> Json<serde_json::Value> {
    let mut chair = state.wheelchair.write().await;
    chair.refresh_online_status();
    Json(serde_json::to_value(&*chair).unwrap_or_default())
}

async fn trail_handler(
    State(state): State<AppState>,
) -> Json<serde_json::Value> {
    let chair = state.wheelchair.read().await;
    Json(serde_json::json!({ "trail": chair.trail }))
}
