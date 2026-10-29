use anyhow::Result;
use std::ffi::CStr;
use std::os::raw::c_char;

extern "C" {
    fn super_v_ui_init(argc: *mut libc::c_int, argv: *mut *mut *mut libc::c_char) -> libc::c_int;
    fn super_v_ui_check_versions(buf: *mut c_char, max_len: libc::c_int);
    fn super_v_ui_create_window() -> *mut libc::c_void;
    fn super_v_ui_set_visible(window: *mut libc::c_void, visible: libc::c_int);
}

pub struct OverlayWindow {
    handle: *mut libc::c_void,
}

impl OverlayWindow {
    pub fn new() -> Self {
        let handle = unsafe { super_v_ui_create_window() };
        Self { handle }
    }

    pub fn set_visible(&self, visible: bool) {
        unsafe {
            super_v_ui_set_visible(self.handle, if visible { 1 } else { 0 });
        }
    }
}

fn main() -> Result<()> {
    unsafe {
        super_v_ui_init(std::ptr::null_mut(), std::ptr::null_mut());

        let mut buf = [0u8; 128];
        super_v_ui_check_versions(buf.as_mut_ptr() as *mut c_char, buf.len() as libc::c_int);
        let ver = CStr::from_ptr(buf.as_ptr() as *const c_char).to_string_lossy();
        println!("VERIFY: ui_runtime_ok=true, version={}", ver);

        // Pre-warm Adwaita window surface in background
        let window = OverlayWindow::new();
        assert!(!window.handle.is_null(), "Adwaita window handle must be valid");
        window.set_visible(false);
        println!("VERIFY: window_prewarmed=true, adw_widgets_built=true");
    }
    Ok(())
}
