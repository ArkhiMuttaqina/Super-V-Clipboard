//! Snippets and the writing assistant.
//!
//! Snippets are plain JSON on disk and paste like a history row. The assistant
//! adapts to whatever provider the machine already has: an API key in settings or
//! the environment, or a local CLI (claude, codex, antigravity).

use std::ffi::{c_char, c_void, CStr, CString};

use serde::{Deserialize, Serialize};

use crate::{append_log, config_dir, load_settings, super_v_ui_post_to_main, Settings};

extern "C" {
    fn super_v_ui_add_snippet_row(name: *const c_char, body: *const c_char, id: i64);
    fn super_v_ui_clear_snippet_rows();
    fn super_v_ui_set_ai_result(text: *const c_char);
    fn super_v_ui_set_snippet_hooks(
        add: extern "C" fn(*const c_char, *const c_char),
        del: extern "C" fn(i64),
    );
    fn super_v_ui_set_ai_config_hook(cb: extern "C" fn(*const c_char, *const c_char, *const c_char, *const c_char));
    fn super_v_ui_set_ai_config_values(provider: *const c_char, model: *const c_char, key: *const c_char, base: *const c_char);
    fn super_v_ui_set_ai_generate_hook(
        cb: extern "C" fn(*const c_char, *const c_char, *const c_char, *const c_char, *const c_char),
    );
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Snippet {
    pub id: i64,
    pub name: String,
    pub body: String,
}

fn snippets_path() -> std::path::PathBuf {
    config_dir().join("snippets.json")
}

pub fn load_snippets() -> Vec<Snippet> {
    std::fs::read_to_string(snippets_path())
        .ok()
        .and_then(|raw| serde_json::from_str(&raw).ok())
        .unwrap_or_default()
}

fn store_snippets(items: &[Snippet]) {
    if let Ok(raw) = serde_json::to_string_pretty(items) {
        let _ = std::fs::write(snippets_path(), raw);
    }
}

pub fn push_snippet_rows() {
    unsafe { super_v_ui_clear_snippet_rows() };
    for item in load_snippets() {
        let name = CString::new(item.name).unwrap_or_default();
        let body = CString::new(item.body).unwrap_or_default();
        unsafe { super_v_ui_add_snippet_row(name.as_ptr(), body.as_ptr(), item.id) };
    }
}

extern "C" fn on_snippet_add(name: *const c_char, body: *const c_char) {
    if name.is_null() || body.is_null() {
        return;
    }
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().to_string();
    let body = unsafe { CStr::from_ptr(body) }.to_string_lossy().to_string();
    let mut items = load_snippets();
    let id = items.iter().map(|s| s.id).max().unwrap_or(0) + 1;
    items.push(Snippet { id, name, body });
    store_snippets(&items);
    push_snippet_rows();
    append_log("snippet added");
}

extern "C" fn on_snippet_delete(id: i64) {
    let mut items = load_snippets();
    items.retain(|s| s.id != id);
    store_snippets(&items);
    push_snippet_rows();
    let cfg = load_settings();
    let provider = CString::new(cfg.ai_provider).unwrap_or_default();
    let model = CString::new(cfg.ai_model).unwrap_or_default();
    let key = CString::new(cfg.ai_api_key).unwrap_or_default();
    let base = CString::new(cfg.ai_base_url).unwrap_or_default();
    unsafe {
        super_v_ui_set_ai_config_values(provider.as_ptr(), model.as_ptr(), key.as_ptr(), base.as_ptr());
    }
}

extern "C" fn on_ai_generate(
    prompt: *const c_char,
    lang: *const c_char,
    tone: *const c_char,
    format: *const c_char,
    target: *const c_char,
) {
    let read = |p: *const c_char| -> String {
        if p.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(p) }.to_string_lossy().to_string()
        }
    };
    let prompt = read(prompt);
    let lang = read(lang);
    let tone = read(tone);
    let format = read(format);
    let chosen = read(target);

    std::thread::spawn(move || {
        let cfg = load_settings();
        let target = detect_target(&prompt, &chosen);
        let system = build_system_prompt(&lang, &tone, &format, &target);
        let text = match run_provider(&cfg, &system, &prompt) {
            Ok(text) => text,
            Err(err) => format!("Gagal: {err}"),
        };
        unsafe {
            super_v_ui_post_to_main(
                deliver_ai_result,
                Box::into_raw(Box::new(text)) as *mut c_void,
            )
        };
    });
}

