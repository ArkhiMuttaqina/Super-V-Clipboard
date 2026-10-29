//! IPC Protocol messages between super-v-daemon and super-v-ui overlay.

use serde::{Deserialize, Serialize};
use crate::ClipboardItem;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", content = "payload")]
pub enum IpcRequest {
    /// Request recent clipboard history
    GetHistory { limit: usize },
    /// Select item and trigger synthetic paste
    PasteItem { id: i64 },
    /// Toggle pin state for an item
    TogglePin { id: i64 },
    /// Delete item from history
    DeleteItem { id: i64 },
    /// Clear all unpinned items
    ClearUnpinned,
    /// Search items via SQLite FTS5
    Search { query: String, limit: usize },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", content = "payload")]
pub enum IpcResponse {
    History(Vec<ClipboardItem>),
    SearchResult(Vec<ClipboardItem>),
    Ack,
    Error(String),
}
