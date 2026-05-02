use libc::{c_int, socklen_t};
use std::process::Command;

use crate::config::BACKLOG;
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

/// Broad category derived from the Class-of-Device field.
#[derive(Debug, Clone, PartialEq)]
pub enum DeviceType {
    Phone,
    Audio,
    Computer,
    Other,
}

impl DeviceType {
    pub fn icon(&self) -> &'static str {
        match self {
            DeviceType::Phone    => "📱",
            DeviceType::Audio    => "🎧",
            DeviceType::Computer => "💻",
            DeviceType::Other    => "🔷",
        }
    }
}

/// Classify a device from its hex CoD string (with or without "0x" prefix).
pub fn classify_cod(cod_hex: &str) -> DeviceType {
    let clean = cod_hex.trim().trim_start_matches("0x").trim_start_matches("0X");
    let cod = u32::from_str_radix(clean, 16).unwrap_or(0);
    match (cod >> 8) & 0x1F {
        0x02 => DeviceType::Phone,
        0x04 => DeviceType::Audio,
        0x01 => DeviceType::Computer,
        _ => DeviceType::Other,
    }
}

/// A detected connection between two nearby devices.
pub struct DevicePair {
    /// The device to impersonate (usually the audio device / peripheral).
    pub target: BtDevice,
    /// The connecting device (phone/source). May be unknown.
    pub source: Option<BtDevice>,
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


// ── Pair detection ───────────────────────────────────────────────────────────

/// Scan for all nearby Bluetooth devices, classify them, and return detected
/// connections as (target, source) pairs.
///
/// Strategy:
///   1. `hcitool inq`           — raw inquiry, finds connectable devices + CoD
///   2. `bluetoothctl devices`  — names for paired/known devices
///   3. `bluetoothctl info`     — per-device: name, CoD, Connected status
///   4. Classification          — phone vs audio vs other from CoD major class
///   5. Pairing heuristic       — audio device with Connected=yes + phone in range
pub fn bt_find_connected_pairs() -> Vec<DevicePair> {
    info_print!("Scanning for nearby Bluetooth devices (~10 s)…");

    let mut devices: Vec<BtDevice> = Vec::new();

    // Pass 1: hcitool inq — finds any connectable device even if not discoverable.
    // Output lines: "\tAA:BB:CC:DD:EE:FF\tclock offset: 0x1234\tclass: 0x240404"
    let inq = run_popen("timeout 10 hcitool inq 2>/dev/null");
    for line in inq.lines() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.is_empty() || !is_valid_mac(parts[0]) {
            continue;
        }
        let mac = parts[0].to_uppercase();
        let cod = parts.windows(2)
            .find(|w| w[0] == "class:")
            .map(|w| w[1].trim_start_matches("0x").to_string());
        devices.push(BtDevice { addr: mac, cod, ..Default::default() });
    }

    // Pass 2: bluetoothctl devices — names for all devices BlueZ knows about.
    let btctl = run_popen("bluetoothctl -- devices 2>/dev/null");
    for line in btctl.lines() {
        let clean = strip_ansi_codes(line);
        let parts: Vec<&str> = clean.split_whitespace().collect();
        if parts.len() < 2 || parts[0] != "Device" || !is_valid_mac(parts[1]) {
            continue;
        }
        let mac = parts[1].to_uppercase();
        let name = if parts.len() >= 3 { parts[2..].join(" ") } else { String::new() };
        match devices.iter_mut().find(|d| d.addr.eq_ignore_ascii_case(&mac)) {
            Some(d) => { if d.name.is_empty() { d.name = name; } }
            None    => devices.push(BtDevice { addr: mac, name, ..Default::default() }),
        }
    }

    // Pass 3: bluetoothctl info per device — fill in name, CoD, and connected flag.
    for dev in devices.iter_mut() {
        let info = run_popen(&format!("bluetoothctl -- info {} 2>/dev/null", dev.addr));
        for line in info.lines() {
            let line = line.trim();
            if let Some(n) = line.strip_prefix("Name: ") {
                if dev.name.is_empty() { dev.name = n.trim().to_string(); }
            }
            if let Some(c) = line.strip_prefix("Class: ") {
                if dev.cod.is_none() {
                    dev.cod = Some(c.trim().trim_start_matches("0x").to_string());
                }
            }
            if line == "Connected: yes" { dev.connected = true; }
        }
        if dev.name.is_empty() {
            dev.name = "[Unknown]".to_string();
        }
    }

    // Build pairs.
    build_pairs(devices)
}

fn build_pairs(devices: Vec<BtDevice>) -> Vec<DevicePair> {
    let mut pairs: Vec<DevicePair> = Vec::new();

    let phones: Vec<BtDevice> = devices.iter()
        .filter(|d| d.cod.as_deref().map(classify_cod).unwrap_or(DeviceType::Other) == DeviceType::Phone)
        .cloned()
        .collect();

    // Primary: audio devices that BlueZ reports as connected.
    for dev in devices.iter() {
        if dev.cod.as_deref().map(classify_cod).unwrap_or(DeviceType::Other) == DeviceType::Audio
            && dev.connected
        {
            let source = if phones.len() == 1 {
                Some(phones[0].clone())
            } else {
                // Multiple phones in range — can't reliably tell which one.
                None
            };
            pairs.push(DevicePair { target: dev.clone(), source });
        }
    }

    // Fallback: any connected device (non-audio) if nothing found yet.
    if pairs.is_empty() {
        for dev in devices.iter().filter(|d| d.connected) {
            pairs.push(DevicePair { target: dev.clone(), source: phones.first().cloned() });
        }
    }

    // Last resort: all audio devices in range (connected flag may be absent for
    // devices not paired with this adapter).
    if pairs.is_empty() {
        for dev in devices.iter() {
            if dev.cod.as_deref().map(classify_cod).unwrap_or(DeviceType::Other) == DeviceType::Audio {
                pairs.push(DevicePair { target: dev.clone(), source: phones.first().cloned() });
            }
        }
    }

    pairs
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
