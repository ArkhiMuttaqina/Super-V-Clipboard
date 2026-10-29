//! Glyph models and static datasets for Emoji, Kaomoji, and Symbols.

use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct EmojiEntry {
    pub char: &'static str,
    pub name: &'static str,
    pub category: &'static str,
    pub keywords: &'static [&'static str],
}

#[derive(Debug, Clone, Serialize)]
pub struct KaomojiEntry {
    pub text: &'static str,
    pub category: &'static str,
    pub description: &'static str,
}

#[derive(Debug, Clone, Serialize)]
pub struct SymbolEntry {
    pub char: &'static str,
    pub name: &'static str,
    pub category: &'static str,
}

pub const SAMPLE_EMOJIS: &[EmojiEntry] = &[
    EmojiEntry { char: "😀", name: "grinning face", category: "smileys", keywords: &["happy", "smile"] },
    EmojiEntry { char: "🚀", name: "rocket", category: "travel", keywords: &["space", "launch", "fast"] },
    EmojiEntry { char: "🦀", name: "crab", category: "animals", keywords: &["rust", "ferris", "crustacean"] },
    EmojiEntry { char: "⚡", name: "high voltage", category: "objects", keywords: &["zap", "lightning", "fast"] },
];

pub const SAMPLE_KAOMOJI: &[KaomojiEntry] = &[
    KaomojiEntry { text: r#"¯\_(ツ)_/¯"#, category: "shrug", description: "shrug" },
    KaomojiEntry { text: "( ͡° ͜ʖ ͡°)", category: "lenny", description: "lenny face" },
    KaomojiEntry { text: "(╯°□°)╯︵ ┻━┻", category: "angry", description: "table flip" },
    KaomojiEntry { text: "┬─┬ノ( º _ ºノ)", category: "neutral", description: "put table back" },
];

pub const SAMPLE_SYMBOLS: &[SymbolEntry] = &[
    SymbolEntry { char: "→", name: "rightwards arrow", category: "arrows" },
    SymbolEntry { char: "✓", name: "check mark", category: "symbols" },
    SymbolEntry { char: "€", name: "euro sign", category: "currency" },
    SymbolEntry { char: "∑", name: "n-ary summation", category: "math" },
    SymbolEntry { char: "λ", name: "greek small letter lambda", category: "greek" },
];
