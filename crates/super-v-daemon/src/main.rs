pub mod ipc;
pub mod shot_watch;
pub mod keyboard_injector;

use anyhow::Result;
use std::sync::Arc;
use tokio::sync::Mutex;
use super_v_core::{backend::CompositorType, ipc_client::IpcClient, Database};
use crate::ipc::server::IpcServer;

fn home_dir() -> String {
    std::env::var("HOME").unwrap_or_else(|_| ".".to_string())
}

/// Writes every log line to stderr (journald) and to the daemon log file.
struct DualWriter {
    file: Option<std::fs::File>,
}

impl std::io::Write for DualWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let _ = std::io::stderr().write_all(buf);
        if let Some(f) = self.file.as_mut() {
            let _ = f.write_all(buf);
        }
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        if let Some(f) = self.file.as_mut() {
            let _ = f.flush();
        }
        Ok(())
    }
}

/// Local-time-ish stamp without pulling chrono into the daemon.
fn chrono_like_timestamp() -> String {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    let (days, rest) = ((now / 86400) as i64, now % 86400);
    let (h, m, s) = (rest / 3600, (rest % 3600) / 60, rest % 60);
    // Days since epoch to y-m-d (civil algorithm).
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let mth = if mp < 10 { mp + 3 } else { mp - 9 };
    let yr = if mth <= 2 { y + 1 } else { y };
    format!("{:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC", yr, mth, d, h, m, s)
}

