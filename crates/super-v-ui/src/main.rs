use anyhow::Result;
use std::env;
mod tools;

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::sync::OnceLock;
use super_v_core::glyphs::GlyphIndex;

type PasteCallback = extern "C" fn(id: libc::c_longlong, text: *const c_char);
type PinCallback = extern "C" fn(id: libc::c_longlong);
type DeleteCallback = extern "C" fn(id: libc::c_longlong);
type GlyphProvider = extern "C" fn(tab: *const c_char, query: *const c_char, offset: c_int, limit: c_int) -> c_int;
type GlyphVariantsProvider = extern "C" fn(glyph: *const c_char, out: *mut c_char, cap: c_int) -> c_int;
type ActivateHook = extern "C" fn();
type LanguageHook = extern "C" fn(langs: *const c_char);
type GifProviderHook = extern "C" fn(id: *const c_char);
type GifReloadHook = extern "C" fn();
type GifSearchHook = extern "C" fn(query: *const c_char);
type GifMoreHook = extern "C" fn();
type ApiKeyHook = extern "C" fn(key: *const c_char);
type ProviderTestHook = extern "C" fn(provider: *const c_char);
type PlacementHook = extern "C" fn(mode: *const c_char);
type ClipTextHook = extern "C" fn(text: *const c_char);
type ImageHook = extern "C" fn(path: *const c_char, mime: *const c_char);
type RefreshHook = extern "C" fn();
type UpdateHook = extern "C" fn();

#[allow(dead_code)]
extern "C" {
    fn super_v_ui_set_paste_callback(cb: PasteCallback);
    fn super_v_ui_set_pin_callback(cb: PinCallback);
    fn super_v_ui_set_delete_callback(cb: DeleteCallback);
    fn super_v_ui_set_glyph_provider(cb: GlyphProvider);
    fn super_v_ui_set_glyph_variants_provider(cb: GlyphVariantsProvider);
    fn super_v_ui_set_activate_hook(cb: ActivateHook);
    fn super_v_ui_set_language_hook(cb: LanguageHook);
    fn super_v_ui_set_gif_provider_hook(cb: GifProviderHook);
    fn super_v_ui_set_gif_reload_hook(cb: GifReloadHook);
    fn super_v_ui_set_gif_search_hook(cb: GifSearchHook);
    fn super_v_ui_set_gif_more_hook(cb: GifMoreHook);
    fn super_v_ui_post_to_main(fn_: extern "C" fn(data: *mut std::ffi::c_void), data: *mut std::ffi::c_void);
    fn super_v_ui_set_api_key_hook(cb: ApiKeyHook);
    fn super_v_ui_set_provider_test_hook(cb: ProviderTestHook);
    fn super_v_ui_set_placement_hook(cb: PlacementHook);
    fn super_v_ui_set_placement(mode: *const c_char);
    fn super_v_ui_set_clipboard_hook(cb: ClipTextHook);
    fn super_v_ui_set_image_hook(cb: ImageHook);
    fn super_v_ui_set_refresh_hook(cb: RefreshHook);
    fn super_v_ui_set_start_hidden(hidden: c_int);
    fn super_v_ui_set_live_update_hook(cb: UpdateHook);
    fn super_v_ui_set_api_key_text(key: *const c_char);
    fn super_v_ui_report_provider_test(message: *const c_char);
    fn super_v_ui_set_log_path(path: *const c_char);
    fn super_v_ui_apply_preferences(langs: *const c_char, gif_provider: *const c_char);
    fn super_v_ui_clear_glyphs(tab: *const c_char);
    fn super_v_ui_add_gif_item(path: *const c_char, title: *const c_char);
    fn super_v_ui_clear_gif_items();
    fn super_v_ui_set_gif_hint(text: *const c_char);
    fn super_v_ui_run_app(argc: c_int, argv: *mut *mut c_char) -> c_int;
    fn super_v_ui_test_prewarm(version_buf: *mut c_char, max_len: c_int) -> c_int;
    fn super_v_ui_add_history_item(
        title: *const c_char,
        subtitle: *const c_char,
        is_pinned: c_int,
        is_sensitive: c_int,
        id: libc::c_longlong,
        preview_image_path: *const c_char,
        created_at: libc::c_longlong,
        mime: *const c_char,
    );
    fn super_v_ui_clear_history_items();
    fn super_v_ui_add_glyph_item(
        tab: *const c_char,
        glyph: *const c_char,
        name: *const c_char,
        keywords: *const c_char,
        skin_tone_capable: c_int,
    );
}

const SOCKET: &str = "/tmp/super-v-clipboard.sock";

fn glyph_index() -> &'static GlyphIndex {
    static IDX: OnceLock<GlyphIndex> = OnceLock::new();
    IDX.get_or_init(GlyphIndex::load_embedded)
}

/// Building the index parses ~500 KB of embedded data and composes the variant
/// keyword tables, roughly 9 ms. Run it on a background thread at startup so the
/// first emoji tab never pays that cost on the GTK main thread.
fn warm_glyph_index() {
    std::thread::spawn(|| {
        let started = std::time::Instant::now();
        let index = glyph_index();
        append_log(&format!(
            "glyph index ready: unicode={} emojis={} bases={} tone_variants={} symbols={} in {}ms",
            index.unicode_version(),
            index.emoji_count(),
            index.emoji_base_count(),
            index.tone_variant_count(),
            index.symbol_count(),
            started.elapsed().as_millis()
        ));
    });
}

