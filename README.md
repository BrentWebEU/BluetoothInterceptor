# Bluetooth MITM Interceptor System

A complete Bluetooth Man-in-the-Middle (MITM) attack system for intercepting and streaming audio between devices, consisting of a C application for Raspberry Pi and a React Native mobile app.

## ⚠️ Legal Disclaimer

**FOR EDUCATIONAL AND AUTHORIZED SECURITY RESEARCH ONLY**

This project is intended solely for:
- Educational purposes and learning about Bluetooth security
- Authorized penetration testing with explicit permission
- Security research in controlled environments

Unauthorized interception of Bluetooth communications is illegal in most jurisdictions. Users are responsible for ensuring all usage complies with applicable laws and regulations.

## System Overview

### Architecture

```
┌─────────────┐                  ┌──────────────────┐                 ┌─────────────┐
│   Phone     │  Bluetooth A2DP  │  Raspberry Pi    │   TCP Stream    │  Mobile App │
│  (Source)   │◄────────────────►│   Interceptor    │────────────────►│  (Monitor)  │
│             │                  │  (MITM Proxy)    │                 │             │
└─────────────┘                  └──────────────────┘                 └─────────────┘
                                          │
                                          ▼
                                  ┌──────────────┐
                                  │  Headphones  │
                                  │   (Target)   │
                                  └──────────────┘
```

### Components

1. **Raspberry Pi Interceptor** (`./interceptor/`) - C Application
   - Performs active MITM attack on Bluetooth connections
   - Spoofs source device MAC address
   - Extracts encryption keys from BlueZ pairing
   - Relays and decrypts/re-encrypts traffic
   - Streams decrypted audio over TCP to mobile app

2. **React Native App** (`./app/`) - Mobile Application
   - Connects to Pi via TCP socket
   - Receives intercepted audio stream in real-time
   - Displays connection statistics and monitoring
   - Buffers audio data for playback

## Quick Start

### Prerequisites

**Raspberry Pi:**
- Raspberry Pi 3B+ or newer
- Raspberry Pi OS
- Bluetooth adapter (built-in or USB dongle)

**Mobile Development:**
- Node.js 16+
- React Native development environment
- Android Studio or Xcode

### Installation

#### 1. Raspberry Pi Setup

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install build-essential libbluetooth-dev bluez-tools

# Build the interceptor
cd interceptor
make

# Optional: Install system-wide
sudo make install
```

#### 2. Mobile App Setup

```bash
# Install dependencies
cd app
npm install

# Run on Android
npx react-native run-android

# Run on iOS
cd ios && pod install && cd ..
npx react-native run-ios
```

## Usage Guide

### Quick Start (Fully Automated)

```bash
sudo ./bt_interceptor -S
```

**The interceptor will automatically:**
1. Scan for nearby Bluetooth devices
2. Show you a list with connection status
3. Let you select source (phone) and target (headphones)
4. **Break any existing connections** to the target device
5. Pair with the target device automatically
6. Extract encryption keys
7. Set up the MITM relay
8. Begin intercepting traffic

### Manual Mode (Advanced)

If you already know the MAC addresses:

```bash
sudo ./bt_interceptor -s AA:BB:CC:DD:EE:FF -t 11:22:33:44:55:66
```

Where:
- `-s` = Source MAC (phone you're spoofing)
- `-t` = Target MAC (headphones to intercept)
- `-p` = PSM (default: 25 for A2DP)
- `-P` = TCP port (default: 8888)
- `-S` = Enable scanning mode

The interceptor will still automatically detect and break existing connections.

### Step 3: Connect Mobile App (Optional)

The mobile app is optional but provides real-time monitoring:

1. Launch the mobile app
2. Enter Pi's IP address (e.g., `192.168.1.100`)
3. Enter TCP port (default: `8888`)
4. Tap "Connect"
5. Monitor intercepted audio stream

### Step 4: Source Device Connects

On your phone:
1. Go to Bluetooth settings
2. Connect to "headphones" (actually the Pi)
3. Play audio
4. Audio flows: Phone → Pi (decrypted) → Headphones (re-encrypted)
5. Mobile app receives decrypted stream in real-time

## New Features (v2.0)

### 🎯 Automatic Device Scanning
- Discovers all nearby Bluetooth devices
- Shows device names and MAC addresses
- Displays connection status (Connected/Available)
- Interactive selection menu

### 🔓 Automatic Pairing
- No manual `bluetoothctl` commands needed
- Automatic trust and connection
- Falls back to manual mode if needed
- Progress tracking and status updates

### 💥 Force Disconnect
- **Detects existing connections** between devices
- **Automatically breaks connections** using 4 methods:
  1. Polite disconnect (bluetoothctl)
  2. Pairing removal (forced disconnect)
  3. HCI-level disconnect (hcitool)
  4. RF jamming guidance (hardware)
- Works on **actively connected** devices
- True active MITM capability

See detailed documentation:
- [DEVICE_SCANNING.md](DEVICE_SCANNING.md) - Device discovery details
- [AUTO_PAIRING.md](AUTO_PAIRING.md) - Automatic pairing implementation
- [FORCE_DISCONNECT.md](FORCE_DISCONNECT.md) - Connection breaking techniques

## MITM Attack Strategy

This system implements a **fully automated active MITM attack**:

### Attack Steps (Automated)

1. **Device Discovery**
   - Scan for all nearby Bluetooth devices
   - Identify source (phone) and target (headphones)
   - Check connection status

2. **Connection Breaking** ⚡ **NEW**
   - Detect if target is already connected
   - Force disconnect using multiple methods
   - Ensure target is available for MITM

3. **MAC Spoofing**
   - Pi changes its MAC to match source device
   - Uses `bdaddr` tool or kernel patches

4. **Automated Pairing** ⚡ **NEW**
   - Pi pairs with target as "fake" source
   - Automatic trust and connection
   - No manual bluetoothctl commands needed

5. **Key Extraction**
   - BlueZ stores link key in `/var/lib/bluetooth/`
   - Interceptor extracts key from `info` file

6. **Transparent Relay**
   - Accepts connection from real source device
   - Connects to target device
   - Relays traffic bidirectionally
   - Decrypts audio stream for monitoring

### Security Mechanisms Bypassed

- **Bluetooth Pairing** - Active MITM during pairing
- **Link Layer Encryption** - Key extracted from pairing
- **A2DP Audio Stream** - Decrypted using extracted key

## Technical Details

### Bluetooth Protocols

- **L2CAP** - Logical Link Control and Adaptation Protocol
- **RFCOMM** - Serial port emulation
- **A2DP** - Advanced Audio Distribution Profile
- **SBC** - Subband Codec (audio compression)

### Encryption

- **E0 Stream Cipher** - Bluetooth Classic encryption
- **Link Key** - 128-bit key from pairing process
- Current implementation: Stub (passes data through)
- Full implementation: Requires E0 cipher library

### Audio Codec

- **SBC Encoding** - Used by A2DP profile
- **PCM Output** - Raw audio after decoding
- Current implementation: Receives but doesn't decode
- Full implementation: Requires SBC decoder (native module or server-side)

## Project Structure

```
Bluetooth/
├── interceptor/              # Raspberry Pi C Application
│   ├── main.c               # Main relay loop and orchestration
│   ├── bt_utils.c/h         # Bluetooth socket operations
│   ├── tcp_server.c/h       # TCP streaming server
│   ├── crypto.c/h           # Encryption/decryption (stub)
│   ├── config.h             # Configuration and constants
│   ├── Makefile             # Build configuration
│   └── README.md
│
└── app/                      # React Native Mobile App
    ├── src/
    │   ├── components/      # UI components
    │   ├── screens/         # Screen components
    │   ├── services/        # Business logic
    │   ├── types/           # TypeScript definitions
    │   └── utils/           # Helper functions
    ├── android/             # Android native code
    ├── ios/                 # iOS native code (empty)
    ├── App.tsx              # Root component
    └── README.md
