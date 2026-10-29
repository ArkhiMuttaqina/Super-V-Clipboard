//! Native Wayland / wlroots backend with C FFI for compositor connection and data-control.

use anyhow::{anyhow, Result};

extern "C" {
    fn super_v_wayland_is_available() -> libc::c_int;
}

pub struct WlrBackend;

impl WlrBackend {
    pub fn new() -> Result<Self> {
        unsafe {
            if super_v_wayland_is_available() != 1 {
                return Err(anyhow!("Wayland display connection is not available"));
            }
        }
        Ok(Self)
    }

    pub fn is_available() -> bool {
        unsafe { super_v_wayland_is_available() == 1 }
    }
}