/* ------------------------------------------------------------------ settings */

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub(crate) struct Settings {
    emoji_languages: String,
    gif_provider: String,
    gif_api_key: String,
    #[serde(default = "default_placement")]
    placement: String,
    #[serde(default)]
    pub(crate) ai_provider: String,
    #[serde(default)]
    pub(crate) ai_model: String,
    #[serde(default)]
    pub(crate) ai_api_key: String,
    #[serde(default)]
    pub(crate) ai_base_url: String,
    #[serde(default)]
    pub(crate) ai_models: Vec<String>,
    pub(crate) snippets: Vec<tools::Snippet>,
}

fn default_placement() -> String {
    "pointer".to_string()
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            emoji_languages: "en,id".to_string(),
            gif_provider: "local".to_string(),
            gif_api_key: String::new(),
            placement: default_placement(),
            ai_provider: "auto".to_string(),
            ai_model: String::new(),
            ai_api_key: String::new(),
            ai_base_url: String::new(),
            ai_models: Vec::new(),
            snippets: Vec::new(),
        }
    }
}

pub(crate) fn config_dir() -> std::path::PathBuf {
    if let Ok(x) = std::env::var("XDG_CONFIG_HOME") {
        if !x.is_empty() {
            return std::path::PathBuf::from(x).join("super-v-clipboard");
        }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
    std::path::PathBuf::from(home).join(".config").join("super-v-clipboard")
}

fn settings_path() -> std::path::PathBuf {
    config_dir().join("settings.json")
}

pub(crate) fn load_settings() -> Settings {
    let path = settings_path();
    match std::fs::read_to_string(&path) {
        Ok(text) => serde_json::from_str(&text).unwrap_or_default(),
        Err(_) => Settings::default(),
    }
}

fn save_settings(cfg: &Settings) {
    let dir = config_dir();
    let _ = std::fs::create_dir_all(&dir);
    if let Ok(text) = serde_json::to_string_pretty(cfg) {
        let _ = std::fs::write(settings_path(), text);
    }
}

static LANGS: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());
static GIF_PROVIDER: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());
static GIF_QUERY: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

fn current_langs() -> String {
    LANGS.lock().map(|g| g.clone()).unwrap_or_else(|_| "en,id".to_string())
}

fn current_gif_provider() -> String {
    GIF_PROVIDER.lock().map(|g| g.clone()).unwrap_or_else(|_| "local".to_string())
}

fn cache_dir() -> std::path::PathBuf {
    let base = std::env::var("XDG_CACHE_HOME").ok().filter(|s| !s.is_empty())
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| {
            let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
            std::path::PathBuf::from(home).join(".cache")
        });
    base.join("super-v-clipboard").join("gifs")
}

/// One cache file per URL: the same GIF seen on two pages resolves to the same
/// path, so the picker can dedupe by path without re-downloading.
fn gif_cache_path(provider: &str, url: &str) -> std::path::PathBuf {
    use std::hash::{Hash, Hasher};
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    url.hash(&mut hasher);
    cache_dir().join(format!("{}-{}.gif", provider, hasher.finish()))
}

fn cached_gif_ok(path: &std::path::Path) -> bool {
    path.metadata().map(|m| m.len()).unwrap_or(0) > 0
}

/// Store a page of remote GIFs in the cache. Downloads run through one parallel
/// curl invocation: spawning 24 curls serialised TLS handshakes and took ~4-5 s,
/// which is why the grid felt dead while scrolling.
fn store_gifs(provider: &str, items: Vec<(String, String)>) -> Vec<(String, String)> {
    let dir = cache_dir();
    let _ = std::fs::create_dir_all(&dir);
    /* Two transfers writing the same destination race inside one parallel curl
     * run and can leave the file missing, so collapse duplicate URLs first. */
    let mut unique: Vec<(std::path::PathBuf, String, String)> = Vec::new();
    let mut seen: std::collections::HashSet<std::path::PathBuf> = std::collections::HashSet::new();
    for (url, title) in items {
        let path = gif_cache_path(provider, &url);
        if !seen.insert(path.clone()) {
            continue;
        }
        unique.push((path, url, title));
    }
    let entries = unique;

    {
        let pending: Vec<&(std::path::PathBuf, String, String)> =
            entries.iter().filter(|(path, _, _)| !cached_gif_ok(path)).collect();
        if !pending.is_empty() {
            let mut cmd = std::process::Command::new("curl");
            cmd.args(["-sS", "--parallel", "--parallel-max", "8", "--max-time", "25"]);
            for (path, url, _) in &pending {
                cmd.arg("-o").arg(path).arg(url);
            }
            let _ = cmd.status();
        }
    }

    let kept: Vec<(String, String)> = entries
        .into_iter()
        .filter(|(path, _, _)| cached_gif_ok(path))
        .map(|(path, _, title)| (path.to_string_lossy().to_string(), title))
        .collect();
    kept
}

const GIF_PAGE: usize = 24;
static GIF_OFFSET: std::sync::Mutex<usize> = std::sync::Mutex::new(0);
static GIF_POS: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());
static GIF_TOTAL: std::sync::Mutex<usize> = std::sync::Mutex::new(0);
static GIF_SEEN: std::sync::Mutex<Option<std::collections::HashSet<String>>> =
    std::sync::Mutex::new(None);
static GIF_EXHAUSTED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(true);
static GIF_INFLIGHT: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
static GIF_GEN: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

/// One fetched page, handed back to the GTK main loop by the worker thread.
struct GifPage {
    gen: u64,
    provider: String,
    offset: usize,
    next_pos: String,
    rows: Vec<(String, String)>,
    error: Option<String>,
    exhausted: bool,
    elapsed_ms: u128,
    query: String,
}

