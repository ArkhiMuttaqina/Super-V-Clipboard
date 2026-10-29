//! Smart clipboard content classifier and category tagger.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ContentCategory {
    PlainText,
    Url,
    ColorHex,
    CodeSnippet,
    Email,
    Image,
}

impl ContentCategory {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::PlainText => "text",
            Self::Url => "url",
            Self::ColorHex => "color",
            Self::CodeSnippet => "code",
            Self::Email => "email",
            Self::Image => "image",
        }
    }

    pub fn classify(mime: &str, text: Option<&str>) -> Self {
        if mime.starts_with("image/") {
            return Self::Image;
        }

        let Some(s) = text else {
            return Self::PlainText;
        };

        let trimmed = s.trim();
        if trimmed.is_empty() {
            return Self::PlainText;
        }

        // 1. Color Hex Check (#308b53, #fff, #ffffff, #ffffffff)
        if trimmed.starts_with('#') && (trimmed.len() == 4 || trimmed.len() == 7 || trimmed.len() == 9) {
            if trimmed[1..].chars().all(|c| c.is_ascii_hexdigit()) {
                return Self::ColorHex;
            }
        }
        if trimmed.starts_with("rgb(") || trimmed.starts_with("rgba(") {
            return Self::ColorHex;
        }

        // 2. URL Check
        if trimmed.starts_with("http://") || trimmed.starts_with("https://") || trimmed.starts_with("ftp://") {
            return Self::Url;
        }

        // 3. Email Check
        if !trimmed.contains('\n') && trimmed.contains('@') && trimmed.contains('.') && !trimmed.contains(' ') {
            let parts: Vec<&str> = trimmed.split('@').collect();
            if parts.len() == 2 && !parts[0].is_empty() && parts[1].contains('.') {
                return Self::Email;
            }
        }

        // 4. Code Snippet Check
        let code_keywords = [
            "fn ", "def ", "function ", "class ", "import ", "const ", "let ", "var ",
            "pub struct ", "public class ", "return ", "if (", "while (", "for (", "=>", "::",
            "SELECT ", "INSERT INTO ", "CREATE TABLE ", "<html>", "</div>", "<?php",
        ];
        let has_code_keyword = code_keywords.iter().any(|k| trimmed.contains(k));
        let has_brackets = (trimmed.contains('{') && trimmed.contains('}'))
            || (trimmed.contains('(') && trimmed.contains(')') && trimmed.contains(';'));

        if has_code_keyword || (trimmed.contains('\n') && has_brackets) {
            return Self::CodeSnippet;
        }

        Self::PlainText
    }
}
