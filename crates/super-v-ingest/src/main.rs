//! Tiny helper the GNOME Shell extension spawns after a clipboard change.
//!
//! The extension is the only component that can observe selection changes
//! without stealing focus, so it hands the text over here; this process is not
//! a Wayland client, which is exactly why the dock never flickers.

use std::io::Read;

use super_v_core::ipc::IpcRequest;
use super_v_core::ipc_client::IpcClient;

const SOCKET: &str = "/tmp/super-v-clipboard.sock";

#[tokio::main(flavor = "current_thread")]
async fn main() {
    let args: Vec<String> = std::env::args().collect();

    let mut text = String::new();
    if let Some(path) = args.get(1) {
        match std::fs::read_to_string(path) {
            Ok(contents) => text = contents,
            Err(_) => return,
        }
        let _ = std::fs::remove_file(path);
    } else if std::io::stdin().read_to_string(&mut text).is_err() {
        return;
    }

    if text.trim().is_empty() {
        return;
    }

    let mut client = match IpcClient::connect(SOCKET).await {
        Ok(client) => client,
        Err(_) => return,
    };

    let _ = client
        .send_request(&IpcRequest::Ingest {
            mime: "text/plain".to_string(),
            text,
        })
        .await;
}