/// Tenor: featured feed without a query, search with one. `pos` is the cursor
/// Tenor hands back for the next page.
fn fetch_tenor(
    api_key: &str,
    query: &str,
    limit: usize,
    pos: &str,
) -> Result<(Vec<(String, String)>, String), String> {
    let encoded: String = query.trim().replace(' ', "+");
    let mut url = format!(
        "https://tenor.googleapis.com/v2/{}?key={}&limit={}&media_filter=tinygif",
        if encoded.is_empty() { "featured" } else { "search" },
        api_key,
        limit
    );
    if !encoded.is_empty() {
        url.push_str(&format!("&q={}", encoded));
    }
    if !pos.is_empty() {
        url.push_str(&format!("&pos={}", pos));
    }
    let json = curl_json(&url).ok_or_else(|| "network or curl error".to_string())?;
    if let Some(code) = json.pointer("/error/code") {
        let message = json
            .pointer("/error/message")
            .and_then(|m| m.as_str())
            .unwrap_or("request failed");
        return Err(format!("{message} ({code})"));
    }
    let next = json
        .get("next")
        .and_then(|n| n.as_str())
        .unwrap_or("")
        .to_string();
    let mut wanted: Vec<(String, String)> = Vec::new();
    for item in json
        .get("results")
        .and_then(|r| r.as_array())
        .into_iter()
        .flatten()
    {
        let gif_url = item
            .pointer("/media_formats/tinygif/url")
            .and_then(|u| u.as_str());
        let title = item
            .get("content_description")
            .and_then(|t| t.as_str())
            .unwrap_or("GIF");
        if let Some(gif_url) = gif_url {
            wanted.push((gif_url.to_string(), title.to_string()));
        }
    }
    Ok((store_gifs("tenor", wanted), next))
}

/// Giphy: trending feed without a query, search with one; paged by offset.
/// Prefers the downsampled media: the picker draws 84 px thumbnails, so a
/// 40-90 KB file beats the ~1 MB original.
fn fetch_giphy(
    api_key: &str,
    query: &str,
    limit: usize,
    offset: usize,
) -> Result<Vec<(String, String)>, String> {
    let encoded: String = query.trim().replace(' ', "+");
    let mut url = format!(
        "https://api.giphy.com/v1/gifs/{}?api_key={}&limit={}&offset={}&rating=pg",
        if encoded.is_empty() { "trending" } else { "search" },
        api_key,
        limit,
        offset
    );
    if !encoded.is_empty() {
        url.push_str(&format!("&q={}", encoded));
    }
    let json = curl_json(&url).ok_or_else(|| "network or curl error".to_string())?;
    if json.pointer("/meta/status").and_then(|s| s.as_u64()) != Some(200) {
        let message = json
            .get("message")
            .and_then(|m| m.as_str())
            .or_else(|| json.pointer("/meta/msg").and_then(|m| m.as_str()))
            .unwrap_or("request failed");
        return Err(message.to_string());
    }
    let mut wanted: Vec<(String, String)> = Vec::new();
    for item in json
        .get("data")
        .and_then(|d| d.as_array())
        .into_iter()
        .flatten()
    {
        let gif_url = item
            /* fixed_width keeps every frame at ~200 px wide; the downsampled
             * variants drop frames, and the larger originals cost far more to
             * decode per frame than an 84 px cell can use. */
            .pointer("/images/fixed_width/url")
            .or_else(|| item.pointer("/images/fixed_height/url"))
            .or_else(|| item.pointer("/images/original/url"))
            .and_then(|u| u.as_str());
        let title = item
            .get("title")
            .and_then(|t| t.as_str())
            .filter(|t| !t.is_empty())
            .unwrap_or("GIF");
        if let Some(gif_url) = gif_url {
            wanted.push((gif_url.to_string(), title.to_string()));
        }
    }
    Ok(store_gifs("giphy", wanted))
}

/// Add rows that were not shown yet, and return how many were appended.
fn append_gif_rows(rows: Vec<(String, String)>) -> usize {
    let mut guard = GIF_SEEN.lock().unwrap_or_else(|e| e.into_inner());
    let seen = guard.get_or_insert_with(std::collections::HashSet::new);
    let mut total = GIF_TOTAL.lock().unwrap_or_else(|e| e.into_inner());
    let mut added = 0usize;
    for (path, title) in rows {
        if !seen.insert(path.clone()) {
            continue;
        }
        let c_path = CString::new(path).unwrap_or_default();
        let c_title = CString::new(title).unwrap_or_default();
        unsafe { super_v_ui_add_gif_item(c_path.as_ptr(), c_title.as_ptr()); }
        *total += 1;
        added += 1;
    }
    added
}

fn set_gif_hint(text: &str) {
    if let Ok(hint_c) = CString::new(text) {
        unsafe { super_v_ui_set_gif_hint(hint_c.as_ptr()); }
    }
}

fn gif_hint(provider: &str, error: Option<&str>) -> String {
    match error {
        Some(message) => format!("{provider} error: {message}"),
        None => {
            let total = *GIF_TOTAL.lock().unwrap_or_else(|e| e.into_inner());
            if provider == "local" {
                format!("Local GIF library ({total})")
            } else {
                format!("{provider}: {total} GIFs")
            }
        }
    }
}

