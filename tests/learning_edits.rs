// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::learning::{
    edits::{apply_once, WriterLock},
    now, save_atomic,
    service::Service,
    Learning,
};
use std::{
    fs,
    path::PathBuf,
    sync::atomic::{AtomicUsize, Ordering},
    time::{Duration, Instant},
};
static SEQUENCE: AtomicUsize = AtomicUsize::new(0);
struct Fixture(PathBuf);
impl Fixture {
    fn new() -> Self {
        let root = std::env::temp_dir().join(format!(
            "cowtype-edit-{}-{}",
            std::process::id(),
            SEQUENCE.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(root.join("learning-edits")).unwrap();
        let f = Self(root);
        save_atomic(
            &f.path(),
            "# cowtype-learning-v1\n# reset-token\t\nshan\t杉\t3\t100\n",
        )
        .unwrap();
        f
    }
    fn path(&self) -> PathBuf {
        self.0.join("learning-v1.tsv")
    }
    fn queue(&self, id: &str, token: &str, old: &str, new: &str) {
        save_atomic(
            &self.0.join("learning-edits").join(format!("{id}.request")),
            &format!("# cowtype-learning-edit-v1\nreset-token\t{token}\n{old}\n{new}\n"),
        )
        .unwrap();
    }
    fn status(&self, id: &str) -> String {
        fs::read_to_string(self.0.join("learning-edits").join(format!("{id}.result"))).unwrap()
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
const ID: &str = "00000000000000000000000000000001";
const ID2: &str = "00000000000000000000000000000002";
#[test]
fn offline_rename_weight_delete_and_idempotent_replay() {
    let f = Fixture::new();
    f.queue(ID, "", "shan\t杉\t3\t100", "shan\t山\t8");
    assert!(apply_once(&f.path()).unwrap());
    assert_eq!(f.status(ID), "applied\n");
    let first = fs::read_to_string(f.path()).unwrap();
    assert_eq!(Learning::load(&f.path()).score("shan", "山", now()), 8);
    assert_eq!(Learning::load(&f.path()).score("shan", "杉", 100), 0);
    f.queue(ID, "", "shan\t杉\t3\t100", "shan\t山\t8");
    apply_once(&f.path()).unwrap();
    assert_eq!(fs::read_to_string(f.path()).unwrap(), first);
    let row = first.lines().find(|l| l.starts_with("shan\t")).unwrap();
    f.queue(ID2, "", row, "shan\t山\t0");
    apply_once(&f.path()).unwrap();
    assert_eq!(f.status(ID2), "applied\n");
    assert_eq!(Learning::load(&f.path()).score("shan", "山", now()), 0);
}
#[test]
fn conflicts_reset_and_bad_requests_preserve_records() {
    let f = Fixture::new();
    let original = fs::read_to_string(f.path()).unwrap();
    f.queue(ID, "", "shan\t杉\t2\t100", "shan\t山\t8");
    // An unrelated filename must not stall valid requests.
    fs::write(f.0.join("learning-edits/!.request"), "invalid").unwrap();
    apply_once(&f.path()).unwrap();
    assert_eq!(f.status(ID), "conflict\n");
    assert_eq!(fs::read_to_string(f.path()).unwrap(), original);
    f.queue(ID2, "", "shan\t杉\t3\t100", "shan\t山\t8");
    fs::write(f.0.join("learning-reset-v1"), "fresh-reset").unwrap();
    apply_once(&f.path()).unwrap();
    assert_eq!(f.status(ID2), "conflict\n");
    assert_eq!(Learning::load(&f.path()).score("shan", "杉", 100), 0);
}
#[test]
fn corrupt_file_with_old_reset_and_symlink_are_never_replaced() {
    use std::os::unix::fs::symlink;
    let f = Fixture::new();
    let marker = f.0.join("learning-reset-v1");
    fs::write(&marker, "old-reset").unwrap();
    fs::File::open(marker)
        .unwrap()
        .set_modified(std::time::SystemTime::UNIX_EPOCH)
        .unwrap();
    fs::write(f.path(), "unknown format\n").unwrap();
    f.queue(ID, "", "shan\t杉\t3\t100", "shan\t山\t8");
    apply_once(&f.path()).unwrap();
    assert_eq!(f.status(ID), "invalid\n");
    assert_eq!(fs::read_to_string(f.path()).unwrap(), "unknown format\n");
    let link = f.0.join("link.tsv");
    symlink(f.path(), &link).unwrap();
    assert!(apply_once(&link).is_err());
}
#[test]
fn running_writer_applies_edits_and_preserves_new_confirmations() {
    let f = Fixture::new();
    let service = Service::new(f.path());
    assert!(WriterLock::acquire(&f.path()).unwrap().is_none());
    assert!(!apply_once(&f.path()).unwrap());
    service
        .state
        .lock()
        .unwrap()
        .confirm("ni hao", "你好", now());
    f.queue(ID, "", "shan\t杉\t3\t100", "shan\t山\t9");
    let deadline = Instant::now() + Duration::from_secs(4);
    while !f.0.join(format!("learning-edits/{ID}.result")).exists() {
        assert!(Instant::now() < deadline, "writer response timed out");
        std::thread::sleep(Duration::from_millis(20));
    }
    assert_eq!(f.status(ID), "applied\n");
    assert_eq!(service.state.lock().unwrap().score("shan", "山", now()), 9);
    drop(service);
    let loaded = Learning::load(&f.path());
    assert_eq!(loaded.score("shan", "山", now()), 9);
    assert_eq!(loaded.score("ni hao", "你好", now()), 1);
}
