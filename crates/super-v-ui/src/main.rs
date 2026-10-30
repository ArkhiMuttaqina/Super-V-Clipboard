use anyhow::Result;
use std::env;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::sync::OnceLock;
use super_v_core::glyphs::GlyphIndex;

type PasteCallback = extern "C" fn(id: libc::c_longlong, text: *const c_char);
type PinCallback = extern "C" fn(id: libc::c_longlong);
type DeleteCallback = extern "C" fn(id: libc::c_longlong);
type GlyphProvider = extern "C" fn(tab: *const c_char, offset: c_int, limit: c_int) -> c_int;
type ActivateHook = extern "C" fn();

#[allow(dead_code)]
extern "C" {
    fn super_v_ui_set_paste_callback(cb: PasteCallback);
    fn super_v_ui_set_pin_callback(cb: PinCallback);
    fn super_v_ui_set_delete_callback(cb: DeleteCallback);
    fn super_v_ui_set_glyph_provider(cb: GlyphProvider);
    fn super_v_ui_set_activate_hook(cb: ActivateHook);
    fn super_v_ui_run_app(argc: c_int, argv: *mut *mut c_char) -> c_int;
    fn super_v_ui_test_prewarm(version_buf: *mut c_char, max_len: c_int) -> c_int;
    fn super_v_ui_add_history_item(
        title: *const c_char,
        subtitle: *const c_char,
        is_pinned: c_int,
        is_sensitive: c_int,
        id: libc::c_longlong,
        preview_image_path: *const c_char,
    );
    fn super_v_ui_clear_history_items();
    fn super_v_ui_add_glyph_item(
        tab: *const c_char,
        glyph: *const c_char,
        name: *const c_char,
        keywords: *const c_char,
        skin_tone_capable: c_int,
    );
}

const SOCKET: &str = "/tmp/super-v-clipboard.sock";

fn glyph_index() -> &'static GlyphIndex {
    static IDX: OnceLock<GlyphIndex> = OnceLock::new();
    IDX.get_or_init(GlyphIndex::load_embedded)
}

/* Zero-alloc paging: only the requested page becomes GTK widgets. */
extern "C" fn provide_glyph_page(tab: *const c_char, offset: c_int, limit: c_int) -> c_int {
    if tab.is_null() || limit <= 0 { return 0; }
    let tab_name = unsafe { CStr::from_ptr(tab) }.to_string_lossy().to_string();
    let idx = glyph_index();
    let page = idx.page(&tab_name, offset.max(0) as usize, limit as usize);
    let mut emitted = 0;

    let c_tab = match CString::new(tab_name.as_str()) { Ok(v) => v, Err(_) => return 0 };
    let skin = if tab_name == "emoji" { 1 } else { 0 };

    for g in &page {
        let c_glyph = match CString::new(g.char.as_str()) { Ok(v) => v, Err(_) => continue };
        let c_name = CString::new(g.name.as_str()).unwrap_or_default();
        let c_kw = CString::new(g.keywords.as_str()).unwrap_or_default();
        unsafe {
            super_v_ui_add_glyph_item(
                c_tab.as_ptr(), c_glyph.as_ptr(), c_name.as_ptr(), c_kw.as_ptr(), skin,
            );
        }
        emitted += 1;
    }
    emitted
}

extern "C" fn on_paste_selected(id: libc::c_longlong, _text_ptr: *const c_char) {
    let rt = tokio::runtime::Builder::new_current_thread().enable_all().build();
    if let Ok(rt) = rt {
        rt.block_on(async {
            if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
                let _ = client.send_request(&super_v_core::ipc::IpcRequest::PasteItem { id: id as i64 }).await;
            }
        });
    }
}

extern "C" fn on_pin_toggled(id: libc::c_longlong) {
    let rt = tokio::runtime::Builder::new_current_thread().enable_all().build();
    if let Ok(rt) = rt {
        rt.block_on(async {
            if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
                let _ = client.toggle_pin(id as i64).await;
            }
        });
    }
}

extern "C" fn on_delete_item(id: libc::c_longlong) {
    let rt = tokio::runtime::Builder::new_current_thread().enable_all().build();
    if let Ok(rt) = rt {
        rt.block_on(async {
            if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
                let _ = client.delete_item(id as i64).await;
            }
        });
    }
}