extern "C" fn deliver_ai_result(data: *mut c_void) {
    if data.is_null() {
        return;
    }
    let text = unsafe { Box::from_raw(data as *mut String) };
    if let Ok(text) = CString::new(text.as_str()) {
        unsafe { super_v_ui_set_ai_result(text.as_ptr()) };
    }
}

extern "C" fn on_ai_config(provider: *const c_char, model: *const c_char,
                          key: *const c_char, base: *const c_char) {
    let read = |p: *const c_char| -> String {
        if p.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(p) }.to_string_lossy().to_string()
        }
    };
    let mut cfg = load_settings();
    cfg.ai_provider = read(provider);
    cfg.ai_model = read(model);
    cfg.ai_api_key = read(key);
    cfg.ai_base_url = read(base);
    crate::save_settings(&cfg);
    append_log("ai config saved");
}

pub fn init() {
    unsafe {
        super_v_ui_set_snippet_hooks(on_snippet_add, on_snippet_delete);
        super_v_ui_set_ai_generate_hook(on_ai_generate);
        super_v_ui_set_ai_config_hook(on_ai_config);
    }
    push_snippet_rows();
}

// ------------------------------------------------------------------ assistant

#[derive(Clone, Copy, PartialEq)]
enum Provider {
    Anthropic,
    OpenAi,
    ClaudeCli,
    CodexCli,
    AntigravityCli,
}

fn which(bin: &str) -> bool {
    std::env::var_os("PATH")
        .map(|path| std::env::split_paths(&path).any(|dir| dir.join(bin).is_file()))
        .unwrap_or(false)
}

fn resolve(cfg: &Settings) -> Option<Provider> {
    match cfg.ai_provider.as_str() {
        "anthropic" => return Some(Provider::Anthropic),
        "openai" => return Some(Provider::OpenAi),
        "claude-cli" => return Some(Provider::ClaudeCli),
        "codex-cli" => return Some(Provider::CodexCli),
        "antigravity" => return Some(Provider::AntigravityCli),
        _ => {}
    }
    if !cfg.ai_api_key.is_empty() {
        return Some(if cfg.ai_api_key.starts_with("sk-ant") {
            Provider::Anthropic
        } else {
            Provider::OpenAi
        });
    }
    if std::env::var("ANTHROPIC_API_KEY").is_ok() {
        return Some(Provider::Anthropic);
    }
    if std::env::var("OPENAI_API_KEY").is_ok() {
        return Some(Provider::OpenAi);
    }
    for (bin, provider) in [
        ("claude", Provider::ClaudeCli),
        ("codex", Provider::CodexCli),
        ("antigravity", Provider::AntigravityCli),
    ] {
        if which(bin) {
            return Some(provider);
        }
    }
    None
}

fn detect_target(prompt: &str, chosen: &str) -> String {
    let chosen = chosen.to_lowercase();
    if chosen.contains("markdown") {
        return "markdown".into();
    }
    if chosen.contains("email") {
        return "email".into();
    }
    if chosen.contains("chat") {
        return "chat".into();
    }
    if chosen.contains("document") {
        return "document".into();
    }
    let p = prompt.to_lowercase();
    for (needle, target) in [
        ("markdown", "markdown"),
        ("readme", "markdown"),
        (".md", "markdown"),
        ("whatsapp", "chat"),
        ("telegram", "chat"),
        ("pesan", "chat"),
        ("email", "email"),
        ("surat", "email"),
    ] {
        if p.contains(needle) {
            return target.into();
        }
    }
    "chat".into()
}

fn build_system_prompt(lang: &str, tone: &str, format: &str, target: &str) -> String {
    let lang_rule = match lang.to_lowercase().as_str() {
        "indonesia" => "Tulis dalam Bahasa Indonesia.",
        "english" => "Write in English.",
        _ => "Pakai bahasa yang sama dengan permintaan pengguna.",
    };
    let tone_rule = match tone.to_lowercase().as_str() {
        "polite" => "Nada sopan, hangat, tanpa basa-basi berlebih.",
        "casual" => "Nada santai seperti mengobrol, kalimat pendek.",
        "formal" => "Nada formal dan rapi, tanpa singkatan.",
        _ => "Nada netral dan jelas.",
    };
    let format_rule = match target {
        "markdown" => "Keluarkan Markdown siap tempel: heading, daftar, blok kode bila perlu.",
        "email" => "Keluarkan email siap kirim: subjek, salam, isi, penutup.",
        "document" => "Keluarkan dokumen terstruktur dengan paragraf rapi.",
        _ => "Keluarkan pesan chat siap kirim: tanpa Markdown, tanpa judul, ringkas.",
    };
    let format_override = match format.to_lowercase().as_str() {
        "markdown" => " Gunakan Markdown.",
        "plain text" => " Gunakan teks biasa tanpa Markdown.",
        _ => "",
    };
    format!(
        "Kau asisten penulis. {lang_rule} {tone_rule} {format_rule}{format_override} Jangan menulis penjelasan meta di luar isi yang diminta."
    )
}

