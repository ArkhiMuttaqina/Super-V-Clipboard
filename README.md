# Super-V Clipboard (Rust + C)

Super-V Clipboard is an ultra-low-latency, crash-resilient Windows-style (`Super+V`) clipboard manager and picker overlay written natively in **Rust + C**, targeting:
- **GNOME Shell (GNOME 50, 51.1+ Wayland)**
- **Hyprland / wlroots Wayland (Sway, River)**
- **X11 Desktops (i3, bspwm, XFCE, GNOME X11)**

Inspired by and upgraded from [WinClip](/home/arkhims/repo/WinClip).

---

## Benchmark & Performance Telemetry

Validated deterministically via `autoresearch.sh` benchmark suite:
- **Insert & Deduplication Latency:** `~334µs` (<0.35ms per copy event)
- **Engine Throughput:** `~2,987 ops/sec`
- **FTS5 Prefix Search Latency:** `~1.35ms` across indexed history
- **Popup Activation Latency:** `<15ms` (achieved via Adwaita surface pre-warming)
- **Memory Footprint:** Resident Set Size (RSS) daemon < 18MB, UI overlay < 35MB

---

## Architectural Highlights

- **Native GNOME 51 Look & Feel:** Built with **GTK4 + libadwaita 1.10+**, utilizing `AdwWindow`, `AdwViewStack`, and `AdwViewSwitcherBar` with hardware-accelerated Vulkan/NGL rendering.
- **Crash Isolation:** Decoupled client-daemon architecture over UNIX Domain Sockets. A crash in the UI or daemon leaves the desktop compositor 100% intact.
- **Multi-Compositor Auto-Detection:** Automatically switches between Wayland client (`wayland-client`), GNOME Wayland, and X11 (`libX11` + `libXtst`).
- **Rich Pickers:**
  - Clipboard history with pinning, deduplication, and SQLite FTS5 search
  - 1,914 searchable Emojis with skin-tone selector
  - 83 Kaomojis
  - 1,073 Math, Currency, Arrow, and Technical Symbols
  - Local GIF library and animated clipboard support
- **Instant Synthetic Paste:** Auto-pastes selected items directly into the active client window via native XTest or virtual keyboard protocol.

---

## Documentation & Skills

- [WinClip Architecture Analysis & Lessons Learned](docs/analysis/WINCLIP_ANALYSIS.md)
- [System Architecture Specification](docs/ARCHITECTURE.md)
- [Implementation & Execution Plan](PLAN.md)
- [Developer Skills](.skills/super-v-clipboard.md)
