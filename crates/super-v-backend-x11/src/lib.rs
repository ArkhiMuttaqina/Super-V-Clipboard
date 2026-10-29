//! Native X11 Backend with C FFI for XTest synthetic keypress injection.

use anyhow::{anyhow, Result};

extern "C" {
    fn super_v_x11_is_available() -> libc::c_int;
    fn super_v_x11_fake_paste() -> libc::c_int;
}

pub struct X11Backend;

impl X11Backend {
    pub fn new() -> Result<Self> {
        unsafe {
            if super_v_x11_is_available() != 1 {
                return Err(anyhow!("X11 or XTest extension is not available in current display"));
            }
        }
        Ok(Self)
    }

    pub fn trigger_paste(&self) -> Result<()> {
        unsafe {
            let res = super_v_x11_fake_paste();
            if res != 0 {
                return Err(anyhow!("Failed to inject fake paste via XTest, code: {}", res));
            }
        }
        Ok(())
    }
}
