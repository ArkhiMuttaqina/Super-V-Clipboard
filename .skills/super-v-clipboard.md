# Skill: super-v-clipboard Development & Protocol Engineering

## Mission & Scope
Building and maintaining **super-v-clipboard** — an ultra-lightweight, crash-resilient Windows-style (`Super+V`) clipboard overlay written in **Rust + C**, targeting:
- **GNOME Shell (GNOME 48-51+ Wayland)**
- **Hyprland / wlroots Wayland Compositors**
- **Standard X11 sessions**

## Key Architectural Principles

### 1. Separation of Concerns (Dual-Tier Design)
* **Tier 1: Core Daemon & Engine (Rust + C FFI)**
  - Runs as an independent process (`super-v-clipboard-daemon`).
  - Never crashes the compositor.
  - Manages SQLite/redb storage, LRU cache, deduplication, search indexing, and cryptographic hashing.
* **Tier 2: UI Presentation Layer**
  - **Primary / GNOME Native:** GTK4 + `libadwaita` (compiled with native Wayland layer-shell support via `gtk4-layer-shell` or GNOME helper extension).
  - **Hyprland / wlroots:** `gtk4-layer-shell` with `zwlr_layer_shell_v1` overlay anchored at pointer coordinates or screen center.
  - **X11:** Standard override-redirect borderless window.

### 2. Multi-Compositor Protocol Matrix

| Desktop Environment | Clipboard Observation Protocol | Synthetic Paste Mechanism | Global Shortcut (`Super+V`) |
|---|---|---|---|
| **Hyprland / wlroots** | `zwlr_data_control_v1` / `wl-clipboard` | `wlr-virtual-keyboard-v1` or `ydotool` / uinput | `hyprctl bind` or Global Shortcuts Portal |
| **GNOME 50 / 51 Wayland** | Minimal GNOME Shell Helper Extension OR `org.freedesktop.portal.Clipboard` | GNOME Shell DBus Virtual Keyboard helper or `uinput` fallback | GNOME Custom Keybinding / DBus Activatable |
| **X11** | `XFixes` (`XFixesSelectSelectionInput`) | `XTestFakeKeyEvent` | `XGrabKey` (`Super+V`) |

### 3. Zero-Allocation Clipboard Pipeline (Rust)
- Zero-copy memory mapping for image caches (`memmap2`).
- Memory-safe image decoding via `image` or `zune-image` in background threads.
- SQLite WAL mode for concurrency with prepared statements and full-text search (`FTS5`).

### 4. Fast UI Conventions
- Keep activation latency under **16ms (1 frame at 60Hz)**.
- Pre-warm GTK4 context or keep the daemon resident in memory with hidden layer surface.
- Keyboard-first navigation: `Up`/`Down` for list, `1-9` quick paste, `Tab`/`Shift+Tab` for category switching, `/` for search, `Esc` to dismiss.
