//! Real-time background clipboard listener polling wl-paste/xclip every 150ms.
//! Captures text, images (screenshots), and file URI copies.

use std::process::Command;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::Mutex;

use super_v_core::blob_store::BlobStore;
use super_v_core::Database;

pub struct ClipboardPoller {
    db: Arc<Mutex<Database>>,
    blob_store: Arc<BlobStore>,
}

impl ClipboardPoller {
    pub fn new(db: Arc<Mutex<Database>>, blob_store: Arc<BlobStore>) -> Self {
        Self { db, blob_store }
    }

    pub fn start_polling(self: Arc<Self>) {
        tokio::spawn(async move {
            let mut last_hash = String::new();

            loop {
                tokio::time::sleep(Duration::from_millis(150)).await;

                // 1. Check available MIME types
                let types_output = Command::new("wl-paste")
                    .arg("--list-types")
                    .output();

                let Ok(out) = types_output else { continue; };
                if !out.status.success() { continue; };

                let types_str = String::from_utf8_lossy(&out.stdout);
                let types: Vec<&str> = types_str.lines().map(|s| s.trim()).collect();

                if types.is_empty() { continue; }

                // Check for image (e.g. screenshot)
                if types.iter().any(|&t| t == "image/png" || t.starts_with("image/")) {
                    let img_data = Command::new("wl-paste")
                        .args(["--type", "image/png"])
                        .output();

                    if let Ok(img_out) = img_data {
                        if img_out.status.success() && !img_out.stdout.is_empty() {
                            let data = img_out.stdout;
                            let mut hasher = sha2::Sha256::default();
                            use sha2::Digest;
                            hasher.update(&data);
                            let hash = format!("{:x}", hasher.finalize());

                            if hash != last_hash {
                                last_hash = hash.clone();
                                let ext = "png";
                                if let Ok((_, path)) = self.blob_store.write_blob(&data, ext) {
                                    let mut db = self.db.lock().await;
                                    let preview = format!("[Screenshot: {} KB]", data.len() / 1024);
                                    let _ = db.insert_or_touch("image/png", &data, Some(&preview));
                                    log::info!("Captured screenshot into clipboard: {:?}, size={}KB", path, data.len() / 1024);
                                }
                            }
                            continue;
                        }
                    }
                }

                // Check for URI list (file copy/cut)
                if types.iter().any(|&t| t == "text/uri-list") {
                    let uri_data = Command::new("wl-paste")
                        .args(["--type", "text/uri-list"])
                        .output();

                    if let Ok(uri_out) = uri_data {
                        if uri_out.status.success() && !uri_out.stdout.is_empty() {
                            let text = String::from_utf8_lossy(&uri_out.stdout).to_string();
                            let mut hasher = sha2::Sha256::default();
                            use sha2::Digest;
                            hasher.update(text.as_bytes());
                            let hash = format!("{:x}", hasher.finalize());

                            if hash != last_hash {
                                last_hash = hash;
                                let mut db = self.db.lock().await;
                                let _ = db.insert_or_touch("text/uri-list", text.as_bytes(), Some(&text));
                                log::info!("Captured file URI copy: {}", text.trim());
                            }
                            continue;
                        }
                    }
                }

                // Check for plain text
                if types.iter().any(|&t| t == "text/plain;charset=utf-8" || t == "text/plain" || t == "UTF8_STRING") {
                    let text_data = Command::new("wl-paste")
                        .arg("--no-newline")
                        .output();

                    if let Ok(text_out) = text_data {
                        if text_out.status.success() && !text_out.stdout.is_empty() {
                            let text = String::from_utf8_lossy(&text_out.stdout).to_string();
                            let mut hasher = sha2::Sha256::default();
                            use sha2::Digest;
                            hasher.update(text.as_bytes());
                            let hash = format!("{:x}", hasher.finalize());

                            if hash != last_hash {
                                last_hash = hash;
                                let mut db = self.db.lock().await;
                                let _ = db.insert_or_touch("text/plain", text.as_bytes(), Some(&text));
                                log::info!("Captured text copy: {}", text.trim().chars().take(40).collect::<String>());
                            }
                        }
                    }
                }
            }
        });
    }
}