/// Fetch one page on a worker thread. Downloads take seconds; doing this on the
/// GTK main thread froze the picker while scrolling.
fn start_gif_page(provider: String, key: String, query: String) {
    let gen = GIF_GEN.load(std::sync::atomic::Ordering::Relaxed);
    let offset = *GIF_OFFSET.lock().unwrap_or_else(|e| e.into_inner());
    let pos = GIF_POS.lock().unwrap_or_else(|e| e.into_inner()).clone();
    GIF_INFLIGHT.store(true, std::sync::atomic::Ordering::Relaxed);

    std::thread::spawn(move || {
        let started = std::time::Instant::now();
        let result = match provider.as_str() {
            "giphy" => fetch_giphy(&key, &query, GIF_PAGE, offset).map(|rows| {
                let exhausted = rows.len() < GIF_PAGE;
                (rows, String::new(), exhausted)
            }),
            "tenor" => fetch_tenor(&key, &query, GIF_PAGE, &pos)
                .map(|(rows, next)| (rows, next, false)),
            _ => Ok((Vec::new(), String::new(), true)),
        };

        let page = match result {
            Ok((rows, next_pos, short)) => {
                let exhausted = short || (next_pos.is_empty() && provider == "tenor");
                GifPage {
                    gen,
                    provider,
                    offset,
                    next_pos,
                    exhausted,
                    rows,
                    error: None,
                    elapsed_ms: started.elapsed().as_millis(),
                    query: query.clone(),
                }
            }
            Err(err) => GifPage {
                gen,
                provider,
                offset,
                next_pos: String::new(),
                rows: Vec::new(),
                error: Some(err),
                exhausted: true,
                elapsed_ms: started.elapsed().as_millis(),
                query: query.clone(),
            },
        };
        unsafe {
            super_v_ui_post_to_main(
                deliver_gif_page,
                Box::into_raw(Box::new(page)) as *mut std::ffi::c_void,
            );
        }
    });
}

/// Runs on the GTK main thread: adds one scanned local GIF.
extern "C" fn deliver_gif_row(data: *mut std::ffi::c_void) {
    if data.is_null() {
        return;
    }
    let row = unsafe { Box::from_raw(data as *mut (String, String)) };
    append_gif_rows(vec![(row.0.clone(), row.1.clone())]);
}

/// Runs on the GTK main thread: the only place widgets are touched.
extern "C" fn deliver_gif_page(data: *mut std::ffi::c_void) {
    if data.is_null() {
        return;
    }
    let page = unsafe { Box::from_raw(data as *mut GifPage) };
    GIF_INFLIGHT.store(false, std::sync::atomic::Ordering::Relaxed);
    if page.gen != GIF_GEN.load(std::sync::atomic::Ordering::Relaxed) {
        return;
    }

    *GIF_OFFSET.lock().unwrap_or_else(|e| e.into_inner()) = page.offset + GIF_PAGE;
    if !page.next_pos.is_empty() {
        *GIF_POS.lock().unwrap_or_else(|e| e.into_inner()) = page.next_pos.clone();
    }
    GIF_EXHAUSTED.store(page.exhausted, std::sync::atomic::Ordering::Relaxed);
    append_gif_rows(page.rows);

    let hint = gif_hint(&page.provider, page.error.as_deref());
    let suffix = if page.query.is_empty() {
        String::new()
    } else {
        format!(" for '{}'", page.query)
    };
    append_log(&format!(
        "gif page: {hint} in {}ms{suffix}",
        page.elapsed_ms
    ));
    set_gif_hint(&hint);
}

fn load_gifs() {
    let provider = current_gif_provider();
    let query = GIF_QUERY.lock().map(|q| q.clone()).unwrap_or_default();
    unsafe { super_v_ui_clear_gif_items(); }

    GIF_GEN.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    GIF_INFLIGHT.store(false, std::sync::atomic::Ordering::Relaxed);
    GIF_EXHAUSTED.store(true, std::sync::atomic::Ordering::Relaxed);
    *GIF_OFFSET.lock().unwrap_or_else(|e| e.into_inner()) = 0;
    *GIF_POS.lock().unwrap_or_else(|e| e.into_inner()) = String::new();
    *GIF_TOTAL.lock().unwrap_or_else(|e| e.into_inner()) = 0;
    *GIF_SEEN.lock().unwrap_or_else(|e| e.into_inner()) = Some(std::collections::HashSet::new());

    /* The filesystem walk can take seconds on a big home directory; it must
     * never run on the GTK thread or the tab switch freezes the app. */
    std::thread::spawn(|| {
        let started = std::time::Instant::now();
        let items = super_v_core::GifScanner::default_dirs().scan_local_gifs(60);
        let elapsed = started.elapsed().as_millis();
        for item in items {
            let row = (item.path, item.filename);
            unsafe {
                super_v_ui_post_to_main(
                    deliver_gif_row,
                    Box::into_raw(Box::new(row)) as *mut std::ffi::c_void,
                );
            }
        }
        append_log(&format!("gif local scan: {} ms", elapsed));
    });

    let cfg = load_settings();
    if provider == "local" {
        let hint = gif_hint("local", None);
        append_log(&format!("gif load: {hint}"));
        set_gif_hint(&hint);
        return;
    }
    if cfg.gif_api_key.is_empty() {
        set_gif_hint(&format!("{provider}: add the API key in Preferences to load GIFs"));
        return;
    }

    GIF_EXHAUSTED.store(false, std::sync::atomic::Ordering::Relaxed);
    set_gif_hint(&format!("{provider}: loading…"));
    start_gif_page(provider, cfg.gif_api_key, query);
}

/// Next page when the grid is scrolled to the bottom.
fn load_more_gifs() {
    let provider = current_gif_provider();
    if provider == "local"
        || GIF_EXHAUSTED.load(std::sync::atomic::Ordering::Relaxed)
        || GIF_INFLIGHT.load(std::sync::atomic::Ordering::Relaxed)
    {
        return;
    }
    let cfg = load_settings();
    if cfg.gif_api_key.is_empty() {
        GIF_EXHAUSTED.store(true, std::sync::atomic::Ordering::Relaxed);
        return;
    }
    let query = GIF_QUERY.lock().map(|q| q.clone()).unwrap_or_default();
    start_gif_page(provider, cfg.gif_api_key, query);
}

