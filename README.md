# Super-V Clipboard (Rust + C)

Super-V Clipboard is a blazing-fast, crash-resilient Windows-style (`Super+V`) clipboard manager and picker overlay written natively in **Rust + C**, targeting:
- **GNOME Shell (GNOME 50, 51.1+ Wayland)**
- **Hyprland / wlroots Wayland (Sway, River)**
- **X11 Desktops (i3, bspwm, XFCE, GNOME X11)**

Inspired by and upgraded from [WinClip](/home/arkhims/repo/WinClip).

---

## Key Features

- **Blazing Fast Activation (<15ms):** Pre-warmed GTK4 + libadwaita surface with hardware-accelerated Vulkan/NGL rendering.
- **Native Look & Feel:** Seamless integration with GNOME 51 and Adwaita design language.
- **Crash Isolation:** Decoupled client-daemon architecture. If the UI or daemon crashes, your desktop compositor remains 100% intact.
- **Cross-Compositor:** Automatically detects session environment (GNOME Wayland vs Hyprland/wlroots vs X11) and activates the matching protocol backend.
- **Rich Pickers:**
  - Clipboard history with pinning, deduplication, and SQLite FTS5 search
  - 1,914 searchable Emojis with skin-tone selector
  - 83 Kaomojis
  - 1,073 Math, Currency, Arrow, and Technical Symbols
  - Local GIF library and animated clipboard support
- **Instant Synthetic Paste:** Auto-pastes selected items directly into the active client window.

---

## Documentation

- [WinClip Architecture Analysis & Lessons Learned](docs/analysis/WINCLIP_ANALYSIS.md)
- [System Architecture Specification](docs/ARCHITECTURE.md)
- [Implementation & Execution Plan](PLAN.md)
- [Developer Skills](.skills/super-v-clipboard.md)
