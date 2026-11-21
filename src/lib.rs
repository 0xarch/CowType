// SPDX-License-Identifier: GPL-3.0-or-later
//! Offline text conversion. No model, network, device or user-file access.
#![deny(unsafe_code)]

#[cfg(not(target_os = "linux"))]
compile_error!("CowType currently supports Linux only");

pub mod config;
pub mod dictionary;
pub mod engine;
#[cfg(feature = "native-ffi")]
#[allow(unsafe_code)]
pub mod ffi;
pub mod i18n;
pub mod learning;
pub mod session;
