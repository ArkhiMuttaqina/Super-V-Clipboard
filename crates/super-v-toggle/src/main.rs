//! Micro toggle binary for Super+V.
//!
//! Deliberately links nothing but libc: it must never become a Wayland
//! client, otherwise the desktop dock flashes a taskbar entry on every key
//! press. If the resident overlay is already running we poke it with
//! SIGUSR1; otherwise we spawn it once and the overlay stays resident.

use std::os::unix::process::CommandExt;
use std::path::PathBuf;
use std::process::{Command, Stdio};

const UI_BIN: &str = "super-v-ui";

fn find_overlay_pid() -> Option<i32> {
    let self_pid = std::process::id() as i32;
    let entries = std::fs::read_dir("/proc").ok()?;

    for entry in entries.flatten() {
        let name = entry.file_name();
        let pid: i32 = match name.to_string_lossy().parse() {
            Ok(p) => p,
            Err(_) => continue,
        };
        if pid == self_pid {
            continue;
        }
        let cmdline = match std::fs::read(format!("/proc/{}/cmdline", pid)) {
            Ok(v) => v,
            Err(_) => continue,
        };
        let cmd = String::from_utf8_lossy(&cmdline).replace('\0', " ");
        if cmd.contains(UI_BIN) && !cmd.contains("super-v-toggle") {
            return Some(pid);
        }
    }
    None
}

/// Resolve the overlay binary even when PATH is minimal (systemd/dock env).
fn resolve_ui_binary() -> PathBuf {
    if let Ok(home) = std::env::var("HOME") {
        let candidate = PathBuf::from(&home).join(".local/bin").join(UI_BIN);
        if candidate.exists() {
            return candidate;
        }
    }
    for prefix in ["/usr/local/bin", "/usr/bin"] {
        let candidate = PathBuf::from(prefix).join(UI_BIN);
        if candidate.exists() {
            return candidate;
        }
    }
    PathBuf::from(UI_BIN)
}

fn main() {
    if let Some(pid) = find_overlay_pid() {
        unsafe {
            libc::kill(pid, libc::SIGUSR1);
        }
        return;
    }

    let _ = Command::new(resolve_ui_binary())
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .process_group(0)
        .spawn();
}
