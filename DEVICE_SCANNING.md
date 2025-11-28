# Device Scanning Feature

## Overview

The interceptor now supports automatic Bluetooth device discovery, allowing you to scan for nearby devices and select them interactively instead of manually finding MAC addresses.

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

### 2. New Data Structure: `bt_device_t`
**Location:** `interceptor/bt_utils.h`

```c
typedef struct {
    char addr[18];      // MAC address (e.g., "AA:BB:CC:DD:EE:FF")
    char name[248];     // Device name or "[Unknown]"
    int rssi;           // Signal strength (not yet implemented)
} bt_device_t;
```

### 3. Interactive Selection Functions
**Location:** `interceptor/main.c`

- `display_devices()` - Shows formatted table of discovered devices
- `select_device()` - Prompts user to select a device from the list

### 4. New Command Line Option
**Option:** `-S` - Scan mode

When enabled, the interceptor will:
1. Scan for nearby Bluetooth devices
2. Display all found devices in a formatted table
3. Prompt for source device selection
4. Prompt for target device selection
5. Optionally rescan if devices not found

## Usage Examples

### Example 1: Full Interactive Mode

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
...
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

No scanning, directly uses provided MAC addresses.

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

## Network Check Reminder

After selecting devices, ensure:
- ✅ Raspberry Pi is on same network as mobile device (for TCP streaming)
- ✅ Source device (phone) is unpaired from target device (headphones)
- ✅ Target device (headphones) is in pairing mode or previously paired with Pi

## Future Enhancements

Possible improvements:
- [ ] Show signal strength (RSSI) for each device
- [ ] Filter devices by type (audio devices only)
- [ ] Cache recently used devices
- [ ] Auto-detect A2DP capable devices
- [ ] Show device class/type icons
- [ ] Continuous scan mode with live updates
