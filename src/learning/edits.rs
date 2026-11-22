// SPDX-License-Identifier: GPL-3.0-or-later
//! Private, optimistic edit requests. The learning writer remains the only owner.
use super::{now, save_atomic, valid, Learning, Record};
use std::{
    fs, io,
    os::fd::AsRawFd,
    os::unix::fs::OpenOptionsExt,
    path::{Path, PathBuf},
};

pub struct WriterLock {
    _file: fs::File,
}
impl WriterLock {
    #[allow(unsafe_code)]
    pub fn acquire(path: &Path) -> io::Result<Option<Self>> {
        for parent in path.ancestors() {
            if parent.is_symlink() {
                return Err(io::Error::other("refusing learning symlink"));
            }
        }
        fs::create_dir_all(
            path.parent()
                .ok_or_else(|| io::Error::other("missing learning parent"))?,
        )?;
        let file = fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .mode(0o600)
            .custom_flags(0x20000)
            .open(path.with_file_name("learning-writer.lock"))?;
        unsafe extern "C" {
            fn flock(fd: i32, operation: i32) -> i32;
        }
        // SAFETY: Linux flock only consumes this live owned descriptor and
        // integer flags, retaining no pointers. The File owns the lock lifetime.
        if unsafe { flock(file.as_raw_fd(), 2 | 4) } == 0 {
            Ok(Some(Self { _file: file }))
        } else {
            let error = io::Error::last_os_error();
            if error.kind() == io::ErrorKind::WouldBlock {
                Ok(None)
            } else {
                Err(error)
            }
        }
    }
}
pub struct Pending {
    request: PathBuf,
    pub status: &'static str,
}
impl Pending {
    pub fn finish(self) -> io::Result<()> {
        save_atomic(
            &self.request.with_extension("result"),
            &(self.status.to_owned() + "\n"),
        )?;
        fs::remove_file(self.request)
    }
}

pub fn pending(path: &Path, learning: &mut Learning) -> Option<Pending> {
    let directory = path.with_file_name("learning-edits");
    if directory.is_symlink() {
        return None;
    }
    let mut requests: Vec<_> = fs::read_dir(&directory)
        .ok()?
        .filter_map(Result::ok)
        .map(|e| e.path())
        .filter(|p| {
            p.extension().is_some_and(|s| s == "request")
                && p.file_stem()
                    .and_then(|s| s.to_str())
                    .is_some_and(|s| s.len() == 32 && s.bytes().all(|c| c.is_ascii_hexdigit()))
        })
        .collect();
    requests.sort();
    let request = requests.into_iter().next()?;
    let id = request.file_stem()?.to_str()?;
    if id.len() != 32 || !id.bytes().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    let apply = || -> Option<&'static str> {
        if learning.read_error {
            return Some("invalid");
        }
        if learning.edit_token == id {
            learning.dirty = true;
            return Some("applied");
        }
        if request.is_symlink() || fs::metadata(&request).ok()?.len() > 4096 {
            return None;
        }
        let text = fs::read_to_string(&request).ok()?;
        let lines: Vec<_> = text.lines().collect();
        if lines.len() != 4 || lines[0] != "# cowtype-learning-edit-v1" {
            return None;
        }
        let token = lines[1].strip_prefix("reset-token\t")?;
        let old: Vec<_> = lines[2].split('\t').collect();
        let new: Vec<_> = lines[3].split('\t').collect();
        if old.len() != 4 || new.len() != 3 || !valid(old[0], old[1]) {
            return None;
        }
        let count: u32 = old[2].parse().ok()?;
        let last: u64 = old[3].parse().ok()?;
        let new_count: u32 = new[2].parse().ok()?;
        if new_count > 0 && !valid(new[0], new[1]) {
            return None;
        }
        let key = (old[0].to_owned(), old[1].to_owned());
        let target = (new[0].to_owned(), new[1].to_owned());
        if token != learning.reset_token
            || !learning
                .records
                .get(&key)
                .is_some_and(|r| r.count == count && r.last == last)
            || (new_count > 0 && target != key && learning.records.contains_key(&target))
        {
            return Some("conflict");
        }
        learning.records.remove(&key);
        learning.sequence = learning.sequence.wrapping_add(1);
        if new_count > 0 {
            learning.records.insert(
                target,
                Record {
                    count: new_count,
                    last: now(),
                    revision: learning.sequence,
                },
            );
        }
        learning.edit_token = id.to_owned();
        learning.dirty = true;
        Some("applied")
    };
    let mut apply = apply;
    let status = apply().unwrap_or("invalid");
    Some(Pending { request, status })
}

/// Explicit settings helper only. An active input method processes the request
/// itself; otherwise take the same writer lock and apply it offline.
pub fn apply_once(path: &Path) -> io::Result<bool> {
    let Some(_lock) = WriterLock::acquire(path)? else {
        return Ok(false);
    };
    let mut learning = super::service::load_for_writer(path);
    // Drain a bounded batch so abandoned requests cannot hold up a new edit.
    for _ in 0..128 {
        super::service::apply_reset(path, &mut learning);
        let request = pending(path, &mut learning);
        if learning.dirty && !learning.read_error {
            save_atomic(path, &learning.serialize())?;
            learning.dirty = false;
        }
        if let Some(request) = request {
            request.finish()?;
        } else {
            break;
        }
    }
    Ok(true)
}
