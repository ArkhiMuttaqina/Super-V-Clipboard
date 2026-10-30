//! Backup, Export, and Migration Engine (JSON + Tar GZ) for super-v-clipboard.

use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

use crate::ClipboardItem;
use crate::Database;

#[derive(Debug, Serialize, Deserialize)]
pub struct BackupArchive {
    pub version: u32,
    pub created_at: i64,
    pub entries: Vec<ClipboardItem>,
}

pub struct BackupManager;

impl BackupManager {
    pub fn export_to_json<P: AsRef<Path>>(db: &Database, path: P) -> Result<usize> {
        let items = db.list_recent(5000)?;
        let count = items.len();
        let archive = BackupArchive {
            version: 1,
            created_at: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?
                .as_secs() as i64,
            entries: items,
        };

        let json = serde_json::to_string_pretty(&archive)?;
        fs::write(path, json)?;
        Ok(count)
    }

    pub fn import_from_json<P: AsRef<Path>>(db: &mut Database, path: P) -> Result<usize> {
        let content = fs::read_to_string(path)?;
        let archive: BackupArchive = serde_json::from_str(&content)?;

        let mut imported = 0;
        for entry in archive.entries {
            let data = entry.text.as_deref().unwrap_or("").as_bytes();
            let text = entry.text.as_deref();
            db.insert_or_touch(&entry.mime, data, text)?;
            imported += 1;
        }

        Ok(imported)
    }
}
