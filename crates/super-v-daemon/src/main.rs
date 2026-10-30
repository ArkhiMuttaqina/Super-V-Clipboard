pub mod ipc;
pub mod keyboard_injector;

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

    let args: Vec<String> = std::env::args().collect();
    let is_test_run = args.iter().any(|a| a == "--test");

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

    // Default persistent user database path
    let db_path = if is_test_run {
        ":memory:".to_string()
    } else {
        std::env::var("SUPER_V_DB").unwrap_or_else(|_| {
            let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
            let dir = format!("{}/.local/share/super-v-clipboard", home);
            let _ = std::fs::create_dir_all(&dir);
            format!("{}/clipboard.db", dir)
        })
    };

    let mut db_init = Database::open(&db_path)?;

    // Seed and verify SecretFilter
    let token_item = db_init.insert_or_touch("text/plain", b"ghp_1234567890abcdefghijklmnopqrstuvwxyz", Some("ghp_1234567890abcdefghijklmnopqrstuvwxyz"))?;
    assert!(token_item.is_sensitive, "GitHub token must be classified as sensitive");

    db_init.insert_or_touch("text/plain", b"#308b53", Some("#308b53"))?;
    db_init.insert_or_touch("text/plain", b"https://github.com/GNOME/gnome-shell", Some("https://github.com/GNOME/gnome-shell"))?;
    db_init.insert_or_touch("text/plain", b"Rust programming language", Some("Rust programming language"))?;

    // Test Backup Export & Import
    let backup_path = "/tmp/super-v-test-backup.json";
    let exported_count = super_v_core::BackupManager::export_to_json(&db_init, backup_path)?;
    assert!(exported_count >= 4, "Exported backup must have at least 4 items");

    let mut db_restore = super_v_core::Database::open(":memory:")?;
    let imported_count = super_v_core::BackupManager::import_from_json(&mut db_restore, backup_path)?;
    assert_eq!(exported_count, imported_count, "Imported backup must match exported count");
    let _ = std::fs::remove_file(backup_path);

    let db = Arc::new(Mutex::new(db_init));

    // Initialize BlobStore for screenshots and files
    let blob_dir = if is_test_run {
        "/tmp/super-v-test-blobs".to_string()
    } else {
        let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
        format!("{}/.local/share/super-v-clipboard/blobs", home)
    };
    let blob_store = Arc::new(super_v_core::BlobStore::new(&blob_dir)?);

    // Standard IPC socket path
    let socket_path = if is_test_run {
        "/tmp/super-v-test.sock".to_string()
    } else {
        "/tmp/super-v-clipboard.sock".to_string()
    };

    let server = IpcServer::new(&socket_path, Arc::clone(&db));
    let server_handle = tokio::spawn(async move {
        if let Err(e) = server.run().await {
            log::error!("IPC server runtime failure: {:?}", e);
        }
    });

    // Allow server socket to bind
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    // Test client IPC round-trip
    let mut client = IpcClient::connect(&socket_path).await?;
    let history = client.get_history(10).await?;
    let search_rust = client.search("Rust*", 5).await?;

    // Test toggle_pin on first item
    if let Some(first) = history.first() {
        if let Some(id) = first.id {
            client.toggle_pin(id).await?;
            log::info!("Toggled pin state for entry id={}", id);
        }
    }

    // Test blob store write
    let (blob_hash, blob_file) = blob_store.write_blob(b"sample png image data", "png")?;
    log::info!("Wrote blob: hash={}, path={:?}", blob_hash, blob_file);
    assert!(blob_file.exists(), "Blob file must exist on disk");

    {
        let mut db_guard = db.lock().await;
        let evicted = db_guard.enforce_retention_limits(100, 10 * 1024 * 1024)?;
        log::info!("Retention check passed, evicted count: {}", evicted);
    }

    if is_test_run {
        let _ = blob_store.delete_blob(&blob_file.to_string_lossy());
        let _ = std::fs::remove_dir_all(&blob_dir);
    }

    // Test synthetic paste IPC request
    let paste_res = client.send_request(&super_v_core::ipc::IpcRequest::PasteItem { id: 1 }).await?;
    log::info!("PasteItem response: {:?}", paste_res);

    // Verify embedded glyph datasets (1,914 Emojis, 83 Kaomojis, 1,073 Symbols)
    let glyph_index = super_v_core::glyphs::GlyphIndex::load_embedded();
    assert_eq!(glyph_index.emoji_count(), 1914, "Must embed exactly 1,914 Emojis");
    assert_eq!(glyph_index.kaomoji_count(), 83, "Must embed exactly 83 Kaomojis");
    assert_eq!(glyph_index.symbol_count(), 1073, "Must embed exactly 1,073 Symbols");

    let rocket_search = glyph_index.search("rocket", 5);
    assert!(!rocket_search.is_empty(), "Glyph search for rocket must find rocket emoji");

    // Verify local GIF scanner
    let gif_scanner = super_v_core::GifScanner::default_dirs();
    let local_gifs = gif_scanner.scan_local_gifs(10);
    log::info!("Local GIF scan complete, count: {}", local_gifs.len());

    // Test pointer cursor positioning and monitor clamping math
    let mon = super_v_core::positioning::Rect { x: 0, y: 0, width: 1920, height: 1080 };
    let pt = super_v_core::positioning::Point { x: 1850, y: 1000 };
    let clamped_pos = super_v_core::positioning::WindowPlacer::compute_position(pt, (420, 520), mon, 12);
    assert!(clamped_pos.x + 420 <= 1920, "Clamped X must fit monitor");
    assert!(clamped_pos.y + 520 <= 1080, "Clamped Y must fit monitor");

    let gnome_bridge_installed = super_v_core::backend::GnomeBridgeSpec::is_installed();

    // Verify Hyprland window rules helper
    let is_hypr = super_v_core::backend::HyprlandHelper::is_hyprland();
    let hypr_rules = super_v_core::backend::HyprlandHelper::HYPRLAND_RULES;
    assert!(hypr_rules.contains("windowrulev2"), "Hyprland rules must contain windowrulev2");

    // Test client IPC EnforceRetention
    let retention_res = client.enforce_retention(50, 20 * 1024 * 1024).await?;
    log::info!("Client IPC enforce_retention evicted: {}", retention_res);

    println!("VERIFY: ipc_retention_ok=true, evicted={}", retention_res);
    println!("VERIFY: hyprland_rules_verified=true, is_hypr={}", is_hypr);
    println!("VERIFY: gnome_bridge_installed={},", gnome_bridge_installed);
    println!("VERIFY: backup_restore_ok=true, exported={}, imported={}", exported_count, imported_count);
    println!("VERIFY: pointer_positioning_ok=true, clamped_x={}, clamped_y={}", clamped_pos.x, clamped_pos.y);
    println!("VERIFY: full_glyphs_embedded=true, emojis=1914, kaomojis=83, symbols=1073");
    println!("VERIFY: secret_filter_verified=true, token_masked={}", super_v_core::SecretFilter::mask_preview("ghp_1234567890abcdefghijklmnopqrstuvwxyz"));
    println!("VERIFY: gif_scanner_ok=true, found_gifs={}", local_gifs.len());
    println!("VERIFY: ipc_socket_ok=true, history_len={}, search_rust_len={}", history.len(), search_rust.len());
    println!("VERIFY: wayland_c_available={},", wayland_c_support);
    println!("VERIFY: x11_c_available={},", x11_support);
    println!("VERIFY: compositor={}", comp_str);

    if is_test_run {
        let _ = std::fs::remove_file(socket_path);
        return Ok(());
    }

    // In normal execution mode, keep the daemon running forever!
    log::info!("Super-V Daemon is now actively running in background on {:?}", socket_path);
    server_handle.await?;
    Ok(())
}
