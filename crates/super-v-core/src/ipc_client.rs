//! IPC Client helper to communicate with super-v-daemon via UNIX Domain Socket.

use anyhow::{anyhow, Result};
use std::path::Path;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixStream;

use crate::ipc::{IpcRequest, IpcResponse};
use crate::ClipboardItem;

pub struct IpcClient {
    stream: UnixStream,
}

impl IpcClient {
    pub async fn connect<P: AsRef<Path>>(socket_path: P) -> Result<Self> {
        let stream = UnixStream::connect(socket_path).await?;
        Ok(Self { stream })
    }

    pub async fn send_request(&mut self, request: &IpcRequest) -> Result<IpcResponse> {
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
}
