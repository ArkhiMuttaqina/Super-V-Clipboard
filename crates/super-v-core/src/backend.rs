//! Compositor and clipboard backend abstraction trait.

use anyhow::Result;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CompositorType {
    GnomeWayland,
    HyprlandWlroots,
    X11,
    Unknown,
}

impl CompositorType {
    pub fn detect() -> Self {
        if std::env::var("WAYLAND_DISPLAY").is_ok() {
            if let Ok(desktop) = std::env::var("XDG_CURRENT_DESKTOP") {
                let d = desktop.to_lowercase();
                if d.contains("gnome") {
                    return Self::GnomeWayland;
                }
                if d.contains("hyprland") || d.contains("sway") || d.contains("wlroots") {
                    return Self::HyprlandWlroots;
                }
            }
            Self::HyprlandWlroots
        } else if std::env::var("DISPLAY").is_ok() {
            Self::X11
        } else {
            Self::Unknown
        }
    }
}

pub trait ClipboardBackend: Send + Sync {
    /// Start listening for clipboard changes
    fn start_monitoring(&mut self, sender: tokio::sync::mpsc::Sender<crate::ClipboardItem>) -> Result<()>;

    /// Write item to system clipboard
    fn set_clipboard(&self, mime: &str, data: &[u8]) -> Result<()>;

    /// Inject synthetic paste keypress (Ctrl+V) into active focused surface
    fn trigger_paste(&self) -> Result<()>;

    /// Capture pointer position for overlay placement
    fn get_pointer_position(&self) -> Option<(i32, i32)>;
}

/// GNOME 50/51 Shell Bridge constants
pub struct GnomeBridgeSpec;

impl GnomeBridgeSpec {
    pub const DBUS_NAME: &'static str = "my.id.arkhimuttaqina.SuperVBridge";
    pub const DBUS_PATH: &'static str = "/org/arkhimuttaqina/SuperVBridge";
    pub const EXTENSION_UUID: &'static str = "super-v-bridge@arkhimuttaqina.my.id";

    pub fn is_installed() -> bool {
        if let Ok(home) = std::env::var("HOME") {
            let path = std::path::PathBuf::from(home)
                .join(".local/share/gnome-shell/extensions")
                .join(Self::EXTENSION_UUID);
            path.exists()
        } else {
            false
        }
    }
}

/// Hyprland and Sway configuration helper
pub struct HyprlandHelper;

impl HyprlandHelper {
    pub const HYPRLAND_RULES: &'static str = r#"
# Super-V Clipboard Window Rules
windowrulev2 = float, class:(my.id.arkhimuttaqina.SuperVClipboard)
windowrulev2 = center, class:(my.id.arkhimuttaqina.SuperVClipboard)
windowrulev2 = stayfocused, class:(my.id.arkhimuttaqina.SuperVClipboard)
bind = SUPER, V, exec, super-v-ui
"#;

    pub fn is_hyprland() -> bool {
        std::env::var("HYPRLAND_INSTANCE_SIGNATURE").is_ok()
    }
}
