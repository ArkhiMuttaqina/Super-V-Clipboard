# WinClip Architecture Deep-Dive & Lessons Learned

> **Target Source:** `/home/arkhims/repo/WinClip` (GNOME Shell Extension in GJS/JavaScript)  
> **Destination:** `/home/arkhims/repo/super-v-clipboard` (Rewrite in Rust + C/Wayland Native)

---

## 1. Executive Summary

WinClip is a Windows 10/11-style clipboard overlay (`Super+V`) written as a GNOME Shell 45–50 Extension using GJS (GNOME JavaScript) and Clutter/St. It provides:
1. **Clipboard History:** Plaintext & images with pinned items, TTL, and size limits.
2. **Rich Pickers:** Emojis (1,914), Kaomoji (83), Symbols (1,073), and local GIF search/history.
3. **Instant Auto-Paste:** Pressing or clicking an entry immediately sends synthetic `Ctrl+V` to the target client.

---

## 2. Deep-Dive Component Breakdown

### 2.1 Clipboard Monitoring (`clipboardMonitor.js` & `clipboardSource.js`)
* **Mechanism:** GNOME Shell's `MetaSelection::owner-changed` signal.
* **Why it was an extension:** Mutter / GNOME Wayland deliberately does **not** implement the `wlr-data-control-unstable-v1` protocol. Standalone clipboard utilities like `wl-paste --watch` fail under pure GNOME Wayland sessions without portal or internal compositor access.
* **MIME Ordering:** Strict priority cascade:
  `image/gif` -> `image/webp` -> `image/png` -> `image/jpeg` -> `text/plain;charset=utf-8` -> `text/plain` -> `STRING` -> `UTF8_STRING`.
* **Deduplication:** Content hashing via MD5/SHA256, ignore self-pasted entries via token tracking.

### 2.2 Storage & Memory Management (`store.js`)
* **Persistence:** JSON-backed index saved to `~/.local/share/winclip/history.json`.
* **Binary Blobs:** Binary payload caching (images/GIFs) stored as individual files under `~/.local/share/winclip/blobs/<sha256>.<ext>`.
* **Pinning & Quotas:** Pinned items bypass eviction; unpinned items evicted by FIFO count limit (default 25-100) or total blob storage budget (e.g., 50MB-200MB).

### 2.3 Synthetic Paste Mechanism (`paste.js`)
* **Mechanism in WinClip:** Clutter Virtual Keyboard (`Clutter.get_default_backend().get_default_seat().create_virtual_device(KEYBOARD_DEVICE)`).
* **Sequence:**
  1. User selects item.
  2. Overlay hides, releases seat grab.
  3. Compositor restores focus to target app window.
  4. Timer triggers `Ctrl+V` (Keycode Down -> Delay -> Keycode Up).

### 2.4 Overlay UI & Rendering (`overlay.js`, `tabs.js`, `glyphTab.js`)
* **Toolkit:** Clutter / St (Shell Toolkit) running directly inside Mutter render loop.
* **Positioning:** Tracks mouse pointer position (`Clutter.get_default_backend().get_default_seat().get_pointer()`) and clamps within monitor geometry.
* **Tabs:**
  - Clipboard (History)
  - GIF Search (Inotify scanner for Pictures/Downloads + favorites)
  - Emoji Picker (Categorized unicode table + search keywords)
  - Kaomoji (Hardcoded text emoticons)
  - Symbols (Currency, Math, Arrows, Greek)

---

## 3. Structural Bottlenecks & Flaws in WinClip

| Aspect | WinClip (GJS Extension) | Limitation / Failure Mode |
|---|---|---|
| **Compositor Crash Risk** | Runs inside `gnome-shell` process. | Any JS unhandled exception or memory leak crashes the entire user desktop session. |
| **Portability** | Hardlocked to GNOME Shell JS APIs. | Zero support for Hyprland, Sway, KDE, or pure X11. |
| **API Instability** | GJS & Clutter APIs break every 1-2 GNOME releases (e.g. GNOME 45 ESM break, GNOME 50/51 deprecations). | Constant maintenance debt. |
| **Performance** | Garbage collection spikes during large image thumbnailing. | Micro-stutters during heavy typing or rapid `Super+V` toggles. |
