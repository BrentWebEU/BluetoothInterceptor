# Changelog

## Version 2.0 - Active MITM Capabilities (Current)

### Major Features

#### 🎯 Automatic Device Scanning
- **What:** Discovers all nearby Bluetooth devices with MAC addresses and names
- **Why:** No need to manually find MAC addresses using bluetoothctl
- **How:** HCI inquiry-based scanning with 8-second duration
- **Files:** `bt_utils.c::bt_scan_devices()`, `main.c::display_devices()`

#### 🔓 Automatic Pairing
- **What:** Fully automated Bluetooth pairing without manual commands
- **Why:** Eliminates error-prone manual bluetoothctl interaction
- **How:** Scripted bluetoothctl commands with output parsing
- **Files:** `bt_utils.c::bt_auto_pair_device()`
- **Methods:**
  - Power on adapter
  - Scan for device
  - Initiate pairing
  - Trust device
  - Attempt connection
  - Wait for BlueZ persistence

#### 💥 Force Disconnect (NEW!)
- **What:** Breaks existing Bluetooth connections before MITM
- **Why:** Real-world devices are already paired and connected
- **How:** Multi-method progressive disconnect strategy
- **Files:** `bt_utils.c::bt_force_disconnect()`, `bt_utils.c::bt_disconnect_device()`
- **Methods:**
  1. Polite disconnect (bluetoothctl disconnect)
  2. Pairing removal (bluetoothctl remove)
  3. HCI disconnect (hcitool dc)
  4. RF jamming guidance (external hardware)

#### 📊 Connection Status Detection
- **What:** Shows which devices are currently connected
- **Why:** Identifies devices that need force disconnect
- **How:** Queries bluetoothctl for connection status
- **Files:** `bt_utils.c::bt_check_connection_status()`
- **Display:** Shows "CONNECTED" or "Available" in device list

### User Experience Improvements

**Before (v1.0):**
```bash
# Step 1: Find MAC addresses manually
sudo bluetoothctl
scan on
# Note down addresses...

# Step 2: Pair manually
pair AA:BB:CC:DD:EE:FF
trust AA:BB:CC:DD:EE:FF
connect AA:BB:CC:DD:EE:FF
exit

# Step 3: Run interceptor
sudo ./bt_interceptor -s AA:BB:CC:DD:EE:FF -t 11:22:33:44:55:66

# Step 4: Wait for prompt and press Enter
```

**After (v2.0):**
```bash
sudo ./bt_interceptor -S
# Select devices from list - done!
```

### Technical Changes

#### Data Structure Updates
```c
// Added connection status field
typedef struct {
    char addr[18];
    char name[248];
    int rssi;
    int connected;      // NEW: 1 if connected, 0 if available
} bt_device_t;
```

#### New Functions
```c
int bt_scan_devices(bt_device_t *devices, int max_devices, int scan_duration);
int bt_auto_pair_device(const char *device_mac);
int bt_disconnect_device(const char *device_mac);
int bt_force_disconnect(const char *source_mac, const char *target_mac);
int bt_check_connection_status(const char *device_mac);
```

#### Modified Functions
- `main()` - Added scanning loop and force disconnect integration
- `display_devices()` - Now shows connection status
- `select_device()` - Added rescan capability

### Documentation

#### New Documents
- **DEVICE_SCANNING.md** - Device discovery feature details
- **AUTO_PAIRING.md** - Automatic pairing implementation
- **FORCE_DISCONNECT.md** - Connection breaking techniques
- **CHANGELOG.md** - This file

#### Updated Documents
- **README.md** - Reflects new capabilities
- **QUICK_REFERENCE.md** - Simplified quick start
- **interceptor/README.md** - Updated features list
- **INTEGRATION_GUIDE.md** - Updated workflows

### Requirements

#### Software
- `bluetoothctl` (BlueZ) - Required for auto-pairing
- `hcitool` (optional) - For HCI-level disconnect
- `timeout` command - For preventing hangs

#### Hardware
- Bluetooth adapter (built-in or USB)
- Optional: Ubertooth One for RF attacks
- Optional: 2.4GHz RF jammer (illegal in most jurisdictions)

### Breaking Changes
- None - fully backward compatible
- Manual mode still works with `-s` and `-t` flags
- New `-S` flag enables scanning mode

### Performance
- Scan duration: 8 seconds
- Auto-pairing time: 30-45 seconds
- Force disconnect: 2-15 seconds (method-dependent)
- Total setup time: ~1-2 minutes (vs 5-10 minutes manual)

### Known Issues
1. Some devices resist all software disconnect methods
2. Auto-reconnection can interfere with MITM setup
3. RF jamming requires external hardware (not implemented)
4. Link key extraction fails if pairing didn't persist

### Future Roadmap
- [ ] Ubertooth One integration for de-auth attacks
- [ ] Connection handle tracking for targeted disconnect
- [ ] Parallel disconnect attempts
- [ ] Monitor mode for automatic reconnection detection
- [ ] D-Bus API integration (bypass bluetoothctl)
- [ ] GUI for device selection

## Version 1.0 - Initial Release

### Features
- L2CAP socket handling
- Manual MAC address spoofing
- Manual pairing workflow
- Link key extraction from BlueZ
- TCP streaming server
- Bidirectional relay with select() loop
- Crypto stubs (E0 cipher placeholder)

### Limitations
- Required manual bluetoothctl commands
- No device scanning
- Only worked on unpaired devices
- Manual MAC address lookup
- No connection breaking capability

---

**Current Version:** 2.0  
**Release Date:** 2025-11-28  
**Status:** Active MITM capabilities implemented