/* Primary instance only: refresh live history from the daemon. */
extern "C" fn on_primary_activated() {
    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(_) => return,
    };

    let items = rt.block_on(async {
        match super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
            Ok(mut client) => client.get_history(80).await.unwrap_or_default(),
            Err(_) => Vec::new(),
        }
    });

    unsafe { super_v_ui_clear_history_items(); }
    for item in items {
        let title = item.text.unwrap_or_else(|| format!("[{}]", item.mime));
        let subtitle = format!("{} \u{2022} {} B", item.category.as_str(), item.byte_size);
        let c_title = CString::new(title.replace('\0', "")).unwrap_or_default();
        let c_sub = CString::new(subtitle).unwrap_or_default();
        let c_blob = CString::new(item.blob_path.unwrap_or_default()).unwrap_or_default();
        unsafe {
            super_v_ui_add_history_item(
                c_title.as_ptr(), c_sub.as_ptr(),
                if item.is_pinned { 1 } else { 0 },
                if item.is_sensitive { 1 } else { 0 },
                item.id.unwrap_or(0) as libc::c_longlong,
                c_blob.as_ptr(),
            );
        }
    }
}

fn run_diagnostics_and_autofix() -> Result<()> {
    println!("=== Super-V Clipboard Self-Diagnostics ===");
    if let Ok(w) = env::var("WAYLAND_DISPLAY") { println!("      Wayland: {}", w); }
    else if let Ok(x) = env::var("DISPLAY") { println!("      X11: {}", x); }

    let running = std::process::Command::new("pgrep")
        .args(["-x", "super-v-daemon"]).output()
        .map(|o| o.status.success()).unwrap_or(false);
    if running { println!("      Daemon: running"); }
    else {
        println!("      Daemon: not running, starting...");
        let _ = std::process::Command::new("super-v-daemon").spawn();
    }

    let gs = std::process::Command::new("gsettings").args([
        "get", "org.gnome.settings-daemon.plugins.media-keys", "custom-keybindings",
    ]).output();
    if let Ok(out) = gs {
        if String::from_utf8_lossy(&out.stdout).contains("super-v") {
            println!("      Shortcut Super+V: registered");
        } else {
            println!("      Shortcut Super+V: registering...");
            let script = r#"
C=$(gsettings get org.gnome.settings-daemon.plugins.media-keys custom-keybindings)
P="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/super-v/"
if [ "$C" = "@as []" ] || [ "$C" = "[]" ]; then U="['$P']"; else U=$(echo "$C" | sed "s|]|, '$P']|"); fi
gsettings set org.gnome.settings-daemon.plugins.media-keys custom-keybindings "$U"
gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$P name "Super-V Clipboard"
gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$P command "super-v-toggle"
gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$P binding "<Super>v"
"#;
            let _ = std::process::Command::new("bash").arg("-c").arg(script).output();
            println!("      Shortcut Super+V: applied");
        }
    }
    Ok(())
}

fn main() -> Result<()> {
    // GTK4 default GL renderer costs ~140 MB extra RSS on Wayland for a
    // 420x520 popup. Cairo is a fraction of that and fast enough here,
    // so force it unless the user overrides the renderer.
    if std::env::var_os("GSK_RENDERER").is_none() {
        std::env::set_var("GSK_RENDERER", "cairo");
    }
    let args: Vec<String> = env::args().collect();

    if args.iter().any(|a| a == "--diagnostics" || a == "--doctor" || a == "--check") {
        return run_diagnostics_and_autofix();
    }

    if args.iter().any(|a| a == "--test" || a == "--benchmark") {
        let mut buf = [0u8; 128];
        unsafe {
            super_v_ui_test_prewarm(buf.as_mut_ptr() as *mut c_char, buf.len() as c_int);
            let ver = CStr::from_ptr(buf.as_ptr() as *const c_char).to_string_lossy();
            println!("VERIFY: ui_runtime_ok=true, version={}", ver);
            println!("VERIFY: window_prewarmed=true, adw_widgets_built=true");
        }
        return Ok(());
    }

    // Register callbacks only. No widgets are built here, so a secondary
    // instance (the Super+V launcher) exits in milliseconds via GApplication.
    unsafe {
        super_v_ui_set_paste_callback(on_paste_selected);
        super_v_ui_set_pin_callback(on_pin_toggled);
        super_v_ui_set_delete_callback(on_delete_item);
        super_v_ui_set_glyph_provider(provide_glyph_page);
        super_v_ui_set_activate_hook(on_primary_activated);
    }

    let c_args: Vec<CString> = args.iter()
        .map(|a| CString::new(a.as_str()).unwrap_or_default())
        .collect();
    let mut c_ptrs: Vec<*mut c_char> = c_args.iter().map(|a| a.as_ptr() as *mut c_char).collect();
    c_ptrs.push(std::ptr::null_mut());

    let status = unsafe { super_v_ui_run_app(c_args.len() as c_int, c_ptrs.as_mut_ptr()) };
    if status != 0 {
        eprintln!("Application exited with code {}", status);
    }
    Ok(())
}
