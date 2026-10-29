//! Disk-backed Blob Store and Storage Quota Eviction Engine for images and large payloads.

use anyhow::{anyhow, Result};
use sha2::{Digest, Sha256};
use std::fs;
use std::path::{Path, PathBuf};

pub struct BlobStore {
    root_dir: PathBuf,
}

impl BlobStore {
    pub fn new<P: AsRef<Path>>(root_dir: P) -> Result<Self> {
        let root = root_dir.as_ref().to_path_buf();
        if !root.exists() {
            fs::create_dir_all(&root)?;
        }
        Ok(Self { root_dir: root })
    }

    pub fn write_blob(&self, data: &[u8], ext: &str) -> Result<(String, PathBuf)> {
        let mut hasher = Sha256::new();
        hasher.update(data);
        let hash = format!("{:x}", hasher.finalize());

        let filename = format!("{}.{}", hash, ext.trim_start_matches('.'));
        let target_path = self.root_dir.join(&filename);

        if !target_path.exists() {
            fs::write(&target_path, data)?;
        }

        Ok((hash, target_path))
    }

    pub fn read_blob(&self, hash: &str, ext: &str) -> Result<Vec<u8>> {
        let filename = format!("{}.{}", hash, ext.trim_start_matches('.'));
        let target_path = self.root_dir.join(&filename);
        if !target_path.exists() {
            return Err(anyhow!("Blob not found: {:?}", target_path));
        }
        let data = fs::read(&target_path)?;
        Ok(data)
    }

    pub fn delete_blob(&self, blob_path: &str) -> Result<bool> {
        let path = Path::new(blob_path);
        if path.exists() {
            fs::remove_file(path)?;
            Ok(true)
        } else {
            Ok(false)
        }
    }

    pub fn total_storage_bytes(&self) -> Result<u64> {
        let mut total: u64 = 0;
        if self.root_dir.exists() {
            for entry in fs::read_dir(&self.root_dir)? {
                let entry = entry?;
                if let Ok(metadata) = entry.metadata() {
                    if metadata.is_file() {
                        total += metadata.len();
                    }
                }
            }
        }
        Ok(total)
    }
}
