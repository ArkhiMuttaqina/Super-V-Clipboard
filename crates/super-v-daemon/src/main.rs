use anyhow::Result;
use super_v_core::{backend::CompositorType, Database};

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
    
    let db_path = std::env::var("SUPER_V_DB").unwrap_or_else(|_| ":memory:".to_string());
    let mut db = Database::open(&db_path)?;
    
    // Seed and verify search
    db.insert_or_touch("text/plain", b"Important API token 12345", Some("Important API token 12345"))?;
    db.insert_or_touch("text/plain", b"Rust programming language", Some("Rust programming language"))?;
    
    let search_results = db.search("Rust*", 5)?;
    log::info!("Search results for 'Rust*': found {}", search_results.len());
    assert!(!search_results.is_empty(), "FTS5 search should find matched entry");
    
    // Verify native C FFI module link
    let x11_support = super_v_backend_x11::X11Backend::new().is_ok();
    log::info!("Native C X11 XTest availability: {}", x11_support);

    println!("VERIFY: x11_c_available={},", x11_support);
    println!("VERIFY: compositor={}, search_count={}", comp_str, search_results.len());
    Ok(())
}
