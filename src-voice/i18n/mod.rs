// SPDX-License-Identifier: GPL-3.0-or-later
// Both crates compile the same lightweight resource reader, without new dependencies.
#[path = "../../src/i18n/mod.rs"]
mod catalog;
pub use catalog::tr;
