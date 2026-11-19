//! UNIX Domain Socket IPC Server for super-v-clipboard.

use anyhow::Result;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{UnixListener, UnixStream};
use tokio::sync::Mutex;

use super_v_core::ipc::{IpcRequest, IpcResponse};
use super_v_core::Database;
use crate::keyboard_injector::KeyboardInjector;

/// Cheap label for the log line: the JSON variant tag plus its id when present.
fn request_kind(line: &str) -> String {
    match serde_json::from_str::<IpcRequest>(line) {
        Ok(IpcRequest::GetHistory { limit }) => format!("GetHistory({})", limit),
        Ok(IpcRequest::PasteItem { id }) => format!("PasteItem({})", id),
        Ok(IpcRequest::TogglePin { id }) => format!("TogglePin({})", id),
        Ok(IpcRequest::DeleteItem { id }) => format!("DeleteItem({})", id),
        Ok(IpcRequest::ClearAll) => "ClearAll".to_string(),
        Ok(IpcRequest::PurgeOlderThan { seconds }) => format!("PurgeOlderThan({}s)", seconds),
        Ok(IpcRequest::ClearUnpinned) => "ClearUnpinned".to_string(),
        Ok(IpcRequest::Search { query, limit }) => format!("Search({:?}, {})", query, limit),
        Ok(IpcRequest::EnforceRetention { max_count, max_bytes }) =>
            format!("EnforceRetention({}, {})", max_count, max_bytes),
        Ok(IpcRequest::Ingest { mime, text }) => format!("Ingest({}, {}B)", mime, text.len()),
        Ok(IpcRequest::IngestFile { mime, path }) => format!("IngestFile({}, {})", mime, path),
        Ok(IpcRequest::InjectPaste) => "InjectPaste".to_string(),
        Err(_) => "Malformed".to_string(),
    }
}

pub struct IpcServer {
    socket_path: PathBuf,
    db: Arc<Mutex<Database>>,
    injector: Arc<KeyboardInjector>,
}

impl IpcServer {
    pub fn new<P: AsRef<Path>>(socket_path: P, db: Arc<Mutex<Database>>) -> Self {
        Self {
            socket_path: socket_path.as_ref().to_path_buf(),
            db,
            injector: Arc::new(KeyboardInjector::new()),
        }
    }

    pub async fn run(&self) -> Result<()> {
        if self.socket_path.exists() {
            let _ = std::fs::remove_file(&self.socket_path);
        }

        let listener = UnixListener::bind(&self.socket_path)?;
        log::info!("IPC Server listening on {:?}", self.socket_path);

        loop {
            match listener.accept().await {
                Ok((stream, _addr)) => {
                    let db = Arc::clone(&self.db);
                    let injector = Arc::clone(&self.injector);
                    tokio::spawn(async move {
                        if let Err(e) = Self::handle_client(stream, db, injector).await {
                            log::warn!("IPC client error: {:?}", e);
                        }
                    });
                }
                Err(e) => {
                    log::error!("IPC accept error: {:?}", e);
                    break;
                }
            }
        }
        Ok(())
    }

