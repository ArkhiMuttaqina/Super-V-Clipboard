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
    fn super_v_ui_set_ai_test_hook(cb: extern "C" fn());
    fn super_v_ui_set_ai_models_hook(cb: extern "C" fn(*const c_char));
    fn super_v_ui_set_ai_models(lines: *const c_char);
    fn super_v_ui_set_ai_banner(text: *const c_char);
    fn super_v_ui_set_ai_status(text: *const c_char);
    fn super_v_ui_ai_stream_reset();
    fn super_v_ui_ai_stream_delta(text: *const c_char);
    fn super_v_ui_ai_stream_done();
    fn super_v_ui_set_current_config(text: *const c_char);
    fn super_v_ui_set_provider_items(lines: *const c_char);
    fn super_v_ui_set_ai_refresh_hook(cb: extern "C" fn());
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
    let provider = CString::new(cfg.ai_provider.clone()).unwrap_or_default();
    let model = CString::new(cfg.ai_model.clone()).unwrap_or_default();
    let key = CString::new(cfg.ai_api_key.clone()).unwrap_or_default();
    let base = CString::new(cfg.ai_base_url.clone()).unwrap_or_default();
    unsafe {
        super_v_ui_set_ai_config_values(provider.as_ptr(), model.as_ptr(), key.as_ptr(), base.as_ptr());
    }
}

extern "C" fn deliver_reset(_data: *mut c_void) {
    unsafe { super_v_ui_ai_stream_reset() };
}

extern "C" fn deliver_delta(data: *mut c_void) {
    if data.is_null() {
        return;
    }
    let text = unsafe { Box::from_raw(data as *mut String) };
    if let Ok(c) = CString::new(text.as_str()) {
        unsafe { super_v_ui_ai_stream_delta(c.as_ptr()) };
    }
}

extern "C" fn deliver_stream_done(data: *mut c_void) {
    unsafe { super_v_ui_ai_stream_done() };
    if data.is_null() {
        return;
    }
    let text = unsafe { Box::from_raw(data as *mut String) };
    if !text.is_empty() {
        if let Ok(c) = CString::new(text.as_str()) {
            unsafe { super_v_ui_set_ai_status(c.as_ptr()) };
        }
    }
}

fn post_delta(text: &str) {
    unsafe {
        super_v_ui_post_to_main(
            deliver_delta,
            Box::into_raw(Box::new(text.to_string())) as *mut c_void,
        )
    };
}

fn sse_lines(resp: &mut ureq::Body, mut on_data: impl FnMut(&str)) -> Result<(), String> {
    use std::io::BufRead;
    let reader = resp.as_reader();
    let buf = std::io::BufReader::new(reader);
    for line in buf.lines() {
        let line = line.map_err(|e| e.to_string())?;
        if let Some(rest) = line.strip_prefix("data: ") {
            if rest.trim() == "[DONE]" {
                return Ok(());
            }
            on_data(rest);
        }
    }
    Ok(())
}

fn ai_http_stream(
    provider: Provider,
    cfg: &Settings,
    model_override: &str,
    system: &str,
    prompt: &str,
    on_delta: &mut dyn FnMut(&str),
) -> Result<String, String> {
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
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(std::time::Duration::from_secs(120)))
        .build()
        .into();
    let mut full = String::new();

    if provider == Provider::Anthropic {
        let model = if !model_override.is_empty() {
            model_override
        } else if cfg.ai_model.is_empty() {
            "claude-sonnet-4-5"
        } else {
            cfg.ai_model.as_str()
        };
        let body = serde_json::json!({
            "model": model,
            "max_tokens": 1024,
            "stream": true,
            "system": system,
            "messages": [{ "role": "user", "content": prompt }]
        });
        let mut resp = agent
            .post("https://api.anthropic.com/v1/messages")
            .header("x-api-key", &key)
            .header("anthropic-version", "2023-06-01")
            .header("content-type", "application/json")
            .send(body.to_string())
            .map_err(|e| e.to_string())?;
        sse_lines(resp.body_mut(), |data| {
            if let Ok(v) = serde_json::from_str::<serde_json::Value>(data) {
                if v["type"] == "content_block_delta" {
                    if let Some(chunk) = v["delta"]["text"].as_str() {
                        full.push_str(chunk);
                        on_delta(chunk);
                    }
                }
            }
        })?;
        return Ok(full);
    }

    let base = if cfg.ai_base_url.is_empty() {
        "https://api.openai.com/v1".to_string()
    } else {
        cfg.ai_base_url.trim_end_matches('/').to_string()
    };
    let model = if !model_override.is_empty() {
        model_override
    } else if cfg.ai_model.is_empty() {
        "gpt-4o-mini"
    } else {
        cfg.ai_model.as_str()
    };
    let body = serde_json::json!({
        "model": model,
        "stream": true,
        "messages": [
            { "role": "system", "content": system },
            { "role": "user", "content": prompt }
        ]
    });
    let mut resp = agent
        .post(&format!("{base}/chat/completions"))
        .header("authorization", &format!("Bearer {key}"))
        .header("content-type", "application/json")
        .send(body.to_string())
        .map_err(|e| e.to_string())?;
    sse_lines(resp.body_mut(), |data| {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(data) {
            if let Some(chunk) = v["choices"][0]["delta"]["content"].as_str() {
                full.push_str(chunk);
                on_delta(chunk);
            }
        }
    })?;
    Ok(full)
}

