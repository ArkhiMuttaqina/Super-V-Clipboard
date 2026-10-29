//! Full static glyph datasets (1,914 Emojis, 83 Kaomojis, 1,073 Symbols) with search indexing.

use serde::{Deserialize, Serialize};

pub static EMOJIS_RAW: &str = include_str!("../../../data/glyphs/emojis.json");
pub static KAOMOJI_RAW: &str = include_str!("../../../data/glyphs/kaomojis.json");
pub static SYMBOLS_RAW: &str = include_str!("../../../data/glyphs/symbols.json");

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
}

pub struct GlyphIndex {
    emojis: Vec<RawGlyph>,
    kaomojis: Vec<RawGlyph>,
    symbols: Vec<RawGlyph>,
}

impl GlyphIndex {
    pub fn load_embedded() -> Self {
        let emojis: Vec<RawGlyph> = serde_json::from_str(EMOJIS_RAW).unwrap_or_default();
        let kaomojis: Vec<RawGlyph> = serde_json::from_str(KAOMOJI_RAW).unwrap_or_default();
        let symbols: Vec<RawGlyph> = serde_json::from_str(SYMBOLS_RAW).unwrap_or_default();

        Self {
            emojis,
            kaomojis,
            symbols,
        }
    }

    pub fn emoji_count(&self) -> usize {
        self.emojis.len()
    }

    pub fn kaomoji_count(&self) -> usize {
        self.kaomojis.len()
    }

    pub fn symbol_count(&self) -> usize {
        self.symbols.len()
    }

    pub fn search(&self, query: &str, limit: usize) -> Vec<RawGlyph> {
        let q = query.to_lowercase();
        let mut results = Vec::new();

        // Search emojis
        for e in &self.emojis {
            if e.name.contains(&q) || e.keywords.contains(&q) {
                results.push(e.clone());
                if results.len() >= limit {
                    return results;
                }
            }
        }

        // Search kaomojis
        for k in &self.kaomojis {
            if k.name.contains(&q) || k.keywords.contains(&q) || k.char.contains(&q) {
                results.push(k.clone());
                if results.len() >= limit {
                    return results;
                }
            }
        }

        // Search symbols
        for s in &self.symbols {
            if s.name.contains(&q) || s.keywords.contains(&q) {
                results.push(s.clone());
                if results.len() >= limit {
                    return results;
                }
            }
        }

        results
    }
}
