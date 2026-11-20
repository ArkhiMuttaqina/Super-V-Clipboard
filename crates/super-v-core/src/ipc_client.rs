//! IPC Client helper to communicate with super-v-daemon via UNIX Domain Socket.

use anyhow::{anyhow, Result};
use std::path::Path;
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixStream;

use crate::ipc::{IpcRequest, IpcResponse};
use crate::ClipboardItem;

/// How long one request may take before the caller gives up.
///
/// The overlay drives this client from the GTK main thread, so a daemon that
/// stops answering used to freeze the window for good: no error, no log line,
/// just "not responding" until it was force quit. Every request now finishes
/// or fails inside this window instead. 3 s stays under the compositor's own
/// ping timeout, so a stall degrades into a warning instead of a hang.
pub const REQUEST_TIMEOUT: Duration = Duration::from_secs(3);

pub struct IpcClient {
    stream: UnixStream,
}

impl IpcClient {
    pub async fn connect<P: AsRef<Path>>(socket_path: P) -> Result<Self> {
        let stream = UnixStream::connect(socket_path).await?;
        Ok(Self { stream })
    }

    pub async fn send_request(&mut self, request: &IpcRequest) -> Result<IpcResponse> {
        match tokio::time::timeout(REQUEST_TIMEOUT, self.send_request_inner(request)).await {
            Ok(result) => result,
            Err(_) => Err(anyhow!("daemon did not answer within {:?}", REQUEST_TIMEOUT)),
        }
    }

    async fn send_request_inner(&mut self, request: &IpcRequest) -> Result<IpcResponse> {
        let (reader, mut writer) = self.stream.split();
        let mut msg = serde_json::to_string(request)?;
        msg.push('\n');
        writer.write_all(msg.as_bytes()).await?;
        writer.flush().await?;

        let mut lines = BufReader::new(reader).lines();
        if let Some(line) = lines.next_line().await? {
            let res: IpcResponse = serde_json::from_str(&line)?;
            Ok(res)
        } else {
            Err(anyhow!("IPC connection closed prematurely"))
        }
    }

    pub async fn get_history(&mut self, limit: usize) -> Result<Vec<ClipboardItem>> {
        match self.send_request(&IpcRequest::GetHistory { limit }).await? {
            IpcResponse::History(items) => Ok(items),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn search(&mut self, query: &str, limit: usize) -> Result<Vec<ClipboardItem>> {
        match self.send_request(&IpcRequest::Search { query: query.to_string(), limit }).await? {
            IpcResponse::SearchResult(items) => Ok(items),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn toggle_pin(&mut self, id: i64) -> Result<()> {
        match self.send_request(&IpcRequest::TogglePin { id }).await? {
            IpcResponse::Ack => Ok(()),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn delete_item(&mut self, id: i64) -> Result<()> {
        match self.send_request(&IpcRequest::DeleteItem { id }).await? {
            IpcResponse::Ack => Ok(()),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn clear_unpinned(&mut self) -> Result<()> {
        match self.send_request(&IpcRequest::ClearUnpinned).await? {
            IpcResponse::Ack => Ok(()),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn move_to_top(&mut self, id: i64) -> Result<()> {
        match self.send_request(&IpcRequest::MoveToTop { id }).await? {
            IpcResponse::Ack => Ok(()),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn clear_all(&mut self) -> Result<usize> {
        match self.send_request(&IpcRequest::ClearAll).await? {
            IpcResponse::PurgeResult { deleted } => Ok(deleted),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn purge_older_than(&mut self, seconds: u64) -> Result<usize> {
        match self.send_request(&IpcRequest::PurgeOlderThan { seconds }).await? {
            IpcResponse::PurgeResult { deleted } => Ok(deleted),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }

    pub async fn enforce_retention(&mut self, max_count: usize, max_bytes: usize) -> Result<usize> {
        match self.send_request(&IpcRequest::EnforceRetention { max_count, max_bytes }).await? {
            IpcResponse::RetentionResult { evicted_count } => Ok(evicted_count),
            IpcResponse::Error(e) => Err(anyhow!("Daemon error: {}", e)),
            other => Err(anyhow!("Unexpected response: {:?}", other)),
        }
    }
}
