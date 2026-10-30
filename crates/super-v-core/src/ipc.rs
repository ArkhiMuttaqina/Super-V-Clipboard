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
    /// Enforce storage retention limits
    EnforceRetention { max_count: usize, max_bytes: usize },
    /// Store text captured by the UI process (which owns the Wayland clipboard)
    Ingest { mime: String, text: String },
    /// Inject Ctrl+V into the focused application without a history row
    InjectPaste,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", content = "payload")]
pub enum IpcResponse {
    History(Vec<ClipboardItem>),
    SearchResult(Vec<ClipboardItem>),
    RetentionResult { evicted_count: usize },
    Ack,
    Error(String),
}
