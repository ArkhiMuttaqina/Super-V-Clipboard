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
   - `glyphs` (static, embedded datasets - see §3.3; no SQL table, no runtime parsing of external data)
   - `tags` (favourite tags, GIF classifications)

### 3.3 Glyph Engine (Emoji & Symbols)

The picker datasets are compiled into the binaries with `include_str!`; there is
no database, file read, or network access at runtime.

- `data/glyphs/emojis.json` - 3,963 sequences: every fully-qualified RGI entry of
  Unicode Emoji 18.0, including 2,040 skin-tone variants, 1,614 ZWJ sequences and
  262 flags.
- `data/glyphs/symbols.json` - 1,073 symbols (separate tab, unchanged).
- `data/glyphs/glyph-meta.json` - version pinning (Unicode, CLDR), group and
  subgroup tables, tone vocabulary, counts, and SHA-256 of every input file.
- `data/glyphs/i18n.json` - localized word -> English token dictionary.

Flow for one glyph cell:

1. C calls the registered `GlyphProvider` with `(tab, query, offset, limit)`.
2. Rust ranks hits in `GlyphIndex::search` (exact glyph -> code points -> name ->
   keywords -> token match) and calls `super_v_ui_add_glyph_item` per row, with a
   "has tone variants" flag taken from the dataset.
3. On right-click/long-press the overlay calls the `GlyphVariantsProvider`, which
   writes the exact RGI variant sequences (0x1F-separated) into the caller's
   buffer. C never composes a modifier sequence itself.
4. Selection goes through the existing paste path unchanged.

Browsing pages base glyphs only (1,923 cells); tone variants are reachable from
the popover and from tone-aware search. Index construction (~9 ms) happens on a
background thread at UI startup, so the GTK main thread never blocks.

Full schema, tone rules, search ranking, validation and benchmarks:
[docs/GLYPHS.md](GLYPHS.md).

---

## 4. UI Strategy: Why GTK4 + libadwaita?

1. **Native GNOME 51 Compatibility:** Matches GNOME Shell 51 CSS design tokens, Adwaita pill tabs, rounded surfaces, smooth spring physics, and system accent colors.
2. **GPU Acceleration:** GSK (GTK Scene Kit) renders with Vulkan/NGL shaders, delivering consistent 60–144 FPS animation.
3. **Layer Shell Support on Hyprland:** Seamless popups via `gtk4-layer-shell` (`zwlr_layer_shell_v1`).
4. **Wayland & X11 Dual-Stack:** Out-of-the-box backend switching between Wayland (`GdkWaylandDisplay`) and X11 (`GdkX11Display`).