extern "C" fn on_language_changed(langs: *const c_char) {
    if langs.is_null() {
        return;
    }
    let value = unsafe { CStr::from_ptr(langs) }.to_string_lossy().to_string();
    if let Ok(mut guard) = LANGS.lock() {
        *guard = value.clone();
    }
    append_log(&format!("emoji languages -> {}", value));
    let mut cfg = load_settings();
    cfg.emoji_languages = value;
    save_settings(&cfg);
}

extern "C" fn on_gif_provider_changed(provider: *const c_char) {
    if provider.is_null() {
        return;
    }
    let value = unsafe { CStr::from_ptr(provider) }.to_string_lossy().to_string();
    if let Ok(mut guard) = GIF_PROVIDER.lock() {
        *guard = value.clone();
    }
    append_log(&format!("gif provider -> {}", value));
    let mut cfg = load_settings();
    cfg.gif_provider = value;
    save_settings(&cfg);
}

extern "C" fn on_gif_search(query: *const c_char) {
    if query.is_null() {
        return;
    }
    let value = unsafe { CStr::from_ptr(query) }.to_string_lossy().to_string();
    if let Ok(mut guard) = GIF_QUERY.lock() {
        if *guard == value {
            return;
        }
        *guard = value;
    }
    /* GIF feature disabled: no provider fetch. */
}

extern "C" fn on_gif_more() {
    load_more_gifs();
}

extern "C" fn on_gif_reload() {
    /* GIF feature removed: nothing to rescan. */
    append_log("gif disabled");
}


/* -------------------------------------------------------------------- logging */

fn data_dir() -> std::path::PathBuf {
    let base = std::env::var("XDG_DATA_HOME").ok().filter(|s| !s.is_empty())
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| {
            let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
            std::path::PathBuf::from(home).join(".local").join("share")
        });
    base.join("super-v-clipboard")
}

fn log_file() -> std::path::PathBuf {
    data_dir().join("logs").join("super-v-ui.log")
}

/// Civil date from Unix days (Howard Hinnant's algorithm), so we can stamp
/// log lines without pulling in a date crate.
fn civil_from_days(z: i64) -> (i64, u32, u32) {
    let z = z + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = (z - era * 146097) as u64;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (if m <= 2 { y + 1 } else { y }, m, d)
}

fn timestamp() -> String {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64;
    let (y, m, d) = civil_from_days(now.div_euclid(86400));
    let secs = now.rem_euclid(86400);
    format!("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", y, m, d, secs / 3600, (secs % 3600) / 60, secs % 60)
}

pub(crate) fn append_log(message: &str) {
    use std::io::Write;
    let path = log_file();
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    if let Ok(meta) = std::fs::metadata(&path) {
        if meta.len() > 2 * 1024 * 1024 {
            let _ = std::fs::rename(&path, path.with_extension("log.1"));
        }
    }
    if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        let _ = writeln!(file, "[{}] {}", timestamp(), message);
    }
    eprintln!("[{}] {}", timestamp(), message);
}

fn init_logging() {
    let path = log_file();
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    if std::env::var_os("RUST_LOG").is_none() {
        std::env::set_var("RUST_LOG", "info");
    }
    let _ = env_logger::Builder::from_env(env_logger::Env::default()).try_init();
    std::panic::set_hook(Box::new(|info| {
        let location = info.location().map(|l| format!("{}:{}", l.file(), l.line())).unwrap_or_default();
        append_log(&format!("PANIC at {}: {}", location, info));
        let bt = std::backtrace::Backtrace::force_capture();
        append_log(&format!("backtrace: {}", bt));
    }));
    append_log(&format!("--- super-v-ui {} started (pid {}) ---", env!("CARGO_PKG_VERSION"), std::process::id()));
    append_log(&format!("renderer={:?} wayland={:?} display={:?}",
        std::env::var("GSK_RENDERER").ok(),
        std::env::var("WAYLAND_DISPLAY").ok(),
        std::env::var("DISPLAY").ok()));
}

/* ------------------------------------------------------------ provider tests */

fn test_local_provider() -> String {
    let gifs = super_v_core::GifScanner::default_dirs().scan_local_gifs(200);
    if gifs.is_empty() {
        "FAIL local: no GIF found in ~/Downloads, ~/Pictures, ~/Videos, ~/Documents".to_string()
    } else {
        format!("PASS local: {} GIF(s) indexed", gifs.len())
    }
}

fn curl_json(url: &str) -> Option<serde_json::Value> {
    let out = std::process::Command::new("curl")
        .args(["-sS", "--max-time", "8", url])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    serde_json::from_slice(&out.stdout).ok()
}

