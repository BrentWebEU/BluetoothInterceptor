# Automatic Bluetooth Pairing

## Overview

The interceptor now features **fully automatic Bluetooth pairing** via bluetoothctl, eliminating the need for manual pairing commands. Combined with device scanning, the entire setup process is now streamlined and user-friendly.

## How It Works

### Traditional Manual Process (Old Way)

Before this feature, you had to:
1. Manually run `bluetoothctl` in a separate terminal
2. Type commands: `scan on`, `pair XX:XX:XX:XX:XX:XX`, `trust XX:XX:XX:XX:XX:XX`, `connect XX:XX:XX:XX:XX:XX`
3. Switch back to the interceptor terminal
4. Press Enter to continue
5. Hope you typed everything correctly

### New Automatic Process

Now the interceptor does everything for you:
1. You select the target device (or provide MAC address)
2. The interceptor automatically:
   - Powers on the Bluetooth adapter
   - Scans for the target device
   - Removes any old pairing
   - Initiates pairing with the device
   - Trusts the device
   - Attempts connection
   - Waits for BlueZ to write pairing data
3. Continues automatically to the next step

## Implementation Details

### Core Function: `bt_auto_pair_device()`

```c
int bt_auto_pair_device(const char *device_mac);
```

**Location:** `interceptor/bt_utils.c`

**Process Flow:**

```
┌─────────────────────────────────────────────────┐
│ 1. Check if already paired                      │
│    - Query BlueZ for existing pairing           │
│    - Remove old pairing if found                │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│ 2. Power on Bluetooth adapter                   │
│    - Execute: bluetoothctl power on             │
│    - Wait 2 seconds for adapter initialization  │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│ 3. Enable discoverable mode                     │
│    - Execute: bluetoothctl discoverable on      │
│    - Wait 1 second                              │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│ 4. Scan for target device                       │
│    - Execute: bluetoothctl scan on              │
│    - Timeout after 8 seconds                    │
│    - Parse output for target MAC                │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│ 5. Initiate pairing                             │
│    - Execute: bluetoothctl pair <MAC>           │
│    - Wait up to 30 seconds                      │
│    - Parse output for success/failure           │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│ 6. Trust device                                 │
│    - Execute: bluetoothctl trust <MAC>          │
│    - Verify trust succeeded                     │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│ 7. Connect to device                            │
│    - Execute: bluetoothctl connect <MAC>        │
│    - Timeout after 15 seconds                   │
│    - Connection may fail (OK for pairing)       │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│ 8. Wait for BlueZ to persist pairing info       │
│    - Sleep 3 seconds                            │
│    - Allows BlueZ to write to disk              │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
         ┌───────┴────────┐
         │  Success/Fail   │
         └────────────────┘
```

### Technical Approach

The function uses `system()` and `popen()` to interact with `bluetoothctl`:

1. **Command Execution** - Uses `system()` for fire-and-forget commands
2. **Output Parsing** - Uses `popen()` to capture and parse command output
3. **Timeout Handling** - Uses `timeout` command to prevent hangs
4. **Error Detection** - Parses output for success/error messages
5. **Retry Logic** - Removes old pairing before attempting new pairing

### Key Code Snippets

**Pairing Command:**
```c
snprintf(cmd, sizeof(cmd), 
    "timeout 30 bash -c \"echo -e 'pair %s\\nquit' | bluetoothctl 2>&1\"", 
    device_mac);

fp = popen(cmd, "r");
while (fgets(output, sizeof(output), fp)) {
    if (strstr(output, "Pairing successful")) {
        paired = 1;
    }
}
```

**Trust Command:**
```c
snprintf(cmd, sizeof(cmd), 
    "echo -e 'trust %s\\nquit' | bluetoothctl 2>&1", 
    device_mac);
```

## Usage

### With Device Scanning (Recommended)

```bash
sudo ./bt_interceptor -S
```

1. Scan shows available devices
2. Select target device from list
3. Auto-pairing happens automatically
4. No manual intervention needed

### With Manual MAC Address

```bash
sudo ./bt_interceptor -s AA:BB:CC:DD:EE:FF -t 11:22:33:44:55:66
```

Even when providing MAC addresses manually, pairing is still automatic.

## Requirements

### Software Requirements
- `bluetoothctl` (part of BlueZ package)
- `bash` shell
- `timeout` command (coreutils)
- BlueZ Bluetooth stack running

### Hardware Requirements
- Bluetooth adapter (built-in or USB)
- Target device in pairing mode
- Target device within Bluetooth range (~10 meters)

### Permission Requirements
- Must run with `sudo` or as root
- Access to `/var/lib/bluetooth/` for link key extraction

## Error Handling

### Automatic Fallback

If auto-pairing fails, the interceptor provides helpful guidance:

```
[ERROR] Auto-pairing failed
[INFO] You can try manual pairing:
[INFO]   sudo bluetoothctl
[INFO]   scan on
[INFO]   pair 11:22:33:44:55:66
[INFO]   trust 11:22:33:44:55:66
[INFO]   connect 11:22:33:44:55:66
[INFO]   quit
[INFO] Press Enter to continue after manual pairing...
```

