use std::sync::atomic::{AtomicBool, Ordering};

use crate::{debug_print, error_print, info_print};

static KEY_INITIALIZED: AtomicBool = AtomicBool::new(false);
static mut LINK_KEY: [u8; 16] = [0u8; 16];

pub fn init_link_key(key_hex: &str) -> Result<(), String> {
    if key_hex.len() != 32 {
        error_print!("Invalid link key length (expected 32 hex chars)");
        return Err("Invalid link key length".to_string());
    }

    unsafe {
        for i in 0..16 {
            let byte_str = &key_hex[i * 2..i * 2 + 2];
            LINK_KEY[i] = u8::from_str_radix(byte_str, 16)
                .map_err(|e| format!("Invalid hex char: {}", e))?;
        }
    }

    KEY_INITIALIZED.store(true, Ordering::SeqCst);
    info_print!("Link key initialized");
    Ok(())
}

// E0 cipher not fully implemented — passes data through unchanged (same as C stub).
pub fn decrypt_payload(encrypted: &[u8]) -> Result<Vec<u8>, String> {
    if !KEY_INITIALIZED.load(Ordering::SeqCst) {
        error_print!("Link key not initialized");
        return Err("Link key not initialized".to_string());
    }
    debug_print!(
        "Decrypting {} bytes (E0 cipher not fully implemented - copying data)",
        encrypted.len()
    );
    Ok(encrypted.to_vec())
}

pub fn encrypt_payload(plain: &[u8]) -> Result<Vec<u8>, String> {
    if !KEY_INITIALIZED.load(Ordering::SeqCst) {
        error_print!("Link key not initialized");
        return Err("Link key not initialized".to_string());
    }
    debug_print!(
        "Encrypting {} bytes (E0 cipher not fully implemented - copying data)",
        plain.len()
    );
    Ok(plain.to_vec())
}
