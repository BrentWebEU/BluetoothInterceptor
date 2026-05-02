pub const TCP_SERVER_PORT: u16 = 8888;
pub const BLUETOOTH_INFO_PATH: &str = "/var/lib/bluetooth";
pub const MAX_BUFFER_SIZE: usize = 4096;
pub const BACKLOG: u32 = 5;
pub const DEBUG: bool = true;

#[macro_export]
macro_rules! debug_print {
    ($($arg:tt)*) => {
        if $crate::config::DEBUG {
            eprintln!("[DEBUG] {}", format!($($arg)*));
        }
    };
}

#[macro_export]
macro_rules! error_print {
    ($($arg:tt)*) => {
        eprintln!("[ERROR] {}", format!($($arg)*));
    };
}

#[macro_export]
macro_rules! warn_print {
    ($($arg:tt)*) => {
        println!("[WARN] {}", format!($($arg)*));
    };
}

#[macro_export]
macro_rules! info_print {
    ($($arg:tt)*) => {
        println!("[INFO] {}", format!($($arg)*));
    };
}