### Common Failure Scenarios

| Scenario | Cause | Solution |
|----------|-------|----------|
| "Device not found" | Target not in range or not discoverable | Put device in pairing mode, move closer |
| "Pairing failed" | Device rejected pairing | Check device settings, try manual pairing |
| "Permission denied" | Not running as root | Run with `sudo` |
| "bluetoothctl not found" | BlueZ not installed | `sudo apt-get install bluez` |
| "Link key not found" | Pairing didn't persist | Wait longer, check `/var/lib/bluetooth/` |

## Advantages Over Manual Pairing

| Aspect | Manual Pairing | Auto-Pairing |
|--------|---------------|--------------|
| **Time** | 2-3 minutes | 30-45 seconds |
| **Steps** | 8-10 manual steps | 1 automatic step |
| **Error Rate** | High (typos, forgotten commands) | Low (automated) |
| **User Experience** | Poor (terminal switching) | Good (hands-off) |
| **Repeatability** | Inconsistent | Consistent |
| **Documentation** | Complex instructions needed | Self-explanatory |

## Debugging Auto-Pairing

### Enable Verbose Output

The interceptor already provides detailed logging:

```
[INFO] Attempting to pair with device: 11:22:33:44:55:66
[INFO] Powering on Bluetooth adapter...
[INFO] Scanning for device...
[INFO] Pairing with device...
[INFO] Pairing successful
[INFO] Trusting device...
[INFO] Connecting to device...
[INFO] Connection successful
[INFO] Auto-pairing completed
```

### Manual Verification

Check if pairing succeeded:
```bash
# Check paired devices
bluetoothctl devices Paired

# Check device info
bluetoothctl info 11:22:33:44:55:66

# Check link key file
sudo ls -la /var/lib/bluetooth/<adapter_mac>/<device_mac>/info
```

### BlueZ Logs

Check system logs for detailed BlueZ activity:
```bash
sudo journalctl -u bluetooth -f
```

## Integration with Scanning

Auto-pairing works seamlessly with device scanning:

```bash
sudo ./bt_interceptor -S
```

**Complete Flow:**
1. **Scan** → Shows all nearby devices with names and MAC addresses
2. **Select** → Choose target device from numbered list
3. **Pair** → Automatically pairs with selected device
4. **Extract** → Extracts link key from BlueZ storage
5. **Relay** → Sets up MITM relay and begins interception

## Security Considerations

### Pairing Security

- Auto-pairing uses standard Bluetooth pairing protocols
- No security mechanisms are bypassed during pairing itself
- The MITM attack occurs **after** legitimate pairing
- Link key extraction happens from local BlueZ storage

### Attack Surface

Auto-pairing does **not** introduce new attack vectors:
- Still requires physical proximity for pairing
- Target device must accept pairing request
- Uses same BlueZ mechanisms as manual pairing
- No remote exploitation possible

## Future Enhancements

Possible improvements:

- [ ] PIN code automation for legacy devices
- [ ] Support for Bluetooth LE pairing
- [ ] Parallel pairing attempts for multiple devices
- [ ] Pairing retry with exponential backoff
- [ ] GUI progress indicator for pairing status
- [ ] D-Bus API integration (bypass bluetoothctl)
- [ ] Automatic device class detection
- [ ] Smart pairing mode selection based on device type

## Troubleshooting Guide

### Problem: Auto-pairing times out

**Symptoms:**
```
[INFO] Pairing with device...
[ERROR] Pairing failed
```

**Solutions:**
1. Ensure target device is in pairing mode (usually hold button 5+ seconds)
2. Check target device isn't already paired to another device
3. Move devices closer together (< 5 meters)
4. Disable other Bluetooth devices that might interfere
5. Try manual pairing to verify device is functional

### Problem: "Already paired" but link key not found

**Symptoms:**
```
[INFO] Device already paired
[ERROR] Failed to extract link key
```

**Solutions:**
1. Remove pairing and try again: `sudo bluetoothctl remove <MAC>`
2. Check BlueZ directory permissions: `ls -la /var/lib/bluetooth/`
3. Restart BlueZ service: `sudo systemctl restart bluetooth`
4. Check if BlueZ is storing pairing info: `sudo ls /var/lib/bluetooth/*/`

### Problem: bluetoothctl commands hang

**Symptoms:**
- Auto-pairing never completes
- Process seems frozen

**Solutions:**
1. Kill hanging processes: `sudo pkill -9 bluetoothctl`
2. Restart Bluetooth: `sudo systemctl restart bluetooth`
3. Check for adapter hardware issues: `hciconfig -a`
4. Try different Bluetooth adapter if using USB dongle

## Conclusion

Automatic pairing significantly improves the user experience of the interceptor by:
- Eliminating manual terminal commands
- Reducing setup time from minutes to seconds
- Minimizing user errors
- Providing clear feedback during the process
- Offering fallback to manual pairing when needed

Combined with device scanning, the interceptor now offers a streamlined, professional-grade MITM attack tool for educational and authorized security research purposes.
