//! Watches the pictures folder for files written by screenshot tools.
//!
//! GNOME's screenshot UI offers two ways to finish: "Copy" puts the image on
//! the clipboard (the bridge and the overlay capture that), "Save" only writes
//! a file, so nothing ever reaches the clipboard. This watcher covers the
//! second path: any new file whose name looks like a screenshot is imported
//! into the same history, with the bytes kept in the blob store.

use std::collections::HashSet;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::{Duration, SystemTime};

use tokio::sync::Mutex;

use super_v_core::{BlobStore, Database};

fn pictures_dir() -> PathBuf {
    if let Ok(dir) = std::env::var("XDG_PICTURES_DIR") {
        if !dir.trim().is_empty() {
            return PathBuf::from(dir);
        }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".into());
    PathBuf::from(home).join("Pictures")
}

fn looks_like_screenshot(path: &std::path::Path) -> bool {
    let name = match path.file_name().and_then(|n| n.to_str()) {
        Some(name) => name.to_lowercase(),
        None => return false,
    };
    let extension_ok = matches!(
        path.extension().and_then(|e| e.to_str()).map(|e| e.to_lowercase()).as_deref(),
        Some("png") | Some("jpg") | Some("jpeg") | Some("webp")
    );
    extension_ok
        && (name.contains("screenshot")
            || name.contains("flameshot")
            || name.contains("screencast")
            || name.starts_with("shot-")
            || name.contains("gnome-screenshot")
            || name.contains("capture")
            || name.starts_with("shot")
}

fn mime_for(path: &std::path::Path) -> &'static str {
    match path.extension().and_then(|e| e.to_str()).map(|e| e.to_lowercase()).as_deref() {
        Some("jpg") | Some("jpeg") => "image/jpeg",
        Some("webp") => "image/webp",
        _ => "image/png",
    }
}

/// Import existing files silently so the first scan does not flood the history.
pub fn spawn(db: Arc<Mutex<Database>>, blobs: Arc<BlobStore>) {
    tokio::spawn(async move {
        let dir = pictures_dir();
        let mut seen: HashSet<PathBuf> = HashSet::new();
        let mut primed = false;

        loop {
            if let Ok(entries) = std::fs::read_dir(&dir) {
                for entry in entries.flatten() {
                    let path = entry.path();
                    if !looks_like_screenshot(&path) || seen.contains(&path) {
                        continue;
                    }
                    seen.insert(path.clone());
                    if !primed {
                        continue;
                    }
                    let meta = match entry.metadata() {
                        Ok(meta) => meta,
                        Err(_) => continue,
                    };
                    let fresh = meta
                        .modified()
                        .ok()
                        .and_then(|m| SystemTime::now().duration_since(m).ok())
                        .map(|age| age < Duration::from_secs(120))
                        .unwrap_or(false);
                    if !fresh {
                        continue;
                    }
                    let bytes = match std::fs::read(&path) {
                        Ok(bytes) => bytes,
                        Err(_) => continue,
                    };
                    if bytes.len() < 64 {
                        continue;
                    }
                    let mime = mime_for(&path);
                    let mut guard = db.lock().await;
                    if let Err(e) = guard.insert_image(&blobs, mime, &bytes) {
                        log::warn!("screenshot import failed for {:?}: {}", path, e);
                    } else {
                        log::info!("imported screenshot {:?} ({} B)", path, bytes.len());
                    }
                }
            }
            primed = true;
            tokio::time::sleep(Duration::from_secs(2)).await;
        }
    });
}