fn test_online_provider(provider: &str, key: &str) -> String {
    if key.is_empty() {
        return format!("FAIL {}: API key required — paste the key above first", provider);
    }
    match provider {
        "tenor" => {
            let url = format!("https://tenor.googleapis.com/v2/featured?key={}&limit=3&media_filter=tinygif", key);
            match curl_json(&url) {
                Some(v) => match v.get("results").and_then(|r| r.as_array()) {
                    Some(rows) if !rows.is_empty() => format!("PASS tenor: {} GIF(s) returned", rows.len()),
                    Some(_) => "FAIL tenor: key accepted but no results".to_string(),
                    None => {
                        let err = v.get("error").and_then(|e| e.get("message")).and_then(|m| m.as_str())
                            .unwrap_or("unexpected response");
                        format!("FAIL tenor: {}", err)
                    }
                },
                None => "FAIL tenor: network or curl error".to_string(),
            }
        }
        "giphy" => {
            let url = format!("https://api.giphy.com/v1/gifs/trending?api_key={}&limit=3", key);
            match curl_json(&url) {
                Some(v) => match v.get("data").and_then(|r| r.as_array()) {
                    Some(rows) if !rows.is_empty() => format!("PASS giphy: {} GIF(s) returned", rows.len()),
                    Some(_) => "FAIL giphy: key accepted but no results".to_string(),
                    None => {
                        let err = v.get("message").and_then(|m| m.as_str()).unwrap_or("unexpected response");
                        format!("FAIL giphy: {}", err)
                    }
                },
                None => "FAIL giphy: network or curl error".to_string(),
            }
        }
        "klarna" => "FAIL klarna: no public API — pick Tenor or Giphy".to_string(),
        other => format!("FAIL {}: unknown provider", other),
    }
}

extern "C" fn on_api_key_changed(key: *const c_char) {
    if key.is_null() {
        return;
    }
    let value = unsafe { CStr::from_ptr(key) }.to_string_lossy().to_string();
    let mut cfg = load_settings();
    if cfg.gif_api_key == value {
        return;
    }
    cfg.gif_api_key = value;
    save_settings(&cfg);
    append_log("gif api key updated");
}

extern "C" fn on_placement_changed(mode: *const c_char) {
    if mode.is_null() {
        return;
    }
    let value = unsafe { CStr::from_ptr(mode) }.to_string_lossy().to_string();
    let mut cfg = load_settings();
    cfg.placement = value.clone();
    save_settings(&cfg);
    append_log(&format!("placement -> {}", value));
}

extern "C" fn on_provider_test(provider: *const c_char) {
    let provider = if provider.is_null() {
        "local".to_string()
    } else {
        unsafe { CStr::from_ptr(provider) }.to_string_lossy().to_string()
    };
    let cfg = load_settings();
    let result = if provider == "local" {
        test_local_provider()
    } else {
        test_online_provider(&provider, &cfg.gif_api_key)
    };
    append_log(&format!("provider test: {}", result));
    if let Ok(c) = CString::new(result) {
        unsafe { super_v_ui_report_provider_test(c.as_ptr()); }
    }
}

/* Zero-alloc paging: only the requested page becomes GTK widgets. */
extern "C" fn provide_glyph_page(tab: *const c_char, query: *const c_char, offset: c_int, limit: c_int) -> c_int {
    if tab.is_null() || limit <= 0 {
        return 0;
    }
    let tab_name = unsafe { CStr::from_ptr(tab) }.to_string_lossy().to_string();
    let query_str = if query.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(query) }.to_string_lossy().to_string()
    };

    if tab_name == "gif" {
        return 0;
    }

    let langs = current_langs();
    let idx = glyph_index();
    let page = idx.search(&tab_name, &query_str, &langs, offset.max(0) as usize, limit as usize);

    let c_tab = match CString::new(tab_name.as_str()) { Ok(v) => v, Err(_) => return 0 };
    let skin_tab = tab_name == "emoji";
    let mut emitted = 0;

    for g in &page {
        let c_glyph = match CString::new(g.char.as_str()) { Ok(v) => v, Err(_) => continue };
        let c_name = CString::new(g.name.as_str()).unwrap_or_default();
        let c_kw = CString::new(g.keywords.as_str()).unwrap_or_default();
        let capable = skin_tab && idx.has_tone_variants(&g.char);
        unsafe {
            super_v_ui_add_glyph_item(
                c_tab.as_ptr(), c_glyph.as_ptr(), c_name.as_ptr(), c_kw.as_ptr(),
                if capable { 1 } else { 0 },
            );
        }
        emitted += 1;
    }
    emitted
}

/// Hands the exact RGI skin-tone variants for `glyph` to the C overlay as a
/// 0x1F-separated list written into the caller's buffer, so the overlay never
/// has to guess where a modifier belongs.
extern "C" fn provide_glyph_variants(glyph: *const c_char, out: *mut c_char, cap: c_int) -> c_int {
    if glyph.is_null() || out.is_null() || cap <= 1 {
        return -1;
    }
    let base = unsafe { CStr::from_ptr(glyph) }.to_string_lossy().to_string();
    let idx = glyph_index();
    let mut joined = String::new();
    for variant in idx.tone_variants(&base) {
        if !joined.is_empty() {
            joined.push(UNIT_SEPARATOR);
        }
        joined.push_str(&variant.char);
    }
    if joined.is_empty() {
        return -1;
    }
    let bytes = joined.as_bytes();
    let cap = cap as usize;
    let mut n = bytes.len().min(cap - 1);
    while n > 0 && n < bytes.len() && (bytes[n] & 0xC0) == 0x80 {
        n -= 1;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, n);
        *out.add(n) = 0;
    }
    n as c_int
}

const UNIT_SEPARATOR: char = '\u{1f}';

extern "C" fn on_clipboard_image(path: *const c_char, mime_ptr: *const c_char) {
    if path.is_null() {
        return;
    }
    let file = unsafe { CStr::from_ptr(path) }.to_string_lossy().to_string();
    let mime = if mime_ptr.is_null() {
        "image/png".to_string()
    } else {
        unsafe { CStr::from_ptr(mime_ptr) }.to_string_lossy().to_string()
    };
    append_log(&format!("clipboard image -> {}", file));
    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(_) => return,
    };
    rt.block_on(async {
        if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
            let _ = client
                .send_request(&super_v_core::ipc::IpcRequest::IngestFile {
                    mime,
                    path: file,
                })
                .await;
        }
    });
}

