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

    let x11_support = super_v_backend_x11::X11Backend::new().is_ok();
    log::info!("Native C X11 XTest availability: {}", x11_support);

    let db_path = std::env::var("SUPER_V_DB").unwrap_or_else(|_| ":memory:".to_string());
    let mut db_init = Database::open(&db_path)?;
    db_init.insert_or_touch("text/plain", b"Important API token 12345", Some("Important API token 12345"))?;
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

    println!("VERIFY: ipc_socket_ok=true, history_len={}, search_rust_len={}", history.len(), search_rust.len());
    println!("VERIFY: x11_c_available={},", x11_support);
    println!("VERIFY: compositor={}", comp_str);

    assert!(!history.is_empty(), "IPC get_history should return items");
    assert!(!search_rust.is_empty(), "IPC search should find indexed Rust entry");

    // Clean up test socket
    let _ = std::fs::remove_file(socket_path);
    Ok(())
}
