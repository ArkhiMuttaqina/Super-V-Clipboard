//! Core data models, SQLite storage, and deduplication engine for super-v-clipboard.

pub mod backend;
pub mod blob_store;
pub mod classifier;
pub mod glyphs;
pub mod ipc;
pub mod ipc_client;
pub mod secret_filter;

use anyhow::Result;
use rusqlite::{params, Connection};
use sha2::{Digest, Sha256};
use std::path::Path;

pub use blob_store::BlobStore;
pub use classifier::ContentCategory;
pub use secret_filter::SecretFilter;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ClipboardItem {
    pub id: Option<i64>,
    pub hash: String,
    pub mime: String,
    pub text: Option<String>,
    pub category: ContentCategory,
    pub blob_path: Option<String>,
    pub is_pinned: bool,
    pub is_sensitive: bool,
    pub byte_size: usize,
    pub created_at: i64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SortOrder {
    Recent,
    PinnedFirst,
    Alphabetical,
    SizeDesc,
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
                 category TEXT NOT NULL DEFAULT 'text',
                 blob_path TEXT,
                 is_pinned BOOLEAN NOT NULL DEFAULT 0,
                 is_sensitive BOOLEAN NOT NULL DEFAULT 0,
                 byte_size INTEGER NOT NULL,
                 created_at INTEGER NOT NULL
             );
             CREATE INDEX IF NOT EXISTS idx_entries_created ON entries(created_at DESC);
             CREATE INDEX IF NOT EXISTS idx_entries_pinned ON entries(is_pinned);
             CREATE INDEX IF NOT EXISTS idx_entries_category ON entries(category);

             CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5(
                 text,
                 content='entries',
                 content_rowid='id'
             );

             CREATE TRIGGER IF NOT EXISTS entries_ai AFTER INSERT ON entries BEGIN
                 INSERT INTO entries_fts(rowid, text) VALUES (new.id, new.text);
             END;
             CREATE TRIGGER IF NOT EXISTS entries_ad AFTER DELETE ON entries BEGIN
                 INSERT INTO entries_fts(entries_fts, rowid, text) VALUES('delete', old.id, old.text);
             END;
             CREATE TRIGGER IF NOT EXISTS entries_au AFTER UPDATE ON entries BEGIN
                 INSERT INTO entries_fts(entries_fts, rowid, text) VALUES('delete', old.id, old.text);
                 INSERT INTO entries_fts(rowid, text) VALUES (new.id, new.text);
             END;",
        )?;

        let _ = conn.execute("ALTER TABLE entries ADD COLUMN category TEXT NOT NULL DEFAULT 'text'", []);
        let _ = conn.execute("ALTER TABLE entries ADD COLUMN is_sensitive BOOLEAN NOT NULL DEFAULT 0", []);

        Ok(Self { conn })
    }

    pub fn insert_or_touch(&mut self, mime: &str, data: &[u8], text: Option<&str>) -> Result<ClipboardItem> {
        let mut hasher = Sha256::new();
        hasher.update(data);
        let hash = format!("{:x}", hasher.finalize());
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)?
            .as_secs() as i64;

        let category = ContentCategory::classify(mime, text);
        let is_sensitive = SecretFilter::is_sensitive(mime, text);

        self.conn.execute(
            "INSERT INTO entries (hash, mime, text, category, is_sensitive, byte_size, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
             ON CONFLICT(hash) DO UPDATE SET created_at = excluded.created_at, category = excluded.category, is_sensitive = excluded.is_sensitive",
            params![hash, mime, text, category.as_str(), is_sensitive, data.len(), now],
        )?;

        let id = self.conn.last_insert_rowid();

        Ok(ClipboardItem {
            id: Some(id),
            hash,
            mime: mime.to_string(),
            text: text.map(|s| s.to_string()),
            category,
            blob_path: None,
            is_pinned: false,
            is_sensitive,
            byte_size: data.len(),
            created_at: now,
        })
    }

    pub fn toggle_pin(&mut self, id: i64) -> Result<bool> {
        self.conn.execute(
            "UPDATE entries SET is_pinned = NOT is_pinned WHERE id = ?1",
            params![id],
        )?;
        let mut stmt = self.conn.prepare("SELECT is_pinned FROM entries WHERE id = ?1")?;
        let is_pinned: bool = stmt.query_row(params![id], |row| row.get(0))?;
        Ok(is_pinned)
    }

    pub fn delete_entry(&mut self, id: i64) -> Result<()> {
        self.conn.execute("DELETE FROM entries WHERE id = ?1", params![id])?;
        Ok(())
    }

    pub fn clear_unpinned(&mut self) -> Result<usize> {
        let count = self.conn.execute("DELETE FROM entries WHERE is_pinned = 0", [])?;
        Ok(count)
    }

    pub fn enforce_retention_limits(&mut self, max_unpinned_count: usize, max_total_bytes: usize) -> Result<usize> {
        let current_count: usize = self.conn.query_row(
            "SELECT COUNT(*) FROM entries WHERE is_pinned = 0",
            [],
            |r| r.get(0),
        )?;

        let mut evicted = 0;
        if current_count > max_unpinned_count {
            let excess = current_count - max_unpinned_count;
            let ids_to_delete: Vec<(i64, Option<String>)> = {
                let mut stmt = self.conn.prepare(
                    "SELECT id, blob_path FROM entries WHERE is_pinned = 0 ORDER BY created_at ASC LIMIT ?1",
                )?;
                let rows = stmt.query_map([excess], |r| Ok((r.get(0)?, r.get(1)?)))?;
                rows.filter_map(|res| res.ok()).collect()
            };

            for (id, blob_path) in ids_to_delete {
                if let Some(path) = blob_path {
                    let _ = std::fs::remove_file(path);
                }
                self.conn.execute("DELETE FROM entries WHERE id = ?1", params![id])?;
                evicted += 1;
            }
        }

        let mut total_bytes: usize = self.conn.query_row(
            "SELECT COALESCE(SUM(byte_size), 0) FROM entries WHERE is_pinned = 0",
            [],
            |r| r.get(0),
        )?;

        while total_bytes > max_total_bytes {
            let oldest: Option<(i64, usize, Option<String>)> = {
                let mut stmt = self.conn.prepare(
                    "SELECT id, byte_size, blob_path FROM entries WHERE is_pinned = 0 ORDER BY created_at ASC LIMIT 1",
                )?;
                stmt.query_row([], |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?))).ok()
            };

            match oldest {
                Some((id, size, blob_path)) => {
                    if let Some(path) = blob_path {
                        let _ = std::fs::remove_file(path);
                    }
                    self.conn.execute("DELETE FROM entries WHERE id = ?1", params![id])?;
                    total_bytes = total_bytes.saturating_sub(size);
                    evicted += 1;
                }
                None => break,
            }
        }

        Ok(evicted)
    }

    pub fn list_ordered(&self, limit: usize, sort: SortOrder) -> Result<Vec<ClipboardItem>> {
        let query = match sort {
            SortOrder::Recent => {
                "SELECT id, hash, mime, text, category, blob_path, is_pinned, is_sensitive, byte_size, created_at
                 FROM entries
                 ORDER BY created_at DESC
                 LIMIT ?1"
            }
            SortOrder::PinnedFirst => {
                "SELECT id, hash, mime, text, category, blob_path, is_pinned, is_sensitive, byte_size, created_at
                 FROM entries
                 ORDER BY is_pinned DESC, created_at DESC
                 LIMIT ?1"
            }
            SortOrder::Alphabetical => {
                "SELECT id, hash, mime, text, category, blob_path, is_pinned, is_sensitive, byte_size, created_at
                 FROM entries
                 ORDER BY text ASC
                 LIMIT ?1"
            }
            SortOrder::SizeDesc => {
                "SELECT id, hash, mime, text, category, blob_path, is_pinned, is_sensitive, byte_size, created_at
                 FROM entries
                 ORDER BY byte_size DESC
                 LIMIT ?1"
            }
        };

        let mut stmt = self.conn.prepare(query)?;
        let rows = stmt.query_map([limit], |row| {
            let cat_str: String = row.get(4)?;
            let category = match cat_str.as_str() {
                "url" => ContentCategory::Url,
                "color" => ContentCategory::ColorHex,
                "code" => ContentCategory::CodeSnippet,
                "email" => ContentCategory::Email,
                "image" => ContentCategory::Image,
                _ => ContentCategory::PlainText,
            };

            Ok(ClipboardItem {
                id: Some(row.get(0)?),
                hash: row.get(1)?,
                mime: row.get(2)?,
                text: row.get(3)?,
                category,
                blob_path: row.get(5)?,
                is_pinned: row.get(6)?,
                is_sensitive: row.get(7)?,
                byte_size: row.get(8)?,
                created_at: row.get(9)?,
            })
        })?;

        let mut items = Vec::new();
        for r in rows {
            items.push(r?);
        }
        Ok(items)
    }

    pub fn list_by_category(&self, limit: usize, category: ContentCategory) -> Result<Vec<ClipboardItem>> {
        let mut stmt = self.conn.prepare(
            "SELECT id, hash, mime, text, category, blob_path, is_pinned, is_sensitive, byte_size, created_at
             FROM entries
             WHERE category = ?1
             ORDER BY is_pinned DESC, created_at DESC
             LIMIT ?2",
        )?;
        let rows = stmt.query_map(params![category.as_str(), limit], |row| {
            Ok(ClipboardItem {
                id: Some(row.get(0)?),
                hash: row.get(1)?,
                mime: row.get(2)?,
                text: row.get(3)?,
                category,
                blob_path: row.get(5)?,
                is_pinned: row.get(6)?,
                is_sensitive: row.get(7)?,
                byte_size: row.get(8)?,
                created_at: row.get(9)?,
            })
        })?;

        let mut items = Vec::new();
        for r in rows {
            items.push(r?);
        }
        Ok(items)
    }

    pub fn list_recent(&self, limit: usize) -> Result<Vec<ClipboardItem>> {
        self.list_ordered(limit, SortOrder::PinnedFirst)
    }

    pub fn search(&self, query: &str, limit: usize) -> Result<Vec<ClipboardItem>> {
        let mut stmt = self.conn.prepare(
            "SELECT e.id, e.hash, e.mime, e.text, e.category, e.blob_path, e.is_pinned, e.is_sensitive, e.byte_size, e.created_at
             FROM entries e
             JOIN entries_fts fts ON e.id = fts.rowid
             WHERE entries_fts MATCH ?1
             ORDER BY e.is_pinned DESC, e.created_at DESC
             LIMIT ?2",
        )?;
        let rows = stmt.query_map(params![query, limit], |row| {
            let cat_str: String = row.get(4)?;
            let category = match cat_str.as_str() {
                "url" => ContentCategory::Url,
                "color" => ContentCategory::ColorHex,
                "code" => ContentCategory::CodeSnippet,
                "email" => ContentCategory::Email,
                "image" => ContentCategory::Image,
                _ => ContentCategory::PlainText,
            };

            Ok(ClipboardItem {
                id: Some(row.get(0)?),
                hash: row.get(1)?,
                mime: row.get(2)?,
                text: row.get(3)?,
                category,
                blob_path: row.get(5)?,
                is_pinned: row.get(6)?,
                is_sensitive: row.get(7)?,
                byte_size: row.get(8)?,
                created_at: row.get(9)?,
            })
        })?;

        let mut items = Vec::new();
        for r in rows {
            items.push(r?);
        }
        Ok(items)
    }
}
