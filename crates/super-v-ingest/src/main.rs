//! Tiny helper the GNOME Shell extension spawns after a clipboard change.
//!
//! Usage: super-v-ingest [--mime <type>] <path>
//! Text goes through IpcRequest::Ingest, images through IngestFile so the
//! daemon stores them in the blob store. Either way the temp file is removed.

use std::io::Read;

use super_v_core::ipc::IpcRequest;
use super_v_core::ipc_client::IpcClient;

const SOCKET: &str = "/tmp/super-v-clipboard.sock";

#[tokio::main(flavor = "current_thread")]
async fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut mime = String::from("text/plain");
    let mut path: Option<String> = None;

    let mut i = 1;
    while i < args.len() {
        if args[i] == "--mime" && i + 1 < args.len() {
            mime = args[i + 1].clone();
            i += 2;
        } else {
            path = Some(args[i].clone());
            i += 1;
        }
    }

    let mut client = match IpcClient::connect(SOCKET).await {
        Ok(client) => client,
        Err(_) => return,
    };

    if mime.starts_with("image/") {
        if let Some(path) = path {
            let _ = client
                .send_request(&IpcRequest::IngestFile { mime, path })
                .await;
        }
        return;
    }

    let mut text = String::new();
    match path {
        Some(p) => {
            if std::fs::read_to_string(&p).is_ok() {
                text = std::fs::read_to_string(&p).unwrap_or_default();
            }
            let _ = std::fs::remove_file(&p);
        }
        None => {
            if std::io::stdin().read_to_string(&mut text).is_err() {
                return;
            }
        }
    }

    if text.trim().is_empty() {
        return;
    }

    let _ = client.send_request(&IpcRequest::Ingest { mime, text }).await;
}
