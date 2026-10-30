//! Static glyph datasets with Unicode-name and localized keyword search.
//!
//! Two datasets ship in the binary: 1,914 emoji and 1,073 symbols. Search runs
//! over the English Unicode name plus a curated keyword list, and the Indonesian
//! dictionary in `data/glyphs/i18n.json` expands everyday words ("sedih") into
//! those English tokens. Adding a language is a data-only change: drop a new
//! top-level key into i18n.json and it becomes selectable in preferences.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

pub static EMOJIS_RAW: &str = include_str!("../../../data/glyphs/emojis.json");
pub static SYMBOLS_RAW: &str = include_str!("../../../data/glyphs/symbols.json");
pub static I18N_RAW: &str = include_str!("../../../data/glyphs/i18n.json");

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RawGlyph {
    #[serde(rename = "c")]
    pub char: String,
    #[serde(rename = "n")]
    pub name: String,
    #[serde(rename = "g", default)]
    pub group: usize,
    #[serde(rename = "k", default)]
    pub keywords: String,
    #[serde(rename = "t", default)]
    pub tone: usize,
}

/// Localized aliases keyed by language, then by the user-facing word.
pub type I18nMap = HashMap<String, HashMap<String, Vec<String>>>;

pub struct GlyphIndex {
    emojis: Vec<RawGlyph>,
    symbols: Vec<RawGlyph>,
    i18n: I18nMap,
}

impl GlyphIndex {
    pub fn load_embedded() -> Self {
        let emojis: Vec<RawGlyph> = serde_json::from_str(EMOJIS_RAW).unwrap_or_default();
        let symbols: Vec<RawGlyph> = serde_json::from_str(SYMBOLS_RAW).unwrap_or_default();
        let i18n: I18nMap = serde_json::from_str(I18N_RAW).unwrap_or_default();
        Self { emojis, symbols, i18n }
    }

    pub fn emoji_count(&self) -> usize {
        self.emojis.len()
    }

    pub fn symbol_count(&self) -> usize {
        self.symbols.len()
    }

    /// Languages present in the bundled dictionary, for the About/preferences UI.
    pub fn available_languages(&self) -> Vec<String> {
        let mut langs: Vec<String> = self.i18n.keys().cloned().collect();
        langs.sort();
        langs
    }

    fn source(&self, tab: &str) -> &[RawGlyph] {
        match tab {
            "emoji" => &self.emojis,
            _ => &self.symbols,
        }
    }

    fn expand_token(&self, token: &str, langs: &[String]) -> Vec<String> {
        let mut out = vec![token.to_string()];
        for lang in langs {
            if let Some(dict) = self.i18n.get(lang) {
                if let Some(aliases) = dict.get(token) {
                    out.extend(aliases.iter().cloned());
                }
            }
        }
        out
    }

    fn matches(glyph: &RawGlyph, expansions: &[String]) -> bool {
        expansions.iter().any(|term| {
            if term.is_empty() {
                return false;
            }
            glyph.name.contains(term.as_str()) || glyph.keywords.contains(term.as_str())
        })
    }

    /// Search one tab. Empty query pages the full dataset.
    ///
    /// Multi-word queries narrow (all tokens must match); a single glyph
    /// character searches for that exact glyph first.
    pub fn search(&self, tab: &str, query: &str, langs: &str, offset: usize, limit: usize) -> Vec<&RawGlyph> {
        let src = self.source(tab);
        let query = query.trim().to_lowercase();

        if query.is_empty() {
            return src.iter().skip(offset).take(limit).collect();
        }

        // A literal glyph always wins.
        if query.chars().count() <= 2 {
            let exact: Vec<&RawGlyph> = src.iter().filter(|g| g.char == query).collect();
            if !exact.is_empty() {
                return exact.into_iter().skip(offset).take(limit).collect();
            }
        }

        let active_langs: Vec<String> = langs
            .split(',')
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect();

        let tokens: Vec<&str> = query.split_whitespace().collect();
        let expansions: Vec<Vec<String>> = tokens
            .iter()
            .map(|t| self.expand_token(t, &active_langs))
            .collect();

        let mut hits: Vec<&RawGlyph> = src
            .iter()
            .filter(|g| expansions.iter().all(|e| Self::matches(g, e)))
            .collect();

        // Narrowing can overshoot on free-form phrases; fall back to any-token.
        if hits.is_empty() && expansions.len() > 1 {
            hits = src
                .iter()
                .filter(|g| expansions.iter().any(|e| Self::matches(g, e)))
                .collect();
        }

        hits.into_iter().skip(offset).take(limit).collect()
    }

    /// Total hits for a query, so the UI can stop paging.
    pub fn search_total(&self, tab: &str, query: &str, langs: &str) -> usize {
        let src = self.source(tab);
        let query = query.trim().to_lowercase();
        if query.is_empty() {
            return src.len();
        }
        let active_langs: Vec<String> = langs
            .split(',')
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect();
        let tokens: Vec<&str> = query.split_whitespace().collect();
        let expansions: Vec<Vec<String>> = tokens
            .iter()
            .map(|t| self.expand_token(t, &active_langs))
            .collect();
        src.iter()
            .filter(|g| expansions.iter().all(|e| Self::matches(g, e)))
            .count()
    }
}