fn run_provider(cfg: &Settings, system: &str, prompt: &str) -> Result<String, String> {
    let provider = resolve(cfg).ok_or_else(|| {
        "Tidak ada provider AI: isi API key di Settings atau pasang claude/codex CLI".to_string()
    })?;
    match provider {
        Provider::ClaudeCli | Provider::CodexCli | Provider::AntigravityCli => {
            ai_cli(provider, system, prompt)
        }
        other => ai_http(other, cfg, system, prompt),
    }
}

fn ai_cli(provider: Provider, system: &str, prompt: &str) -> Result<String, String> {
    let (bin, args): (&str, Vec<&str>) = match provider {
        Provider::ClaudeCli => ("claude", vec!["-p"]),
        Provider::CodexCli => ("codex", vec!["exec"]),
        _ => ("antigravity", vec!["-p"]),
    };
    let full = format!("{system}\n\n{prompt}");
    let output = std::process::Command::new(bin)
        .args(&args)
        .arg(full)
        .output()
        .map_err(|e| format!("{bin} tidak bisa dijalankan: {e}"))?;
    if !output.status.success() {
        let err = String::from_utf8_lossy(&output.stderr);
        return Err(format!("{bin} gagal: {}", err.chars().take(200).collect::<String>()));
    }
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

fn http_post(url: &str, headers: &[(&str, &str)], body: &str) -> Result<String, String> {
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(std::time::Duration::from_secs(90)))
        .build()
        .into();
    let mut req = agent.post(url);
    for (k, v) in headers {
        req = req.header(*k, *v);
    }
    match req.send(body) {
        Ok(mut resp) => resp
            .body_mut()
            .read_to_string()
            .map_err(|e| e.to_string()),
        Err(ureq::Error::StatusCode(code)) => Err(format!("HTTP {code}")),
        Err(e) => Err(e.to_string()),
    }
}

fn ai_http(provider: Provider, cfg: &Settings, system: &str, prompt: &str) -> Result<String, String> {
    let key = if !cfg.ai_api_key.is_empty() {
        cfg.ai_api_key.clone()
    } else if provider == Provider::Anthropic {
        std::env::var("ANTHROPIC_API_KEY").unwrap_or_default()
    } else {
        std::env::var("OPENAI_API_KEY").unwrap_or_default()
    };
    if key.is_empty() {
        return Err("API key AI belum diisi".into());
    }
    if provider == Provider::Anthropic {
        let model = if cfg.ai_model.is_empty() {
            "claude-sonnet-4-5"
        } else {
            cfg.ai_model.as_str()
        };
        let body = serde_json::json!({
            "model": model,
            "max_tokens": 1024,
            "system": system,
            "messages": [{ "role": "user", "content": prompt }]
        });
        let raw = http_post(
            "https://api.anthropic.com/v1/messages",
            &[
                ("x-api-key", key.as_str()),
                ("anthropic-version", "2023-06-01"),
                ("content-type", "application/json"),
            ],
            &body.to_string(),
        )?;
        let parsed: serde_json::Value = serde_json::from_str(&raw).map_err(|e| e.to_string())?;
        parsed["content"][0]["text"]
            .as_str()
            .map(|s| s.to_string())
            .ok_or_else(|| raw.chars().take(200).collect())
    } else {
        let base = if cfg.ai_base_url.is_empty() {
            "https://api.openai.com/v1".to_string()
        } else {
            cfg.ai_base_url.trim_end_matches('/').to_string()
        };
        let model = if cfg.ai_model.is_empty() {
            "gpt-4o-mini"
        } else {
            cfg.ai_model.as_str()
        };
        let body = serde_json::json!({
            "model": model,
            "messages": [
                { "role": "system", "content": system },
                { "role": "user", "content": prompt }
            ]
        });
        let auth = format!("Bearer {key}");
        let raw = http_post(
            &format!("{base}/chat/completions"),
            &[("authorization", auth.as_str()), ("content-type", "application/json")],
            &body.to_string(),
        )?;
        let parsed: serde_json::Value = serde_json::from_str(&raw).map_err(|e| e.to_string())?;
        parsed["choices"][0]["message"]["content"]
            .as_str()
            .map(|s| s.to_string())
            .ok_or_else(|| raw.chars().take(200).collect())
    }
}
