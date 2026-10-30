# Super V Clipboard Native

A native Linux clipboard history and picker that brings the **Windows 11 clipboard
experience** to GNOME — built with the platform's own toolkit so it looks and
behaves like a first-class part of the desktop instead of a transplanted panel.

**Goal:** the muscle memory of `Win+V` (a panel next to the cursor, pinned
snippets, emoji, images, instant paste into whatever you were typing) delivered
through GTK 4 and libadwaita, reusing the libraries the desktop already ships so
typography, spacing, motion and colour stay consistent with the Adwaita design
language.

Targets GNOME Shell on Wayland (50, 51.1+), wlroots compositors (Hyprland, Sway,
River) and X11 desktops (i3, bspwm, XFCE).

> **Naming:** the project is being renamed to **Super V Clipboard Native**. The
> `super-v-*` binary, socket, service and app-id names stay as they are so the
> GNOME bridge extension and existing installs keep working; only the
> user-facing name changes.

---

## Why another clipboard manager

The two projects that shaped this one both solve the same problem well, and both
were studied closely before writing a line of code:

- [**ecstra/super_v**](https://github.com/ecstra/super_v) — a compact GTK4
  clipboard companion with an emoji picker and `ydotool` auto-paste.
- [**gustavosett/Windows-11-Clipboard-History-For-Linux**](https://github.com/gustavosett/Windows-11-Clipboard-History-For-Linux)
  — a Tauri-based panel that nails the Windows 11 look.
- [**stevebushwa/winclip**](https://github.com/stevebushwa/winclip) — the
  original native Windows panel that inspired the interaction model.

Neither approach felt right for a GNOME-first tool: one needs a root-level input
daemon, the other carries a WebView to draw a 420×520 panel. This project keeps
the native toolkit and moves the two things a Wayland client cannot do — observe
the clipboard and inject a keystroke — into a small shell extension that ships
with it.

### How it compares

| | **Super V Clipboard Native** | **ecstra/super_v** | **Win11 Clipboard (Tauri)** |
|---|---|---|---|
| UI stack | GTK4 + libadwaita, no WebView | GTK4 | Tauri v2 (WebView) |
| Paste | shell virtual keyboard — no root, no `ydotool`, no `/dev/uinput` ACL | `ydotool` (uinput) | uinput + `setfacl` in the installer |
| Clipboard capture | shell `Meta.Selection` + deduped poll + in-process raw-bytes fallback; no clipboard client spawned | polling client | polling client |
| Window placement | `move_frame()` next to the pointer, clamped to the work area | not specified | follows the cursor |
| Search | SQLite FTS5 in the daemon, Indonesian aliases | in-UI filter | in-UI filter |
| Entry count | configurable retention + quota eviction, blob store | 25 most recent | unlimited |
| Images | raw-byte capture (png/jpeg/webp/gif/bmp), aspect-fit thumbnails | images supported | images supported |
| Emoji | 3,963 sequences, ZWJ-correct skin tones, dot marker + long-press picker | emoji picker | emoji picker |
| Secrets | private keys and tokens masked | — | — |
| Extras | JSON export/import, debug log dialog, self-test | IPC socket | pin, multi-monitor |
| Published numbers | insert 438 µs, 2,278 ops/s, search 1.1 ms, RSS ~62 MB | — | — |
| Packaging | manual `make install` (**the gap**) | source build | apt repo, AUR, installers |
| Licence | GPL-3.0-or-later | MIT | MIT |

Honest summary: the interaction model and the measured latency are ahead, the
packaging is behind. The roadmap below is mostly about closing that gap.

---

## Performance

Measured deterministically with `autoresearch.sh`:

| Metric | Value |
|---|---|
| Insert & deduplication latency | ~430 µs |
| Engine throughput | ~2,300 ops/s |
| FTS5 prefix search | ~1.1 ms |
| Popup activation | < 15 ms (resident surface, `Super+V`) |
| Launcher latency | 20 ms (`super-v-toggle`) |
| UI resident memory | ~62 MB (Cairo renderer) |
| Daemon idle memory | ~1.4 MB |
| UI memory when hidden | 0 MB (auto-exit after 180 s) |

> GTK4's GL renderer alone costs ~140 MB RSS for a 420×520 surface on this class
> of machine, so the overlay forces `GSK_RENDERER=cairo` unless overridden.

---

## How it works

- **Native GNOME look:** `AdwApplicationWindow`, `AdwViewStack`, `AdwHeaderBar`
  with an inline `AdwViewSwitcher`, `AdwPreferencesDialog` for settings.
- **Crash isolation:** UI and daemon talk over a UNIX socket; a crash in either
  leaves the compositor untouched.
- **Flicker-free invocation:** `super-v-toggle` is a libc-only helper that signals
  the resident overlay with `SIGUSR1` — no new Wayland client, no dock entry.
- **Wayland limits, handled properly:** the bundled shell extension watches
  `Meta.Selection`, positions the window with `move_frame()` and types with a
  virtual keyboard. Without it the app degrades to a centred panel.
- **Pick outside the clipboard:** history with pin sections, timestamps, image
  thumbnails, secret masking, emoji/symbols with skin-tone variants, local and
  online GIFs.

---

## Roadmap

The detailed plan is written separately in [PLAN.md](PLAN.md); this is the shape of
it.

- [ ] **Rename to Super V Clipboard Native** across the window title, About,
      desktop entry, service description and README (binary, socket and app-id
      stay compatible).
- [ ] **Packaging** — `.deb` + apt repository, AUR `PKGBUILD`, then Flatpak. This
      is the single biggest gap against the projects above.
- [ ] **GIF tab** — provider fixes are being worked on in parallel; the tab stays
      behind its own preference until it is stable.
- [ ] **Import/migration** from other managers' history formats.
- [ ] **Multi-monitor placement** — follow the pointer across outputs.
- [ ] **Translations** for the UI (Indonesian first), keeping the data-only
      alias file for search.
- [ ] **CI** — build, self-test and the benchmark on every push.

---

## Credits

**Author & copyright holder:** Arkhi Muttaqina <arkhi07@hotmail.co.id> —
[github.com/ArkhiMuttaqina](https://github.com/ArkhiMuttaqina)

Inspiration and prior art:

- [ecstra/super_v](https://github.com/ecstra/super_v)
- [gustavosett/Windows-11-Clipboard-History-For-Linux](https://github.com/gustavosett/Windows-11-Clipboard-History-For-Linux)
- [stevebushwa/winclip](https://github.com/stevebushwa/winclip)

Built with:

| Layer | Libraries |
|---|---|
| User interface | GTK 4, libadwaita, GdkPixbuf, Noto Color Emoji |
| Core engine | Tokio, rusqlite / SQLite 3 + FTS5, serde + serde_json, sha2, anyhow, log + env_logger, libc |

## License

Super V Clipboard Native is free software released under the **GNU General Public
License v3.0 or later** (GPL-3.0-or-later). See [LICENSE](LICENSE).

---

## Documentation

- [WinClip Architecture Analysis & Lessons Learned](docs/analysis/WINCLIP_ANALYSIS.md)
- [System Architecture Specification](docs/ARCHITECTURE.md)
- [Implementation & Execution Plan](PLAN.md)