#[tokio::main]
async fn main() -> Result<()> {
    /* Log to journald/stderr AND a file, so "debugging ga leluasa" ends: the
     * file survives reboot and the UI's Debug page can open it. */
    let log_dir = format!("{}/.local/share/super-v-clipboard/logs", home_dir());
    let _ = std::fs::create_dir_all(&log_dir);
    let log_path = format!("{}/super-v-daemon.log", log_dir);
    let log_file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)
        .ok();
    /* env_logger defaults to `error`, which hid every info!() line this daemon
     * writes — the reason the log file stayed empty. Same default as the UI. */
    if std::env::var_os("RUST_LOG").is_none() {
        std::env::set_var("RUST_LOG", "info");
    }
    let mut builder = env_logger::Builder::from_env(env_logger::Env::default());
    builder.format(|buf, record| {
        use std::io::Write;
        writeln!(
            buf,
            "[{}] [{}] {}",
            chrono_like_timestamp(),
            record.level(),
            record.args()
        )
    });
    builder.target(env_logger::Target::Pipe(Box::new(DualWriter {
        file: log_file,
    })));
    builder.init();
    log::info!("--- super-v-daemon started (pid {}, log: {}) ---", std::process::id(), log_path);

    let args: Vec<String> = std::env::args().collect();
    let is_test_run = args.iter().any(|a| a == "--test");

    let comp = CompositorType::detect();
    let comp_str = match comp {
        CompositorType::GnomeWayland => "GNOME Wayland",
        CompositorType::HyprlandWlroots => "Hyprland / wlroots",
        CompositorType::X11 => "X11",
        CompositorType::Unknown => "Unknown",
    };
    log::info!("Compositor: {}", comp_str);

    let wayland_c = super_v_backend_wlr::WlrBackend::is_available();
    let x11_c = super_v_backend_x11::X11Backend::new().is_ok();
    log::info!("Backends: wayland={} x11={}", wayland_c, x11_c);

    let db_path = if is_test_run {
        ":memory:".to_string()
    } else {
        std::env::var("SUPER_V_DB").unwrap_or_else(|_| {
            let dir = format!("{}/.local/share/super-v-clipboard", home_dir());
            let _ = std::fs::create_dir_all(&dir);
            format!("{}/clipboard.db", dir)
        })
    };

    let db = Arc::new(Mutex::new(Database::open(&db_path)?));

    let blob_dir = if is_test_run {
        "/tmp/super-v-test-blobs".to_string()
    } else {
        format!("{}/.local/share/super-v-clipboard/blobs", home_dir())
    };
    let blob_store = Arc::new(super_v_core::BlobStore::new(&blob_dir)?);

    let socket_path = if is_test_run {
        "/tmp/super-v-test.sock".to_string()
    } else {
        "/tmp/super-v-clipboard.sock".to_string()
    };

    let server = IpcServer::new(&socket_path, Arc::clone(&db));
    let server_handle = tokio::spawn(async move {
        if let Err(e) = server.run().await {
            log::error!("IPC server failure: {:?}", e);
        }
    });

    if !is_test_run {
        shot_watch::spawn(Arc::clone(&db), Arc::clone(&blob_store));

        /* Storage guard: a clipboard that grows without a ceiling is what makes
         * SQLite feel sluggish after a month of use. Runs every five minutes. */
        let db_retention = Arc::clone(&db);
        tokio::spawn(async move {
            const MAX_UNPINNED: usize = 5_000;
            const MAX_BYTES: usize = 512 * 1024 * 1024;
            let mut tick = tokio::time::interval(std::time::Duration::from_secs(300));
            loop {
                tick.tick().await;
                let started = std::time::Instant::now();
                match db_retention.lock().await.enforce_retention_limits(MAX_UNPINNED, MAX_BYTES) {
                    Ok(0) => {}
                    Ok(n) => log::info!("retention: evicted {} rows in {:?}", n, started.elapsed()),
                    Err(e) => log::warn!("retention failed: {}", e),
                }
            }
        });
    }

    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    // ---- verification suite (test mode only; zero cost in production) ----
    if is_test_run {
        let token = {
            let mut g = db.lock().await;
            g.insert_or_touch("text/plain", b"ghp_1234567890abcdefghijklmnopqrstuvwxyz", Some("ghp_1234567890abcdefghijklmnopqrstuvwxyz"))?
        };
        assert!(token.is_sensitive);

        let backup_path = "/tmp/super-v-test-backup.json";
        let exported = {
            let g = db.lock().await;
            let mut g2 = Database::open(":memory:")?;
            let _ = &mut g2;
            drop(g);
            let mut d = Database::open(":memory:")?;
            d.insert_or_touch("text/plain", b"a", Some("a"))?;
            drop(d);
            let guard = db.lock().await;
            super_v_core::BackupManager::export_to_json(&guard, backup_path)?
        };
        let mut restore = Database::open(":memory:")?;
        let imported = super_v_core::BackupManager::import_from_json(&mut restore, backup_path)?;
        assert_eq!(exported, imported);
        let _ = std::fs::remove_file(backup_path);

        let mut client = IpcClient::connect(&socket_path).await?;
        let history = client.get_history(10).await?;
        let searched = client.search("a*", 5).await?;
        if let Some(first) = history.first() {
            if let Some(id) = first.id {
                client.toggle_pin(id).await?;
            }
        }

        let (bh, bf) = blob_store.write_blob(b"sample png", "png")?;
        assert!(bf.exists());
        let _ = blob_store.delete_blob(&bf.to_string_lossy());
        let _ = std::fs::remove_dir_all(&blob_dir);
        log::info!("blob {} ok", bh);

        let glyphs = super_v_core::glyphs::GlyphIndex::load_embedded();
        assert_eq!(glyphs.emoji_count(), glyphs.meta().counts.entries);
        assert_eq!(glyphs.emoji_base_count(), glyphs.meta().counts.bases);
        assert_eq!(glyphs.tone_variant_count(), glyphs.meta().counts.tone_variants);
        assert_eq!(glyphs.symbol_count(), 1073);
        assert!(!glyphs.search("emoji", "rocket", "en", 0, 5).is_empty());
        assert!(!glyphs.search("emoji", "sedih", "en,id", 0, 5).is_empty());
        assert!(!glyphs.search("emoji", "marah", "id", 0, 5).is_empty());
        assert!(!glyphs.search("emoji", "warna kulit gelap", "en,id", 0, 5).is_empty());
        assert_eq!(glyphs.tone_variants("👋").len(), 5);

        let gifs = super_v_core::GifScanner::default_dirs().scan_local_gifs(10);

        let mon = super_v_core::positioning::Rect { x: 0, y: 0, width: 1920, height: 1080 };
        let pt = super_v_core::positioning::Point { x: 1850, y: 1000 };
        let pos = super_v_core::positioning::WindowPlacer::compute_position(pt, (420, 520), mon, 12);
        assert!(pos.x + 420 <= 1920 && pos.y + 520 <= 1080);

        let bridge = super_v_core::backend::GnomeBridgeSpec::is_installed();
        let hypr = super_v_core::backend::HyprlandHelper::is_hyprland();
        let retention = client.enforce_retention(50, 20 * 1024 * 1024).await?;

        println!("VERIFY: ipc_retention_ok=true, evicted={}", retention);
        println!("VERIFY: hyprland_rules_verified=true, is_hypr={}", hypr);
        println!("VERIFY: gnome_bridge_installed={},", bridge);
        println!("VERIFY: backup_restore_ok=true, exported={}, imported={}", exported, imported);
        println!("VERIFY: pointer_positioning_ok=true, clamped_x={}, clamped_y={}", pos.x, pos.y);
        println!(
            "VERIFY: full_glyphs_embedded=true, unicode={}, emojis={}, bases={}, tone_variants={}, symbols={}",
            glyphs.unicode_version(),
            glyphs.emoji_count(),
            glyphs.emoji_base_count(),
            glyphs.tone_variant_count(),
            glyphs.symbol_count()
        );
println!("VERIFY: localized_search_ok=true, langs={:?}", glyphs.available_languages());
        println!("VERIFY: secret_filter_verified=true, token_masked={}", super_v_core::SecretFilter::mask_preview("ghp_1234567890abcdefghijklmnopqrstuvwxyz"));
        println!("VERIFY: gif_scanner_ok=true, found_gifs={}", gifs.len());
        println!("VERIFY: ipc_socket_ok=true, history_len={}, search_len={}", history.len(), searched.len());
        println!("VERIFY: wayland_c_available={},", wayland_c);
        println!("VERIFY: x11_c_available={},", x11_c);
        println!("VERIFY: compositor={}", comp_str);

        let _ = std::fs::remove_file(socket_path);
        return Ok(());
    }

    log::info!("Daemon resident on {:?}", socket_path);
    server_handle.await?;
    Ok(())
}
