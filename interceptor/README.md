# Bluetooth MITM Interceptor

A C application for Raspberry Pi that performs Bluetooth MITM attacks to intercept and relay audio traffic between a source device (phone) and target device (headphones).

## Features

- L2CAP socket handling for Bluetooth connections
- MAC address spoofing support
- Link key extraction from BlueZ pairing data
- Real-time TCP streaming server for decrypted audio
- Multi-socket relay with select() event loop
- Modular architecture with separate utilities for Bluetooth, TCP, and crypto operations

## Prerequisites

### Hardware
- Raspberry Pi 3B+ or newer
- Bluetooth dongle (optional, for monitor mode/injection)

### Software
```bash
sudo apt-get update
sudo apt-get install build-essential libbluetooth-dev
```

Optional for MAC spoofing:
```bash
sudo apt-get install bluez-tools
```

## Building

```bash
cd interceptor
make
```

## Usage

### Interactive Mode (Recommended)

Scan for nearby devices and select them interactively:

```bash
sudo ./bt_interceptor -S
```

The interceptor will:
1. Scan for nearby Bluetooth devices (8 seconds)
2. Display a list of found devices with MAC addresses and names
3. Prompt you to select the source device (phone)
4. Prompt you to select the target device (headphones)
5. Begin the MITM attack

### Manual Mode

Specify MAC addresses directly:

```bash
sudo ./bt_interceptor -s <phone_mac> -t <headset_mac>
```

### Options

- `-S` - Scan for nearby devices and select interactively
- `-s <source_mac>` - Source device MAC address (phone)
- `-t <target_mac>` - Target device MAC address (headphones)
- `-p <psm>` - L2CAP PSM (default: 25 for A2DP)
- `-P <port>` - TCP server port (default: 8888)
- `-h` - Show help

### Examples

**Interactive scanning:**
```bash
sudo ./bt_interceptor -S
```

**Manual MAC addresses:**
```bash
sudo ./bt_interceptor -s AA:BB:CC:DD:EE:FF -t 11:22:33:44:55:66 -P 9000
```

**Scan and override one MAC:**
```bash
sudo ./bt_interceptor -S -t 11:22:33:44:55:66
# Will scan for source device, use specified target
```

## Architecture

### Modules

1. **main.c** - Main application logic and relay loop
2. **bt_utils.c/h** - Bluetooth socket operations and utilities
3. **tcp_server.c/h** - TCP streaming server for mobile app
4. **crypto.c/h** - Encryption/decryption stubs (E0 cipher placeholder)
5. **config.h** - Configuration constants and debug macros

### Flow

1. Spoof MAC address to match source device
2. Pair with target device (manual step via bluetoothctl)
3. Extract link key from `/var/lib/bluetooth/`
4. Initialize crypto module with link key
5. Start TCP server for streaming
6. Accept connection from source device (phone)
7. Connect to target device (headset)
8. Relay traffic bidirectionally:
   - Phone → Decrypt → Stream to TCP → Re-encrypt → Headset
   - Headset → Phone (control data)

## MITM Attack Strategy

This interceptor implements an active MITM attack:

1. **Jamming** - Target device must be disconnected from source
2. **Spoofing** - Pi assumes source device's MAC address
3. **Pairing** - Pi pairs with target device as fake source
4. **Key Extraction** - Link key extracted from BlueZ storage
5. **Relaying** - Bidirectional traffic relay with decryption/streaming

## Security Notes

**WARNING**: This tool is for educational and authorized security research only. Unauthorized interception of Bluetooth communications may be illegal in your jurisdiction.

## Limitations

- E0 cipher implementation is stubbed (data passed through unchanged)
- MAC spoofing requires external tools (bdaddr) or kernel patches
- Single connection relay (no multi-target support)
- A2DP profile assumed (PSM 25)
- Requires root privileges for Bluetooth operations

## TODO

- [ ] Implement full Bluetooth E0 stream cipher
- [ ] Add support for Bluetooth 4.0+ LE encryption
- [ ] Multi-threaded architecture with pthreads
- [ ] Dynamic PSM detection and profile switching
- [ ] Packet capture/logging functionality
- [ ] Support for HCI raw sockets and monitor mode
- [ ] Automated pairing process
- [ ] Web interface for monitoring

## License

Educational purposes only. Use responsibly and with authorization.