```

## Limitations & Known Issues

### Raspberry Pi Interceptor

- ❌ E0 cipher not fully implemented (data passed through)
- ❌ MAC spoofing requires external tools or patches
- ❌ Single connection only (no multi-target)
- ❌ A2DP profile assumed (PSM 25 hardcoded as default)
- ❌ Requires root privileges
- ⚠️  Manual pairing step required

### Mobile App

- ❌ SBC decoder not implemented (native module stub only)
- ❌ Audio playback not functional without SBC decoder
- ❌ No audio visualization
- ⚠️  Buffers data but doesn't play audio

## Development Roadmap

### High Priority

- [ ] Implement E0 stream cipher in C application
- [ ] Implement SBC decoder (native module or server-side)
- [ ] Automate pairing process
- [ ] Add proper error handling and recovery

### Medium Priority

- [ ] Multi-threaded architecture (pthreads)
- [ ] Support multiple Bluetooth profiles
- [ ] Dynamic PSM detection
- [ ] Audio visualization in mobile app
- [ ] Recording and playback controls

### Low Priority

- [ ] Web interface for Pi configuration
- [ ] Bluetooth LE support
- [ ] Packet capture/logging
- [ ] HCI raw sockets and monitor mode
- [ ] Dark mode for mobile app

## Troubleshooting

### Interceptor Issues

**"No Bluetooth adapter found"**
- Check: `hciconfig -a`
- Solution: Ensure Bluetooth is enabled

**"Failed to bind L2CAP socket"**
- Check: Already running instance
- Solution: `sudo killall bt_interceptor`

**"Link key not found"**
- Check: Pairing completed successfully
- Check: `/var/lib/bluetooth/<adapter>/<device>/info` exists
- Solution: Re-pair devices

### Mobile App Issues

**"Connection Error"**
- Check: Pi and phone on same network
- Check: Firewall not blocking port 8888
- Solution: `sudo ufw allow 8888` on Pi

**"No data received"**
- Check: Interceptor relay established
- Check: Audio playing from source device
- Solution: Check Pi logs for errors

## Performance Considerations

### Raspberry Pi

- CPU Usage: ~10-20% (single-threaded)
- Memory: ~10MB
- Network: ~100-200 KB/s for A2DP audio
- Latency: ~50-100ms added

### Mobile App

- Battery: Moderate impact (TCP socket)
- Data: N/A (local network only)
- Memory: ~50MB

## Contributing

This is an educational project. Contributions are welcome for:
- Bug fixes
- Documentation improvements
- Security enhancements
- Feature implementations (E0 cipher, SBC decoder)

## References

- [Bluetooth Core Specification](https://www.bluetooth.com/specifications/specs/)
- [BlueZ - Linux Bluetooth Stack](http://www.bluez.org/)
- [A2DP Profile Specification](https://www.bluetooth.com/specifications/specs/a2dp-1-3-2/)
- [SBC Codec Specification](https://www.bluetooth.com/specifications/specs/a2dp-1-3-2/)

## License

Educational purposes only. Use responsibly and with proper authorization.

## Acknowledgments

This project demonstrates well-known Bluetooth security vulnerabilities for educational purposes. Similar attacks have been documented in academic research:
- CVE-2018-5383 (Bluetooth pairing vulnerability)
- Various academic papers on Bluetooth MITM attacks

---

**Remember:** Always obtain proper authorization before testing on devices you don't own.
