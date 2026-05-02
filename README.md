# btintercept

A Bluetooth MITM interceptor written in Rust. Built for a cybersecurity school project.

It sits between a phone and a Bluetooth device (e.g. headphones), transparently relaying all traffic while logging every packet. The client can't tell they're connected to an interceptor — the tool clones the target's MAC address, device name, Class-of-Device, and SDP service records.

> **For authorized and educational use only.** Only test on devices you own or have explicit permission to test on.

---

## How it works

1. You pick a target device (headphones, speaker, etc.)
2. The tool disconnects it from the phone
3. Your adapter clones the target's full identity (MAC, name, device class, services)
4. The phone reconnects — to your adapter, thinking it's the real device
5. Your adapter connects to the real target and relays everything through
6. All packets are logged. Optionally streamed over TCP.

---

## Requirements

- Linux with BlueZ
- A Bluetooth adapter that supports MAC spoofing (most USB dongles work)
- Root / sudo
- `bdaddr`, `sdptool`, `hciconfig`, `bluetoothctl`, `hcitool` installed

```bash
sudo apt install bluez bluez-tools
# bdaddr is in bluez-utils or build from source
```

---

## Build & run

```bash
cd btintercept
cargo build --release

sudo ./target/release/btintercept -S        # interactive scan
sudo ./target/release/btintercept -t AA:BB:CC:DD:EE:FF  # direct MAC
```

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `-S` | — | Interactive mode — scan and pick a device |
| `-t <mac>` | — | Target device MAC address |
| `-p <psm>` | 25 | L2CAP PSM (25 = A2DP audio) |
| `-P <port>` | 8888 | TCP port for packet streaming |

---

## TCP streaming

While intercepting, packets are streamed raw over TCP so you can pipe them into another tool:

```bash
nc <pi-ip> 8888 | xxd   # inspect packets live
```

---

## Project structure

```
btintercept/      Rust interceptor (the actual tool)
client/           React Native app (optional TCP monitor)
interceptor/      Old C prototype — ignored, don't use
```