    async fn handle_client(
        stream: UnixStream,
        db: Arc<Mutex<Database>>,
        injector: Arc<KeyboardInjector>,
    ) -> Result<()> {
        let (reader, mut writer) = stream.into_split();
        let mut lines = BufReader::new(reader).lines();

        while let Some(line) = lines.next_line().await? {
            if line.trim().is_empty() {
                continue;
            }

            let request_started = std::time::Instant::now();
            let request_label = request_kind(&line);
            let response = match serde_json::from_str::<IpcRequest>(&line) {
                Ok(IpcRequest::GetHistory { limit }) => {
                    let db = db.lock().await;
                    match db.list_recent(limit) {
                        Ok(items) => IpcResponse::History(items),
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::Search { query, limit }) => {
                    let db = db.lock().await;
                    match db.search(&query, limit) {
                        Ok(items) => IpcResponse::SearchResult(items),
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::TogglePin { id }) => {
                    let mut db = db.lock().await;
                    match db.toggle_pin(id) {
                        Ok(_) => IpcResponse::Ack,
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::DeleteItem { id }) => {
                    let mut db = db.lock().await;
                    match db.delete_entry(id) {
                        Ok(_) => IpcResponse::Ack,
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::ClearUnpinned) => {
                    let mut db = db.lock().await;
                    match db.clear_unpinned() {
                        Ok(_) => IpcResponse::Ack,
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::ClearAll) => {
                    let mut db = db.lock().await;
                    match db.clear_all() {
                        Ok(deleted) => {
                            log::info!("clear_all: removed {} rows", deleted);
                            IpcResponse::PurgeResult { deleted }
                        }
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::PurgeOlderThan { seconds }) => {
                    let mut db = db.lock().await;
                    let now = std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .map(|d| d.as_secs() as i64)
                        .unwrap_or(0);
                    match db.purge_older_than(now - seconds as i64) {
                        Ok(deleted) => {
                            if deleted > 0 {
                                log::info!("history vanish: removed {} rows older than {}s", deleted, seconds);
                            }
                            IpcResponse::PurgeResult { deleted }
                        }
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::EnforceRetention { max_count, max_bytes }) => {
                    let mut db = db.lock().await;
                    match db.enforce_retention_limits(max_count, max_bytes) {
                        Ok(evicted_count) => IpcResponse::RetentionResult { evicted_count },
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::IngestFile { mime, path }) => {
                    let data = std::fs::read(&path);
                    /* Only the UI's own clipboard spool files are disposable.
                     * Deleting the caller's file erased cache GIFs the moment
                     * one was picked, so a second pick found nothing. */
                    let disposable = std::path::Path::new(&path)
                        .file_name()
                        .and_then(|name| name.to_str())
                        .map(|name| name.starts_with("super-v-ui-clip-"))
                        .unwrap_or(false);
                    if disposable {
                        let _ = std::fs::remove_file(&path);
                    }
                    match data {
                        Ok(bytes) => {
                            let store = super_v_core::BlobStore::new(
                                super_v_core::default_blob_dir());
                            match store {
                                Ok(store) => {
                                    let mut db = db.lock().await;
                                    match db.insert_image(&store, &mime, &bytes) {
                                        Ok(item) => IpcResponse::History(vec![item]),
                                        Err(e) => IpcResponse::Error(e.to_string()),
                                    }
                                }
                                Err(e) => IpcResponse::Error(e.to_string()),
                            }
                        }
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::Ingest { mime, text }) => {
                    let mut db = db.lock().await;
                    match db.insert_or_touch(&mime, text.as_bytes(), Some(text.as_str())) {
                        Ok(_) => IpcResponse::Ack,
                        Err(e) => IpcResponse::Error(e.to_string()),
                    }
                }
                Ok(IpcRequest::InjectPaste) => {
                    match injector.paste() {
                        Ok(_) => IpcResponse::Ack,
                        Err(e) => IpcResponse::Error(format!("Paste injection failed: {}", e)),
                    }
                }
                Ok(IpcRequest::PasteItem { id }) => {
                    match injector.paste() {
                        Ok(_) => {
                            log::info!("Paste triggered for item id={}", id);
                            IpcResponse::Ack
                        }
                        Err(e) => IpcResponse::Error(format!("Paste injection failed: {}", e)),
                    }
                }
                Err(e) => IpcResponse::Error(format!("Malformed request: {}", e)),
            };

            match &response {
                IpcResponse::Error(e) => log::warn!(
                    "{} failed after {:?}: {}",
                    request_label, request_started.elapsed(), e),
                _ => log::info!("{} ok in {:?}", request_label, request_started.elapsed()),
            }
            let mut out = serde_json::to_string(&response)?;
            out.push('\n');
            writer.write_all(out.as_bytes()).await?;
            writer.flush().await?;
        }

        Ok(())
    }
}
