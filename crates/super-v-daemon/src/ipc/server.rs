//! UNIX Domain Socket IPC Server for super-v-clipboard.

use anyhow::Result;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{UnixListener, UnixStream};
use tokio::sync::Mutex;

use super_v_core::ipc::{IpcRequest, IpcResponse};
use super_v_core::Database;

pub struct IpcServer {
    socket_path: PathBuf,
    db: Arc<Mutex<Database>>,
}

impl IpcServer {
    pub fn new<P: AsRef<Path>>(socket_path: P, db: Arc<Mutex<Database>>) -> Self {
        Self {
            socket_path: socket_path.as_ref().to_path_buf(),
            db,
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
                    tokio::spawn(async move {
                        if let Err(e) = Self::handle_client(stream, db).await {
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

    async fn handle_client(stream: UnixStream, db: Arc<Mutex<Database>>) -> Result<()> {
        let (reader, mut writer) = stream.into_split();
        let mut lines = BufReader::new(reader).lines();

        while let Some(line) = lines.next_line().await? {
            if line.trim().is_empty() {
                continue;
            }

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
                Ok(IpcRequest::PasteItem { id: _ }) => {
                    IpcResponse::Ack
                }
                Err(e) => IpcResponse::Error(format!("Malformed request: {}", e)),
            };

            let mut out = serde_json::to_string(&response)?;
            out.push('\n');
            writer.write_all(out.as_bytes()).await?;
            writer.flush().await?;
        }

        Ok(())
    }
}
