//! X11 clipboard watcher: the reliable capture path on GNOME Wayland.
//!
//! A background Wayland client cannot read the clipboard unless it has keyboard
//! focus, so the resident UI can only capture when it is open. XWayland has no
//! such restriction, so polling the CLIPBOARD selection from X11 sees every copy
//! from every application, whether it is Wayland or X11.

extern "C" {
    fn sv_x11watch_main(interval_ms: i32) -> i32;
}

fn main() {
    let interval = std::env::var("SUPER_V_X11WATCH_INTERVAL_MS")
        .ok()
        .and_then(|v| v.parse::<i32>().ok())
        .unwrap_or(400);
    let code = unsafe { sv_x11watch_main(interval) };
    std::process::exit(code);
}
