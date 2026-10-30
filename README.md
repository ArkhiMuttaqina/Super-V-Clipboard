# Super-V Clipboard (Rust + C)

Super-V Clipboard is an ultra-low-latency, crash-resilient Windows-style (`Super+V`) clipboard manager and picker overlay written natively in **Rust + C**, targeting:
- **GNOME Shell (GNOME 50, 51.1+ Wayland)**
- **Hyprland / wlroots Wayland (Sway, River)**
- **X11 Desktops (i3, bspwm, XFCE, GNOME X11)**

Inspired by and upgraded from WinClip.

---

## Benchmark & Performance Telemetry

Validated deterministically via `autoresearch.sh`:

| Metric | Value |
|---|---|
| Insert & deduplication latency | ~430 µs |
| Engine throughput | ~2,300 ops/sec |
| FTS5 prefix search | ~1.1 ms across indexed history |
| Popup activation | < 15 ms (resident surface, `Super+V`) |
| Launcher latency | 20 ms (`super-v-toggle`, libc only) |
| **UI resident memory** | **~57 MB** (Cairo renderer) |
| **Daemon idle memory** | **~1.4 MB** |
| UI memory when hidden | 0 MB (auto-exit after 180 s) |

> GTK4's GL renderer alone costs ~140 MB RSS for a 420x520 surface on this class of machine. The overlay forces `GSK_RENDERER=cairo` unless overridden, which is why the resident figure is 57 MB rather than 200 MB.

---

## Architectural Highlights

- **Native GNOME 51 Look & Feel:** GTK4 + libadwaita 1.10+, `AdwWindow`, `AdwViewStack`, `AdwHeaderBar` with an inline `AdwViewSwitcher`.
- **Crash Isolation:** Decoupled client-daemon architecture over UNIX domain sockets. A crash in the UI or daemon leaves the compositor untouched.
- **Flicker-free invocation:** `super-v-toggle` is a libc-only helper that signals the resident overlay with `SIGUSR1`. No new Wayland client, no dock entry flashing.
- **Multi-Compositor Auto-Detection:** Wayland client, GNOME Wayland bridge extension, and X11 (`libX11` + `libXtst`).
- **Rich Pickers:**
  - Clipboard history with pinning, delete, deduplication, secret masking and SQLite FTS5 search
  - **3,963 emoji** = every fully-qualified RGI sequence of Unicode Emoji 18.0, including 2,040 skin-tone variants, 1,614 ZWJ sequences and 262 flags, with **multilingual search** (see [docs/GLYPHS.md](docs/GLYPHS.md))
  - 1,073 math, currency, arrow, Greek and punctuation symbols (also searchable)
  - GIF tab: local library plus Tenor when an API key is configured
- **Instant Synthetic Paste:** Auto-pastes the selection into the active client via XTest or the virtual-keyboard protocol; the UI also owns the clipboard so manual `Ctrl+V` works.

### Multilingual emoji search

Search understands everyday words, not just Unicode names:

```
"sedih"  -> crying faces
"marah"  -> angry faces
"crying" -> crying faces
```

Languages ship in `data/glyphs/i18n.json`. Adding a language is a data-only change — open a PR with a new top-level key (for example `"jv"` for Javanese) mapping words to English Unicode name/keyword tokens. Preferences → *Emoji & Symbols* selects which languages are active (English + Indonesian by default).

Tone and gender queries work too: `warna kulit gelap`, `light skin tone`, `women holding hands`. Direct code points are accepted as `1f44b`, `U+1F44B` or `0x1F44B`, and Unicode shortcodes as `:wave:`.

### Emoji data pipeline

The dataset is generated, not curated: `tools/glyph-generator/generate.py` parses the pinned official Unicode Emoji 18.0 files plus CLDR 46 annotations and writes `data/glyphs/emojis.json`, `data/glyphs/glyph-meta.json` and `docs/glyph-coverage.md`.

```sh
python3 tools/glyph-generator/generate.py           # regenerate
python3 tools/glyph-generator/generate.py --check   # fail if stale
cargo test -p super-v-core --test glyph_tests       # coverage + search contract
cargo run --release -p super-v-core --bin bench_glyphs
```

Schema, skin-tone model, ranking rules, validation matrix and measured performance live in [docs/GLYPHS.md](docs/GLYPHS.md).

---

## Preferences

`F1`, the gear button, or the header menu:

- **General** — sort order, startup behaviour
- **Emoji & Symbols** — search languages, glyph set
- **GIF & Media** — provider (local / Tenor / Giphy / Klarna), rescan
- **About** — version, license, credits, bundled libraries

Settings persist to `~/.config/super-v-clipboard/settings.json`. Online GIF providers read `gif_api_key` from that file.

---

## Credits

**Author & copyright holder:** Arkhi Muttaqina &lt;arkhi07@hotmail.co.id&gt; — [github.com/ArkhiMuttaqina](https://github.com/ArkhiMuttaqina)

Built with:

| Layer | Libraries |
|---|---|
| User interface | GTK 4, libadwaita, GdkPixbuf, Noto Color Emoji |
| Core engine | Rust standard library, Tokio, rusqlite / SQLite 3 + FTS5, serde + serde_json, sha2, anyhow, log + env_logger, libc |

## License

Super-V Clipboard is free software released under the **GNU General Public License v3.0 or later** (GPL-3.0-or-later). See [LICENSE](LICENSE).

---

## Documentation

- [WinClip Architecture Analysis & Lessons Learned](docs/analysis/WINCLIP_ANALYSIS.md)
- [System Architecture Specification](docs/ARCHITECTURE.md)
- [Implementation & Execution Plan](PLAN.md)
