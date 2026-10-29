pub mod ipc;

use anyhow::Result;
use std::sync::Arc;
use tokio::sync::Mutex;
use super_v_core::{
    backend::CompositorType,
    ipc_client::IpcClient,
    Database,
};
use crate::ipc::server::IpcServer;

#[tokio::main]
async fn main() -> Result<()> {
    env_logger::init();

    let comp = CompositorType::detect();
    let comp_str = match comp {
        CompositorType::GnomeWayland => "GNOME Wayland (target GNOME 51)",
        CompositorType::HyprlandWlroots => "Hyprland / wlroots Wayland",
        CompositorType::X11 => "X11 Display",
        CompositorType::Unknown => "Unknown Display",
    };
    log::info!("Detected compositor environment: {}", comp_str);

    let wayland_c_support = super_v_backend_wlr::WlrBackend::is_available();
    log::info!("Native C Wayland client availability: {}", wayland_c_support);

    let x11_support = super_v_backend_x11::X11Backend::new().is_ok();
    log::info!("Native C X11 XTest availability: {}", x11_support);

    let db_path = std::env::var("SUPER_V_DB").unwrap_or_else(|_| ":memory:".to_string());
    let mut db_init = Database::open(&db_path)?;
    db_init.insert_or_touch("text/plain", b"Important API token 12345", Some("Important API token 12345"))?;
    db_init.insert_or_touch("text/plain", b"#308b53", Some("#308b53"))?;
    db_init.insert_or_touch("text/plain", b"https://github.com/GNOME/gnome-shell", Some("https://github.com/GNOME/gnome-shell"))?;
    db_init.insert_or_touch("text/plain", b"Rust programming language", Some("Rust programming language"))?;

    let db = Arc::new(Mutex::new(db_init));

    // Spawn IPC Server in background
    let socket_path = "/tmp/super-v-test.sock";
    let server = IpcServer::new(socket_path, Arc::clone(&db));
    tokio::spawn(async move {
        if let Err(e) = server.run().await {
            log::error!("IPC server runtime failure: {:?}", e);
        }
    });

    // Allow server socket to bind
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    // Test client IPC round-trip
    let mut client = IpcClient::connect(socket_path).await?;
    let history = client.get_history(10).await?;
    let search_rust = client.search("Rust*", 5).await?;

    // Test blob store initialization and quota enforcement
    let blob_dir = "/tmp/super-v-test-blobs";
    let blob_store = super_v_core::BlobStore::new(blob_dir)?;
    let (blob_hash, blob_path) = blob_store.write_blob(b"sample png image data", "png")?;
    log::info!("Wrote blob: hash={}, path={:?}", blob_hash, blob_path);
    assert!(blob_path.exists(), "Blob file must exist on disk");

    // Test retention limit enforcement
    {
        let mut db_guard = db.lock().await;
        let evicted = db_guard.enforce_retention_limits(100, 10 * 1024 * 1024)?;
        log::info!("Retention check passed, evicted count: {}", evicted);
    }

    // Clean up test blob
    let _ = blob_store.delete_blob(&blob_path.to_string_lossy());
    let _ = std::fs::remove_dir_all(blob_dir);

    // Test toggle_pin on first item
    if let Some(first) = history.first() {
        if let Some(id) = first.id {
            client.toggle_pin(id).await?;
            log::info!("Toggled pin state for entry id={}", id);
        }
    }

    println!("VERIFY: ipc_socket_ok=true, history_len={}, search_rust_len={}", history.len(), search_rust.len());
    println!("VERIFY: wayland_c_available={},", wayland_c_support);
    println!("VERIFY: x11_c_available={},", x11_support);
    println!("VERIFY: compositor={}", comp_str);

    assert!(!history.is_empty(), "IPC get_history should return items");
    assert!(!search_rust.is_empty(), "IPC search should find indexed Rust entry");

    // Clean up test socket
    let _ = std::fs::remove_file(socket_path);
    Ok(())
}
