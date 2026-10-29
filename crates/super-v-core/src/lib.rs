//! Core data models, SQLite storage, and deduplication engine for super-v-clipboard.

pub mod glyphs;

use anyhow::Result;
use rusqlite::{params, Connection};
use sha2::{Digest, Sha256};
use std::path::Path;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ClipboardItem {
    pub id: Option<i64>,
    pub hash: String,
    pub mime: String,
    pub text: Option<String>,
    pub blob_path: Option<String>,
    pub is_pinned: bool,
    pub byte_size: usize,
    pub created_at: i64,
}

pub struct Database {
    conn: Connection,
}

impl Database {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self> {
        let conn = Connection::open(path)?;
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS entries (
                 id INTEGER PRIMARY KEY AUTOINCREMENT,
                 hash TEXT UNIQUE NOT NULL,
                 mime TEXT NOT NULL,
                 text TEXT,
                 blob_path TEXT,
                 is_pinned BOOLEAN NOT NULL DEFAULT 0,
                 byte_size INTEGER NOT NULL,
                 created_at INTEGER NOT NULL
             );
             CREATE INDEX IF NOT EXISTS idx_entries_created ON entries(created_at DESC);
             CREATE INDEX IF NOT EXISTS idx_entries_pinned ON entries(is_pinned);
             
             CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5(
                 text,
                 content='entries',
                 content_rowid='id'
             );",
        )?;
        Ok(Self { conn })
    }

    pub fn insert_or_touch(&mut self, mime: &str, data: &[u8], text: Option<&str>) -> Result<ClipboardItem> {
        let mut hasher = Sha256::new();
        hasher.update(data);
        let hash = format!("{:x}", hasher.finalize());
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)?
            .as_secs() as i64;

        self.conn.execute(
            "INSERT INTO entries (hash, mime, text, byte_size, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5)
             ON CONFLICT(hash) DO UPDATE SET created_at = excluded.created_at",
            params![hash, mime, text, data.len(), now],
        )?;

        let id = self.conn.last_insert_rowid();

        Ok(ClipboardItem {
            id: Some(id),
            hash,
            mime: mime.to_string(),
            text: text.map(|s| s.to_string()),
            blob_path: None,
            is_pinned: false,
            byte_size: data.len(),
            created_at: now,
        })
    }

    pub fn list_recent(&self, limit: usize) -> Result<Vec<ClipboardItem>> {
        let mut stmt = self.conn.prepare(
            "SELECT id, hash, mime, text, blob_path, is_pinned, byte_size, created_at
             FROM entries
             ORDER BY is_pinned DESC, created_at DESC
             LIMIT ?1",
        )?;
        let rows = stmt.query_map([limit], |row| {
            Ok(ClipboardItem {
                id: Some(row.get(0)?),
                hash: row.get(1)?,
                mime: row.get(2)?,
                text: row.get(3)?,
                blob_path: row.get(4)?,
                is_pinned: row.get(5)?,
                byte_size: row.get(6)?,
                created_at: row.get(7)?,
            })
        })?;

        let mut items = Vec::new();
        for r in rows {
            items.push(r?);
        }
        Ok(items)
    }
}