extern "C" fn on_clipboard_text(text: *const c_char) {
    if text.is_null() {
        return;
    }
    let value = unsafe { CStr::from_ptr(text) }.to_string_lossy().to_string();
    if value.trim().is_empty() {
        return;
    }
    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(_) => return,
    };
    let preview: String = value.chars().take(40).collect();
    append_log(&format!("clipboard changed: {} chars [{}]", value.chars().count(), preview));
    rt.block_on(async {
        if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
            let _ = client
                .send_request(&super_v_core::ipc::IpcRequest::Ingest {
                    mime: "text/plain".to_string(),
                    text: value,
                })
                .await;
        }
    });
}
extern "C" fn on_paste_selected(id: libc::c_longlong, text_ptr: *const c_char) {
    let rt = tokio::runtime::Builder::new_current_thread().enable_all().build();
    let ok = match rt {
        Ok(rt) => rt,
        Err(_) => return,
    };
    let glyph = if text_ptr.is_null() {
        None
    } else {
        Some(unsafe { CStr::from_ptr(text_ptr) }.to_string_lossy().to_string())
    };
    ok.block_on(async {
        if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
            /* id 0 with no text means the clipboard is already prepared (image or
             * GIF paste); only the keystroke is missing. */
            let request = if id > 0 {
                super_v_core::ipc::IpcRequest::PasteItem { id: id as i64 }
            } else {
                super_v_core::ipc::IpcRequest::InjectPaste
            };
            let _ = client.send_request(&request).await;
        }
    });
}

extern "C" fn on_pin_toggled(id: libc::c_longlong) {
    let rt = tokio::runtime::Builder::new_current_thread().enable_all().build();
    if let Ok(rt) = rt {
        rt.block_on(async {
            if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
                let _ = client.toggle_pin(id as i64).await;
            }
        });
    }
}

extern "C" fn on_delete_item(id: libc::c_longlong) {
    let rt = tokio::runtime::Builder::new_current_thread().enable_all().build();
    if let Ok(rt) = rt {
        rt.block_on(async {
            if let Ok(mut client) = super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
                let _ = client.delete_item(id as i64).await;
            }
        });
    }
}

/* Primary instance only: refresh live history from the daemon. */
fn populate_history(items: Vec<super_v_core::ClipboardItem>) {
    for item in items {
        let title = item.text.unwrap_or_else(|| format!("[{}]", item.mime));
        let subtitle = format!("{} \u{2022} {} B", item.category.as_str(), item.byte_size);
        let c_title = CString::new(title.replace('\0', "")).unwrap_or_default();
        let c_sub = CString::new(subtitle).unwrap_or_default();
        let c_blob = CString::new(item.blob_path.unwrap_or_default()).unwrap_or_default();
        let c_mime = CString::new(item.mime.clone()).unwrap_or_default();
        unsafe {
            super_v_ui_add_history_item(
                c_title.as_ptr(), c_sub.as_ptr(),
                if item.is_pinned { 1 } else { 0 },
                if item.is_sensitive { 1 } else { 0 },
                item.id.unwrap_or(0) as libc::c_longlong,
                c_blob.as_ptr(),
                item.created_at as libc::c_longlong,
                c_mime.as_ptr(),
            );
        }
    }
}

/// Newest id plus row count: cheap way to notice that the daemon stored something.
fn history_signature(items: &[super_v_core::ClipboardItem]) -> (i64, usize) {
    let top = items.first().and_then(|i| i.id).unwrap_or(0);
    (top, items.len())
}

extern "C" fn on_live_update() {
    use std::sync::atomic::{AtomicI64, AtomicUsize, Ordering};
    static LAST_TOP: AtomicI64 = AtomicI64::new(-1);
    static LAST_COUNT: AtomicUsize = AtomicUsize::new(0);

    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(_) => return,
    };
    let items = rt.block_on(async {
        match super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
            Ok(mut client) => client.get_history(80).await.unwrap_or_default(),
            Err(_) => Vec::new(),
        }
    });

    let (top, count) = history_signature(&items);
    if top == LAST_TOP.load(Ordering::Relaxed) && count == LAST_COUNT.load(Ordering::Relaxed) {
        return;
    }
    LAST_TOP.store(top, Ordering::Relaxed);
    LAST_COUNT.store(count, Ordering::Relaxed);

    unsafe { super_v_ui_clear_history_items(); }
    populate_history(items);
}

extern "C" fn on_refresh_history() {
    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(_) => return,
    };
    let items = rt.block_on(async {
        match super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
            Ok(mut client) => client.get_history(80).await.unwrap_or_default(),
            Err(_) => Vec::new(),
        }
    });
    unsafe { super_v_ui_clear_history_items(); }
    populate_history(items);
}

extern "C" fn on_primary_activated() {
    let rt = match tokio::runtime::Builder::new_current_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(_) => return,
    };

    let items = rt.block_on(async {
        match super_v_core::ipc_client::IpcClient::connect(SOCKET).await {
            Ok(mut client) => client.get_history(80).await.unwrap_or_default(),
            Err(_) => Vec::new(),
        }
    });

    unsafe { super_v_ui_clear_history_items(); }
    let boot_cfg = load_settings();
    if let Ok(k) = CString::new(boot_cfg.gif_api_key) {
        unsafe { super_v_ui_set_api_key_text(k.as_ptr()); }
    }
    if let Ok(p) = CString::new(boot_cfg.placement) {
        unsafe { super_v_ui_set_placement(p.as_ptr()); }
    }
    {
        let langs = CString::new(current_langs()).unwrap_or_default();
        let gif = CString::new(current_gif_provider()).unwrap_or_default();
        unsafe { super_v_ui_apply_preferences(langs.as_ptr(), gif.as_ptr()); }
    }
    populate_history(items);
}