fn run_provider_stream(
    cfg: &Settings,
    system: &str,
    prompt: &str,
    on_delta: &mut dyn FnMut(&str),
) -> Result<String, String> {
    let provider = resolve(cfg).ok_or_else(|| {
        "Tidak ada provider AI: isi API key di Settings atau pasang claude/codex CLI".to_string()
    })?;
    match provider {
        Provider::ClaudeCli | Provider::CodexCli | Provider::AntigravityCli => {
            let text = ai_cli(provider, system, prompt)?;
            on_delta(&text);
            Ok(text)
        }
        _ => {
            let mut chain: Vec<String> = cfg.ai_models.clone();
            if !cfg.ai_model.is_empty() && !chain.iter().any(|m| m == &cfg.ai_model) {
                chain.insert(0, cfg.ai_model.clone());
            }
            if chain.is_empty() {
                chain.push(String::new());
            }
            let mut last = String::from("no model tried");
            for model in chain {
                let started = std::cell::Cell::new(false);
                let mut emit = |chunk: &str| {
                    started.set(true);
                    on_delta(chunk);
                };
                match ai_http_stream(provider, cfg, &model, system, prompt, &mut emit) {
                    Ok(text) => return Ok(text),
                    Err(err) => {
                        if !started.get() {
                            last = format!("{model}: {err}");
                            append_log(&format!("stream model failed: {last}"));
                            continue;
                        }
                        return Err(err);
                    }
                }
            }
            Err(last)
        }
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
        unsafe { super_v_ui_post_to_main(deliver_reset, std::ptr::null_mut()) };
        let outcome = run_provider_stream(&cfg, &system, &prompt, &mut |chunk| post_delta(chunk));
        let status = match outcome {
            Ok(_) => String::new(),
            Err(err) => format!("Gagal: {err}"),
        };
        unsafe {
            super_v_ui_post_to_main(
                deliver_stream_done,
                Box::into_raw(Box::new(status)) as *mut c_void,
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

fn cli_version(bin: &str) -> Option<String> {
    let out = std::process::Command::new(bin).arg("--version").output().ok()?;
    let text = String::from_utf8_lossy(&out.stdout);
    let line = text.lines().next().unwrap_or("").trim();
    if line.is_empty() {
        None
    } else {
        Some(line.chars().take(48).collect())
    }
}

fn push_provider_items() {
    let mut lines = String::new();
    lines.push_str("auto	Auto detect");
    lines.push('\n');
    lines.push_str("anthropic	Anthropic API");
    lines.push('\n');
    lines.push_str("openai	OpenAI compatible API");
    for (value, bin, label) in [
        ("claude-cli", "claude", "claude CLI"),
        ("codex-cli", "codex", "codex CLI"),
        ("antigravity", "antigravity", "antigravity CLI"),
    ] {
        if let Some(version) = cli_version(bin) {
            lines.push('\n');
            lines.push_str(value);
            lines.push('\t');
            lines.push_str(&format!("{label} · {version}"));
        }
    }
    if let Ok(c) = CString::new(lines) {
        unsafe { super_v_ui_set_provider_items(c.as_ptr()) };
    }
}

fn current_config_text(cfg: &Settings) -> String {
    let provider = match resolve(cfg) {
        Some(Provider::Anthropic) => "Anthropic API",
        Some(Provider::OpenAi) => "OpenAI compatible API",
        Some(Provider::ClaudeCli) => "claude CLI",
        Some(Provider::CodexCli) => "codex CLI",
        Some(Provider::AntigravityCli) => "antigravity CLI",
        None => "nothing configured",
    };
    let source = if cfg.ai_provider != "auto" {
        "selected manually"
    } else {
        "auto detected"
    };
    let mut details = Vec::new();
    let model = if !cfg.ai_models.is_empty() {
        cfg.ai_models.join(" → ")
    } else if !cfg.ai_model.is_empty() {
        cfg.ai_model.clone()
    } else {
        "provider default".to_string()
    };
    details.push(format!("Model: {model}"));
    if let Some(base) = cfg.ai_base_url.strip_suffix('/').or(Some(cfg.ai_base_url.as_str())) {
        if !base.is_empty() {
            details.push(format!("Base URL: {base}"));
        }
    }
    let key = if !cfg.ai_api_key.is_empty() {
        "key set"
    } else if std::env::var("ANTHROPIC_API_KEY").is_ok() || std::env::var("OPENAI_API_KEY").is_ok() {
        "key from environment"
    } else {
        "no key"
    };
    details.push(key.to_string());
    let mut missing = Vec::new();
    for (bin, label) in [("claude", "claude"), ("codex", "codex"), ("antigravity", "antigravity")] {
        if !which(bin) {
            missing.push(label);
        }
    }
    if !missing.is_empty() {
        details.push(format!("not installed: {}", missing.join(", ")));
    }
    format!("{provider} ({source})
{}", details.join(" · "))
}

extern "C" fn on_ai_refresh() {
    let cfg = load_settings();
    push_provider_items();
    push_current_config(&cfg);
    post_banner(detected_banner(&cfg));
}

fn push_current_config(cfg: &Settings) {
    if let Ok(c) = CString::new(current_config_text(cfg)) {
        unsafe { super_v_ui_set_current_config(c.as_ptr()) };
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
    post_banner(detected_banner(&cfg));
}

fn http_get(url: &str, headers: &[(&str, &str)]) -> Result<String, String> {
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(std::time::Duration::from_secs(30)))
        .build()
        .into();
    let mut req = agent.get(url);
    for (k, v) in headers {
        req = req.header(*k, *v);
    }
    match req.call() {
        Ok(mut resp) => resp.body_mut().read_to_string().map_err(|e| e.to_string()),
        Err(ureq::Error::StatusCode(code)) => Err(format!("HTTP {code}")),
        Err(e) => Err(e.to_string()),
    }
}

fn provider_key(cfg: &Settings, provider: Provider) -> String {
    if !cfg.ai_api_key.is_empty() {
        return cfg.ai_api_key.clone();
    }
    let var = if provider == Provider::Anthropic {
        "ANTHROPIC_API_KEY"
    } else {
        "OPENAI_API_KEY"
    };
    std::env::var(var).unwrap_or_default()
}

fn list_models(cfg: &Settings) -> Result<Vec<String>, String> {
    let provider = resolve(cfg).ok_or_else(|| "No provider configured".to_string())?;
    let key = provider_key(cfg, provider);
    let raw = match provider {
        Provider::Anthropic => {
            if key.is_empty() {
                return Err("Anthropic needs an API key".into());
            }
            http_get(
                "https://api.anthropic.com/v1/models?limit=100",
                &[("x-api-key", key.as_str()), ("anthropic-version", "2023-06-01")],
            )?
        }
        Provider::OpenAi => {
            if key.is_empty() {
                return Err("OpenAI-compatible endpoint needs an API key".into());
            }
            let base = if cfg.ai_base_url.is_empty() {
                "https://api.openai.com/v1".to_string()
            } else {
                cfg.ai_base_url.trim_end_matches('/').to_string()
            };
            let auth = format!("Bearer {key}");
            http_get(&format!("{base}/models"), &[("authorization", auth.as_str())])?
        }
        other => {
            let _ = other;
            return Err("CLI providers do not expose a model list".into());
        }
    };
    let parsed: serde_json::Value = serde_json::from_str(&raw).map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    if let Some(items) = parsed["data"].as_array() {
        for item in items {
            if let Some(id) = item["id"].as_str() {
                out.push(id.to_string());
            }
        }
    }
    out.sort();
    out.dedup();
    if out.is_empty() {
        return Err(raw.chars().take(160).collect());
    }
    Ok(out)
}

fn detected_banner(cfg: &Settings) -> String {
    let mut found = Vec::new();
    for bin in ["claude", "codex", "antigravity"] {
        if which(bin) {
            found.push(bin.to_string());
        }
    }
    let chosen = match resolve(cfg) {
        Some(Provider::Anthropic) => "Anthropic API".to_string(),
        Some(Provider::OpenAi) => "OpenAI-compatible API".to_string(),
        Some(Provider::ClaudeCli) => "claude CLI".to_string(),
        Some(Provider::CodexCli) => "codex CLI".to_string(),
        Some(Provider::AntigravityCli) => "antigravity CLI".to_string(),
        None => "nothing yet".to_string(),
    };
    let key_hint = if !cfg.ai_api_key.is_empty() {
        "API key from settings"
    } else if std::env::var("ANTHROPIC_API_KEY").is_ok() || std::env::var("OPENAI_API_KEY").is_ok() {
        "API key from environment"
    } else {
        "no API key"
    };
    format!(
        "Detected CLI: {}. {} | Will use: {}",
        if found.is_empty() { "none".to_string() } else { found.join(", ") },
        key_hint,
        chosen
    )
}

extern "C" fn deliver_models(data: *mut c_void) {
    if data.is_null() {
        return;
    }
    let text = unsafe { Box::from_raw(data as *mut String) };
    if let Ok(text) = CString::new(text.as_str()) {
        unsafe { super_v_ui_set_ai_models(text.as_ptr()) };
    }
}

extern "C" fn deliver_banner(data: *mut c_void) {
    if data.is_null() {
        return;
    }
    let text = unsafe { Box::from_raw(data as *mut String) };
    if let Ok(text) = CString::new(text.as_str()) {
        unsafe { super_v_ui_set_ai_banner(text.as_ptr()) };
    }
}

fn post_banner(text: String) {
    append_log(&format!("banner: {text}"));
    unsafe {
        super_v_ui_post_to_main(deliver_banner, Box::into_raw(Box::new(text)) as *mut c_void);
    }
}

extern "C" fn on_ai_test() {
    std::thread::spawn(|| {
        let cfg = load_settings();
        match list_models(&cfg) {
            Ok(models) => {
                let mut lines = String::new();
                for model in &models {
                    let selected = cfg.ai_models.iter().any(|m| m == model);
                    lines.push_str(model);
                    lines.push('\t');
                    lines.push(if selected { '1' } else { '0' });
                    lines.push('\n');
                }
                unsafe {
                    super_v_ui_post_to_main(deliver_models, Box::into_raw(Box::new(lines)) as *mut c_void);
                }
                post_banner(format!("{} models available. Tick the ones to try in order.", models.len()));
            }
            Err(err) => post_banner(format!("Smoke test failed: {err}")),
        }
    });
}

extern "C" fn on_ai_models_changed(csv: *const c_char) {
    if csv.is_null() {
        return;
    }
    let csv = unsafe { CStr::from_ptr(csv) }.to_string_lossy().to_string();
    let mut cfg = load_settings();
    cfg.ai_models = csv
        .split(',')
        .map(|m| m.trim().to_string())
        .filter(|m| !m.is_empty())
        .collect();
    crate::save_settings(&cfg);
    append_log(&format!("ai models: {}", cfg.ai_models.len()));
}

pub fn init() {
    unsafe {
        super_v_ui_set_snippet_hooks(on_snippet_add, on_snippet_delete);
        super_v_ui_set_ai_generate_hook(on_ai_generate);
        super_v_ui_set_ai_config_hook(on_ai_config);
        super_v_ui_set_ai_test_hook(on_ai_test);
        super_v_ui_set_ai_models_hook(on_ai_models_changed);
        super_v_ui_set_ai_refresh_hook(on_ai_refresh);
    }
    let cfg = load_settings();
    post_banner(detected_banner(&cfg));
    push_provider_items();
    push_current_config(&cfg);
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
        "indonesian" | "indonesia" => "Tulis dalam Bahasa Indonesia.",
        "english" => "Write in English.",
        "malay" => "Tulis dalam Bahasa Melayu.",
        "arabic" => "Write in Arabic.",
        "mandarin" => "Write in Simplified Chinese.",
        "japanese" => "Write in Japanese.",
        "korean" => "Write in Korean.",
        "spanish" => "Write in Spanish.",
        "french" => "Write in French.",
        "german" => "Write in German.",
        "hindi" => "Write in Hindi.",
        "javanese" => "Tulis dalam Bahasa Jawa.",
        "sundanese" => "Tulis dalam Bahasa Sunda.",
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
        other => ai_http_fallback(other, cfg, system, prompt),
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

fn ai_http_fallback(provider: Provider, cfg: &Settings, system: &str, prompt: &str) -> Result<String, String> {
    let mut chain: Vec<String> = cfg.ai_models.clone();
    if !cfg.ai_model.is_empty() && !chain.iter().any(|m| m == &cfg.ai_model) {
        chain.insert(0, cfg.ai_model.clone());
    }
    if chain.is_empty() {
        chain.push(String::new());
    }
    let mut last = String::from("no model tried");
    for model in chain {
        match ai_http(provider, cfg, &model, system, prompt) {
            Ok(text) => {
                if !model.is_empty() {
                    append_log(&format!("ai model used: {model}"));
                }
                return Ok(text);
            }
            Err(err) => {
                last = if model.is_empty() { err.clone() } else { format!("{model}: {err}") };
                append_log(&format!("ai model failed, trying next: {last}"));
            }
        }
    }
    Err(last)
}

fn ai_http(provider: Provider, cfg: &Settings, model_override: &str, system: &str, prompt: &str) -> Result<String, String> {
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
        let model = if !model_override.is_empty() {
            model_override
        } else if cfg.ai_model.is_empty() {
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
        let model = if !model_override.is_empty() {
            model_override
        } else if cfg.ai_model.is_empty() {
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
