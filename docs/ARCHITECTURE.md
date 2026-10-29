# Super-V Clipboard: Architecture Specification

## 1. Vision & Goals

**Super-V Clipboard** is a high-performance, cross-compositor Windows-style (`Super+V`) clipboard manager designed for the modern Linux desktop.

- **Primary Target Compositors:**
  - **GNOME Shell (GNOME 50, 51.1+ Wayland)**
  - **Hyprland & wlroots (Sway, River, Wayfire)**
  - **Standard X11 (i3, bspwm, XFCE, GNOME X11)**
- **Language Stack:** **Rust** (Safety, async runtime, SQLite FTS5, IPC daemon) + **C** (direct Wayland protocol bindings, X11 `libX11`/`libXtst`, low-level virtual device glue).
- **UI Framework:** **GTK4 + libadwaita** with `gtk4-layer-shell` for Hyprland/wlroots and custom surface integration for GNOME Shell.

---

## 2. System Architecture

```
                 ┌─────────────────────────────────────────────────┐
                 │              super-v-overlay (UI)               │
                 │         [GTK4 + libadwaita + Layer-Shell]        │
                 └────────────────────────▲────────────────────────┘
                                          │ UNIX Domain Socket / D-Bus
                                          ▼
                 ┌─────────────────────────────────────────────────┐
                 │             super-v-daemon (Engine)             │
                 │             [Rust + Tokio Runtime]              │
                 │  - SQLite (FTS5 Search, Metadata, Pins)         │
                 │  - Disk Blob Store (Images, GIFs, Thumbnails)   │
                 │  - Deduplication Engine & Secret Sanitizer      │
                 └───────▲─────────────────▲─────────────────▲─────┘
                         │                 │                 │
                         ▼                 ▼                 ▼
                 ┌───────────────┐ ┌───────────────┐ ┌───────────────┐
                 │ Backend:      │ │ Backend:      │ │ Backend:      │
                 │ GNOME Wayland │ │ Hyprland/wlr  │ │ X11           │
                 │ (Bridge/DBus) │ │ (DataControl) │ │ (XFixes/XTest)│
                 └───────────────┘ └───────────────┘ └───────────────┘
```

---

## 3. Subsystem Breakdown

### 3.1 Backend Abstraction Trait (`ClipboardBackend`)
```rust
pub trait ClipboardBackend: Send + Sync {
    /// Start listening for clipboard changes
    fn start_monitoring(&mut self, sender: tokio::sync::mpsc::Sender<ClipboardEvent>) -> Result<()>;
    
    /// Write entry to system clipboard
    fn set_clipboard(&self, mime: &str, data: &[u8]) -> Result<()>;
    
    /// Inject synthetic paste keypress (Ctrl+V) into active focused surface
    fn trigger_paste(&self) -> Result<()>;
    
    /// Capture current pointer coordinates for overlay positioning
    fn get_pointer_position(&self) -> Option<(i32, i32)>;
}
```

### 3.2 Data Storage Model
1. **Database:** SQLite with WAL mode + `fts5` virtual table for sub-millisecond prefix search.
2. **Tables:**
   - `entries` (id, hash, content_type, text_content, blob_path, is_pinned, created_at, last_used_at, byte_size)
   - `entries_fts` (FTS5 table indexing text_content)
   - `glyphs` (pre-loaded static database of Emoji, Kaomoji, Symbols with keyword index)
   - `tags` (favourite tags, GIF classifications)

---

## 4. UI Strategy: Why GTK4 + libadwaita?

1. **Native GNOME 51 Compatibility:** Matches GNOME Shell 51 CSS design tokens, Adwaita pill tabs, rounded surfaces, smooth spring physics, and system accent colors.
2. **GPU Acceleration:** GSK (GTK Scene Kit) renders with Vulkan/NGL shaders, delivering consistent 60–144 FPS animation.
3. **Layer Shell Support on Hyprland:** Seamless popups via `gtk4-layer-shell` (`zwlr_layer_shell_v1`).
4. **Wayland & X11 Dual-Stack:** Out-of-the-box backend switching between Wayland (`GdkWaylandDisplay`) and X11 (`GdkX11Display`).
