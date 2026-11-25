// SPDX-License-Identifier: GPL-3.0-or-later
//! Probe the optional native backend without introducing a required dependency.
use std::ffi::{c_char, c_int, c_void};
use std::sync::OnceLock;

#[link(name = "dl")]
extern "C" {
    fn dlopen(name: *const c_char, flags: c_int) -> *mut c_void;
    fn dlsym(library: *mut c_void, name: *const c_char) -> *mut c_void;
    fn dlclose(library: *mut c_void) -> c_int;
}

pub(super) fn available() -> bool {
    static AVAILABLE: OnceLock<bool> = OnceLock::new();
    *AVAILABLE.get_or_init(|| unsafe { probe() })
}

unsafe fn probe() -> bool {
    let mut library = std::ptr::null_mut();
    // Match TraditionalConverter's SONAME and profile fallback order.
    for name in [
        c"libopencc.so.1.4",
        c"libopencc.so.1",
        c"libopencc.so.2",
        c"libopencc.so",
    ] {
        library = dlopen(name.as_ptr(), 2); // RTLD_NOW | RTLD_LOCAL on Linux.
        if !library.is_null() {
            break;
        }
    }
    if library.is_null() {
        return false;
    }
    let result = (|| {
        let open = dlsym(library, c"opencc_open".as_ptr());
        let close = dlsym(library, c"opencc_close".as_ptr());
        if open.is_null()
            || close.is_null()
            || dlsym(library, c"opencc_convert_utf8".as_ptr()).is_null()
            || dlsym(library, c"opencc_convert_utf8_free".as_ptr()).is_null()
        {
            return false;
        }
        let open: unsafe extern "C" fn(*const c_char) -> *mut c_void = std::mem::transmute(open);
        let close: unsafe extern "C" fn(*mut c_void) -> c_int = std::mem::transmute(close);
        let mut available = true;
        for profiles in [[c"s2tw.json", c"s2t.json"], [c"tw2s.json", c"t2s.json"]] {
            let mut found = false;
            for profile in profiles {
                let handle = open(profile.as_ptr());
                if !handle.is_null() && handle as isize != -1 {
                    close(handle);
                    found = true;
                    break;
                }
            }
            available &= found;
        }
        available
    })();
    dlclose(library);
    result
}
