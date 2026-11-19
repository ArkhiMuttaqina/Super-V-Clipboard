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

## Discovered Lessons & Gotchas (self:: to be clear)
- **Static Glyph Structs:** When embedding pre-compiled datasets (Emoji, Kaomoji, Symbols), derive only `Serialize` on static borrowed slices (`&'static [&'static str]`). Deriving `Deserialize` fails compilation due to lack of runtime buffer ownership.
- **Process Decoupling:** Never run UI rendering in the same thread or process as clipboard monitoring. When Mutter or Hyprland events spike, a detached Tokio daemon ensures zero frame drop and zero compositor crashes.
- **Never synthesize emoji sequences in C.** RGI skin-tone sequences put each modifier immediately after its own person code point, so `man teacher` + dark is `U+1F468 U+1F3FB U+200D U+1F3EB`, not a trailing modifier. `tone_capable()`-style code-point ranges are always wrong for multi-person emoji; Rust owns `tone_variants()` and hands C a `0x1F`-separated list of exact sequences through a buffer-filling provider callback (no cross-allocator frees).
- **Generate emoji data, never hand-maintain it.** `tools/glyph-generator/generate.py` parses the pinned `sources/` (Unicode 18.0 + CLDR 46) and writes `data/glyphs/emojis.json` + `glyph-meta.json`; run it with `--check` in CI. Any hand-edited entry will be overwritten, so curated search words belong in `tools/glyph-generator/curated-keywords.json`.
- **Keep emoji keywords language-split.** `k` = English + curated (always matched), `ki` = Indonesian (matched only when `id` is in the language list). Merging both into one blob silently breaks the language toggle — the regression test asserts `sedih` with `en` returns nothing.
- **Browse bases, search variants.** The grid pages 1,923 base glyphs (`tones.is_empty()`); the 2,040 tone variants are only pulled in by a tone-word query or a code-point lookup, otherwise every hand emoji shows up six times.
- **Derive what the data can derive.** Variant names (`base + ": " + tone names`), keywords (base + tone vocabulary) and code points are recomputed in Rust at load, which keeps `emojis.json` at ~470 KB instead of ~1 MB. The generator stores a field only when the derivation does not match.
- **Warm the glyph index off the main thread.** Building the index costs ~9 ms (parse + variant composition + code-point maps); do it in a `std::thread` at UI startup so the emoji tab never stutters on the GTK main loop.
