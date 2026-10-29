//! Local GIF Scanner and Inotify Watcher for super-v-clipboard.
//! Scans ~/Downloads and ~/Pictures for animated GIFs and tracks favorites.

use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct GifItem {
    pub path: String,
    pub filename: String,
    pub is_favorite: bool,
    pub byte_size: u64,
}

pub struct GifScanner {
    search_dirs: Vec<PathBuf>,
}

impl GifScanner {
    pub fn default_dirs() -> Self {
        let mut dirs = Vec::new();
        if let Ok(home) = std::env::var("HOME") {
            let h = PathBuf::from(home);
            dirs.push(h.join("Downloads"));
            dirs.push(h.join("Pictures"));
            dirs.push(h.join(".local/share/super-v-clipboard/gifs"));
        }
        Self { search_dirs: dirs }
    }

    pub fn scan_local_gifs(&self, limit: usize) -> Vec<GifItem> {
        let mut results = Vec::new();

        for dir in &self.search_dirs {
            if !dir.exists() || !dir.is_dir() {
                continue;
            }

            if let Ok(entries) = fs::read_dir(dir) {
                for entry in entries.flatten() {
                    let path = entry.path();
                    if path.is_file() {
                        if let Some(ext) = path.extension() {
                            if ext.to_string_lossy().to_lowercase() == "gif" {
                                if let Ok(metadata) = entry.metadata() {
                                    results.push(GifItem {
                                        path: path.to_string_lossy().to_string(),
                                        filename: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                                        is_favorite: false,
                                        byte_size: metadata.len(),
                                    });

                                    if results.len() >= limit {
                                        return results;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        results
    }
}