fn run_diagnostics_and_autofix() -> Result<()> {
    println!("=== Super-V Clipboard Self-Diagnostics ===");
    if let Ok(w) = env::var("WAYLAND_DISPLAY") { println!("      Wayland: {}", w); }
    else if let Ok(x) = env::var("DISPLAY") { println!("      X11: {}", x); }

    let running = std::process::Command::new("pgrep")
        .args(["-x", "super-v-daemon"]).output()
        .map(|o| o.status.success()).unwrap_or(false);
    if running { println!("      Daemon: running"); }
    else {
        println!("      Daemon: not running, starting...");
        let _ = std::process::Command::new("super-v-daemon").spawn();
    }

    let gs = std::process::Command::new("gsettings").args([
        "get", "org.gnome.settings-daemon.plugins.media-keys", "custom-keybindings",
    ]).output();
    if let Ok(out) = gs {
        if String::from_utf8_lossy(&out.stdout).contains("super-v") {
            println!("      Shortcut Super+V: registered");
        } else {
            println!("      Shortcut Super+V: registering...");
            let script = r#"
C=$(gsettings get org.gnome.settings-daemon.plugins.media-keys custom-keybindings)
P="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/super-v/"
if [ "$C" = "@as []" ] || [ "$C" = "[]" ]; then U="['$P']"; else U=$(echo "$C" | sed "s|]|, '$P']|"); fi
gsettings set org.gnome.settings-daemon.plugins.media-keys custom-keybindings "$U"
gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$P name "Super-V Clipboard"
gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$P command "super-v-toggle"
gsettings set org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$P binding "<Super>v"
"#;
            let _ = std::process::Command::new("bash").arg("-c").arg(script).output();
            println!("      Shortcut Super+V: applied");
        }
    }
    Ok(())
}

fn main() -> Result<()> {
    // GTK4 default GL renderer costs ~140 MB extra RSS on Wayland for a
    // 420x520 popup. Cairo is a fraction of that and fast enough here,
    // so force it unless the user overrides the renderer.
    if std::env::var_os("GSK_RENDERER").is_none() {
        std::env::set_var("GSK_RENDERER", "cairo");
    }
    {
        let cfg = load_settings();
        if let Ok(mut g) = LANGS.lock() { *g = cfg.emoji_languages; }
        if let Ok(mut g) = GIF_PROVIDER.lock() { *g = cfg.gif_provider; }
    }

    init_logging();
    if let Ok(c) = CString::new(log_file().to_string_lossy().to_string()) {
        unsafe { super_v_ui_set_log_path(c.as_ptr()); }
    }

    let args: Vec<String> = env::args().collect();

    if args.iter().any(|a| a == "--diagnostics" || a == "--doctor" || a == "--check") {
        return run_diagnostics_and_autofix();
    }

    if args.iter().any(|a| a == "--test" || a == "--benchmark") {
        let mut buf = [0u8; 128];
        unsafe {
            super_v_ui_test_prewarm(buf.as_mut_ptr() as *mut c_char, buf.len() as c_int);
            let ver = CStr::from_ptr(buf.as_ptr() as *const c_char).to_string_lossy();
            println!("VERIFY: ui_runtime_ok=true, version={}", ver);
            println!("VERIFY: window_prewarmed=true, adw_widgets_built=true");
        }
        return Ok(());
    }

    // Register callbacks only. No widgets are built here, so a secondary
    // instance (the Super+V launcher) exits in milliseconds via GApplication.
    unsafe {
        super_v_ui_set_paste_callback(on_paste_selected);
        super_v_ui_set_pin_callback(on_pin_toggled);
        super_v_ui_set_delete_callback(on_delete_item);
        super_v_ui_set_glyph_provider(provide_glyph_page);
        super_v_ui_set_glyph_variants_provider(provide_glyph_variants);
        super_v_ui_set_activate_hook(on_primary_activated);
        super_v_ui_set_language_hook(on_language_changed);
        super_v_ui_set_gif_provider_hook(on_gif_provider_changed);
        super_v_ui_set_gif_reload_hook(on_gif_reload);
        super_v_ui_set_gif_search_hook(on_gif_search);
        super_v_ui_set_gif_more_hook(on_gif_more);
        super_v_ui_set_api_key_hook(on_api_key_changed);
        super_v_ui_set_provider_test_hook(on_provider_test);
        super_v_ui_set_placement_hook(on_placement_changed);
        if std::env::args().any(|a| a == "--hidden") {
            unsafe { super_v_ui_set_start_hidden(1) };
        }
        super_v_ui_set_clipboard_hook(on_clipboard_text);
        super_v_ui_set_image_hook(on_clipboard_image);
        super_v_ui_set_refresh_hook(on_refresh_history);
        super_v_ui_set_live_update_hook(on_live_update);
        warm_glyph_index();
        tools::init();
    }

    /* GTK rejects unknown options, so --hidden is consumed above. */
    let forwarded: Vec<String> = args.iter().filter(|a| a.as_str() != "--hidden").cloned().collect();
    let c_args: Vec<CString> = forwarded.iter()
        .map(|a| CString::new(a.as_str()).unwrap_or_default())
        .collect();
    let mut c_ptrs: Vec<*mut c_char> = c_args.iter().map(|a| a.as_ptr() as *mut c_char).collect();
    c_ptrs.push(std::ptr::null_mut());

    let status = unsafe { super_v_ui_run_app(c_args.len() as c_int, c_ptrs.as_mut_ptr()) };
    if status != 0 {
        eprintln!("Application exited with code {}", status);
    }
    Ok(())
}
