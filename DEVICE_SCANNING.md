# Device Scanning and Auto-Pairing Feature

## Overview

The interceptor now supports:
1. **Automatic Bluetooth device discovery** - Scan for nearby devices and see their MAC addresses and names
2. **Interactive device selection** - Choose devices from a visual list instead of typing MAC addresses
3. **Automatic pairing** - No need to manually run bluetoothctl commands; pairing happens automatically

## What Was Added

### 1. New Function: `bt_scan_devices()`
**Location:** `interceptor/bt_utils.c`

Scans for nearby Bluetooth devices using HCI inquiry and returns a list with:
- MAC address
- Device name
- RSSI (signal strength placeholder)

```c
int bt_scan_devices(bt_device_t *devices, int max_devices, int scan_duration);
```

### 2. New Function: `bt_auto_pair_device()`
**Location:** `interceptor/bt_utils.c`

Automatically pairs with a target Bluetooth device by:
- Powering on the Bluetooth adapter
- Scanning for the target device
- Removing existing pairing (if any)
- Initiating pairing
- Trusting the device
- Attempting connection
- Waiting for BlueZ to write pairing information

```c
int bt_auto_pair_device(const char *device_mac);
```

### 3. New Data Structure: `bt_device_t`
**Location:** `interceptor/bt_utils.h`

```c
typedef struct {
    char addr[18];      // MAC address (e.g., "AA:BB:CC:DD:EE:FF")
    char name[248];     // Device name or "[Unknown]"
    int rssi;           // Signal strength (not yet implemented)
} bt_device_t;
```

### 4. Interactive Selection Functions
**Location:** `interceptor/main.c`

- `display_devices()` - Shows formatted table of discovered devices
- `select_device()` - Prompts user to select a device from the list

### 5. New Command Line Option
**Option:** `-S` - Scan mode

When enabled, the interceptor will:
1. Scan for nearby Bluetooth devices
2. Display all found devices in a formatted table
3. Prompt for source device selection
4. Prompt for target device selection
5. Automatically pair with the target device
6. Optionally rescan if devices not found

## Usage Examples

### Example 1: Full Interactive Mode with Auto-Pairing

```bash
sudo ./bt_interceptor -S
```

**Output:**
```
[INFO] Scanning for Bluetooth devices (duration: 8 seconds)...
[INFO] Found 5 device(s)

═══════════════════════════════════════════════════════════════════════
  #  │  MAC Address       │  Device Name
═══════════════════════════════════════════════════════════════════════
  1  │  AA:BB:CC:DD:EE:FF  │  Samsung Galaxy S21
  2  │  11:22:33:44:55:66  │  Sony WH-1000XM4
  3  │  77:88:99:AA:BB:CC  │  Apple AirPods Pro
  4  │  DD:EE:FF:00:11:22  │  [Unknown]
  5  │  33:44:55:66:77:88  │  Bose QC35
═══════════════════════════════════════════════════════════════════════

Select source device (phone) (1-5, 0 to rescan): 1
Select target device (headphones) (1-5, 0 to rescan): 2

[INFO] Bluetooth MITM Interceptor
[INFO] Source (phone): AA:BB:CC:DD:EE:FF
[INFO] Target (headset): 11:22:33:44:55:66
[INFO] PSM: 25
[INFO] TCP Port: 8888
[INFO] Adapter address: XX:XX:XX:XX:XX:XX
[INFO] Step 1: Spoofing MAC address...
[INFO] Step 2: Auto-pairing with target device
[INFO] Ensure target device 11:22:33:44:55:66 is in pairing mode
[INFO] Auto-pairing will begin in 3 seconds...
[INFO] Attempting to pair with device: 11:22:33:44:55:66
[INFO] Powering on Bluetooth adapter...
[INFO] Scanning for device...
[INFO] Pairing with device...
[INFO] Pairing successful
[INFO] Trusting device...
[INFO] Connecting to device...
[INFO] Connection successful
[INFO] Auto-pairing completed
[INFO] Link key extracted: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
[INFO] Step 3: Creating TCP server
[INFO] TCP server listening on port 8888
[INFO] Step 4: Setting up Bluetooth relay
[INFO] Waiting for phone to connect...
```

