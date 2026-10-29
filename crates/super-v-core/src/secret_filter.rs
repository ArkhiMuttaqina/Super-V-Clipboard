//! Secret and Sensitive Data Filter for super-v-clipboard.
//! Detects API tokens, private keys, passwords, and clipboard password-manager hints.

pub struct SecretFilter;

impl SecretFilter {
    pub fn is_sensitive(mime: &str, text: Option<&str>) -> bool {
        // 1. Password manager MIME hints (KeepassXC, Bitwarden, 1Password)
        let m = mime.to_lowercase();
        if m.contains("password") || m.contains("secret") || m == "x-kde-passwordmanagerhint" {
            return true;
        }

        let Some(s) = text else {
            return false;
        };

        let trimmed = s.trim();
        if trimmed.is_empty() {
            return false;
        }

        // 2. Cryptographic Private Keys
        if trimmed.contains("-----BEGIN PRIVATE KEY-----")
            || trimmed.contains("-----BEGIN RSA PRIVATE KEY-----")
            || trimmed.contains("-----BEGIN OPENSSH PRIVATE KEY-----")
        {
            return true;
        }

        // 3. GitHub personal access tokens
        if trimmed.starts_with("ghp_") || trimmed.starts_with("gho_") || trimmed.starts_with("ghu_") {
            return true;
        }

        // 4. Slack tokens
        if trimmed.starts_with("xoxb-") || trimmed.starts_with("xoxp-") {
            return true;
        }

        // 5. Telegram Bot API token pattern: <digit{9,10}>:<alphanumeric/dash/underscore 35 chars>
        if let Some((bot_id, hash)) = trimmed.split_once(':') {
            if (bot_id.len() == 9 || bot_id.len() == 10)
                && bot_id.chars().all(|c| c.is_ascii_digit())
                && hash.len() == 35
                && hash.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
            {
                return true;
            }
        }

        // 6. Generic Bearer Token or Authorization header
        if trimmed.starts_with("Bearer ") && trimmed.len() > 30 {
            return true;
        }

        false
    }

    pub fn mask_preview(text: &str) -> String {
        if text.len() <= 6 {
            "******".to_string()
        } else {
            let prefix = &text[..3];
            let suffix = &text[text.len() - 3..];
            format!("{}••••••••{}", prefix, suffix)
        }
    }
}
