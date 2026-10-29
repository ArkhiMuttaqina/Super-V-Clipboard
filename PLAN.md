# Implementation & Execution Plan: Super-V Clipboard (Rust + C)

> **Sprint Objective:** Deliver a complete, robust, blazing-fast native clipboard overlay supporting GNOME 50/51, Hyprland/Wayland, and X11.

---

## Phase 1: Workspace & Core Engine Initialization
- [ ] Initialize Cargo workspace with crates:
  - `super-v-core` (domain models, storage, FTS5 search, hash deduplication)
  - `super-v-backend-wlr` (Hyprland / wlroots Wayland client via C FFI or `wayland-client`)
  - `super-v-backend-gnome` (GNOME Shell D-Bus bridge / Mutter portal adapter)
  - `super-v-backend-x11` (X11 XFixes & XTest C bindings)
  - `super-v-daemon` (Tokio async supervisor & IPC server)
  - `super-v-ui` (GTK4 + libadwaita overlay application)
- [ ] Implement SQLite schema with WAL mode & FTS5 full-text search.
- [ ] Port static datasets from WinClip:
  - 1,914 Emojis + search tags
  - 83 Kaomojis
  - 1,073 Symbols

---

## Phase 2: Compositor Backend Implementation

### 2.1 Hyprland / wlroots (`super-v-backend-wlr`)
- [ ] Implement `zwlr_data_control_v1` listener in C/Rust for background clipboard tracking without window focus.
- [ ] Implement `zwlr_virtual_keyboard_v1` or `uinput` synthetic paste injector.
- [ ] Integrate `gtk4-layer-shell` for popup positioning at cursor or center screen.

### 2.2 GNOME 50 / 51+ Wayland (`super-v-backend-gnome`)
- [ ] Build a lightweight bridge extension (`super-v-gnome-bridge`) exposing D-Bus signals for `MetaSelection::owner-changed` and synthetic input dispatch via Mutter virtual keyboard.
- [ ] Provide fallback via Freedesktop RemoteDesktop portal / uinput for standalone setups.

### 2.3 X11 (`super-v-backend-x11`)
- [ ] Bind `XFixesSelectSelectionInput` for clipboard change notifications.
- [ ] Bind `XTestFakeKeyEvent` for instant Ctrl+V injection.
- [ ] Bind `XGrabKey` for global `Super+V` intercept.

---

## Phase 3: GTK4 + libadwaita UI Construction
- [ ] Create `AdwApplicationWindow` with compact dimensions (420px x 480px) and dark/light adaptive styling.
- [ ] Build multi-tab navigation:
  - Tab 1: **Clipboard** (Text cards, image thumbnails with lazy loader, pin toggle, delete action).
  - Tab 2: **GIFs** (Disk cache scanner + local favorites).
  - Tab 3: **Emoji** (Grid view with category bar, quick-search filter).
  - Tab 4: **Kaomoji** (Flow-box layout for one-click insertion).
  - Tab 5: **Symbols** (Categorized unicode tables).
- [ ] Zero-latency activation: daemon pre-warms UI surface, toggles visibility instantly on `Super+V`.

---

## Phase 4: Verification & Benchmarking
- [ ] Measure launch & activation latency (Target: < 15ms).
- [ ] Memory footprint check (Target: Daemon RSS < 20MB, UI RSS < 40MB).
- [ ] Stress-test clipboard change churn (100 copies/sec).
- [ ] End-to-end paste validation across terminal, web browser, and text editors.
