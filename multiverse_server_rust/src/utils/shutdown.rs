use std::sync::atomic::{AtomicBool, Ordering};

static SHOULD_SHUTDOWN: AtomicBool = AtomicBool::new(false);

pub fn should_shutdown() -> bool {
    SHOULD_SHUTDOWN.load(Ordering::Relaxed)
}

pub fn set_shutdown() {
    SHOULD_SHUTDOWN.store(true, Ordering::Relaxed);
}