### Example 2: Scan with Partial Override

```bash
sudo ./bt_interceptor -S -t 11:22:33:44:55:66
```

This will scan for source device but use the specified target MAC address.

### Example 3: Traditional Manual Mode (Still Works)

```bash
sudo ./bt_interceptor -s AA:BB:CC:DD:EE:FF -t 11:22:33:44:55:66
```

Even in manual mode, pairing is now automatic - no need to manually run bluetoothctl.

## Automatic Pairing Details

### What Happens During Auto-Pairing

The `bt_auto_pair_device()` function performs the following steps automatically:

1. **Check Existing Pairing** - Verifies if device is already paired
2. **Power On Adapter** - Ensures Bluetooth adapter is powered on
3. **Enable Discoverability** - Makes the adapter visible to other devices
4. **Scan for Target** - Searches for the target device (8 second scan)
5. **Remove Old Pairing** - Cleans up any existing pairing for fresh start
6. **Initiate Pairing** - Sends pairing request to target device
7. **Trust Device** - Marks device as trusted in BlueZ
8. **Connect** - Attempts to establish connection
9. **Wait for BlueZ** - Gives BlueZ time to write pairing info to disk

### Requirements for Auto-Pairing

For auto-pairing to work:
- ✅ Target device must be in **pairing mode** (discoverable)
- ✅ Target device must accept the pairing request
- ✅ `bluetoothctl` must be installed and accessible
- ✅ Must run with root/sudo privileges
- ✅ BlueZ Bluetooth stack must be running

### Fallback to Manual Pairing

If auto-pairing fails, the interceptor will:
1. Display error message
2. Show manual pairing instructions
3. Wait for you to complete pairing manually
4. Continue once you press Enter

## Technical Details

### Scanning Process

The `bt_scan_devices()` function:
1. Opens HCI device (Bluetooth adapter)
2. Performs HCI inquiry for specified duration (default: 8 seconds)
3. For each discovered device:
   - Retrieves MAC address
   - Attempts to read remote device name
   - Falls back to "[Unknown]" if name unavailable
4. Returns count of devices found

### Integration Points

**Modified Files:**
- `interceptor/bt_utils.h` - Added `bt_device_t` struct and `bt_scan_devices()` declaration
- `interceptor/bt_utils.c` - Implemented `bt_scan_devices()` using HCI inquiry
- `interceptor/main.c` - Added interactive UI and scan mode logic

**No changes required to:**
- `tcp_server.c/h` - TCP streaming unchanged
- `crypto.c/h` - Encryption stubs unchanged
- `config.h` - Configuration unchanged

## Compilation

The code compiles on systems with BlueZ libraries:

```bash
cd interceptor
make
```

**Requirements:**
- `libbluetooth-dev` (provides `bluetooth/hci.h` and `bluetooth/hci_lib.h`)
- Linux system with BlueZ stack (Raspberry Pi OS, Ubuntu, etc.)

**Note:** This won't compile on macOS as it lacks native BlueZ support. The interceptor is designed to run on Raspberry Pi with Linux.

## Benefits

1. **Easier setup** - No need to manually find MAC addresses using `bluetoothctl`
2. **Better UX** - Visual selection from a list of devices
3. **Fewer errors** - Less chance of typos in MAC addresses
4. **Device discovery** - See all nearby Bluetooth devices at once
5. **Flexibility** - Can rescan if target devices not found initially
6. **Fully automated** - Pairing happens automatically without manual bluetoothctl commands
7. **Error recovery** - Falls back to manual pairing if auto-pairing fails

## Network Check Reminder

After selecting devices, ensure:
- ✅ Raspberry Pi is on same network as mobile device (for TCP streaming)
- ✅ Source device (phone) is unpaired from target device (headphones)
- ✅ Target device (headphones) is in **pairing mode** before starting
- ✅ You have root/sudo privileges to run the interceptor

## Future Enhancements

Possible improvements:
- [ ] Show signal strength (RSSI) for each device
- [ ] Filter devices by type (audio devices only)
- [ ] Cache recently used devices
- [ ] Auto-detect A2DP capable devices
- [ ] Show device class/type icons
- [ ] Continuous scan mode with live updates
