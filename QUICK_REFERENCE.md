# Quick Reference Card

## 🚀 Fastest Way to Start

```bash
sudo ./bt_interceptor -S
```

**That's it!** Everything else is automatic:
- Scans for devices
- Shows connection status  
- Breaks existing connections
- Pairs automatically
- Sets up MITM

## Command Reference

### Build Interceptor
```bash
cd interceptor
make
sudo make install  # Optional: system-wide install
```

### Interactive Mode (Recommended)
```bash
sudo ./bt_interceptor -S
```

### Manual Mode (If you know MAC addresses)
```bash
sudo ./bt_interceptor -s <SOURCE_MAC> -t <TARGET_MAC>
```

Example:
```bash
sudo ./bt_interceptor -s AA:BB:CC:DD:EE:FF -t 11:22:33:44:55:66 -P 8888
```

### Manual Pairing (Only if auto-pairing fails)
```bash
sudo bluetoothctl
scan on
pair <DEVICE_MAC>
trust <DEVICE_MAC>
connect <DEVICE_MAC>
exit
```

### Check Network
```bash
hostname -I              # Get Pi IP address
sudo netstat -tlnp       # Check listening ports
sudo ufw allow 8888/tcp  # Open firewall port
```

### Bluetooth Tools
```bash
hciconfig -a             # List Bluetooth adapters
hcitool scan             # Scan for nearby devices
hciconfig hci0 down      # Disable adapter
hciconfig hci0 up        # Enable adapter
```

## Mobile App Commands

### Install Dependencies
```bash
cd app
npm install
```

### Run on Android
```bash
npx react-native run-android
```

### Run on iOS
```bash
cd ios && pod install && cd ..
npx react-native run-ios
```

### Development
```bash
npm start                # Start Metro bundler
npm run lint            # Run ESLint
npm test                # Run tests
```

## Connection Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| IP Address | - | Raspberry Pi IP (e.g., 192.168.1.100) |
| TCP Port | 8888 | Streaming server port |
| PSM | 25 | L2CAP PSM (A2DP audio) |

## File Locations

### Raspberry Pi
- **Link Keys:** `/var/lib/bluetooth/<ADAPTER_MAC>/<DEVICE_MAC>/info`
- **BlueZ Config:** `/etc/bluetooth/main.conf`
- **Logs:** `stdout/stderr` (run with `2>&1 | tee log.txt`)

### Mobile App
- **Source Code:** `app/src/`
- **Android Native:** `app/android/app/src/main/java/`
- **Config:** `app/package.json`

## Common Issues

| Problem | Quick Fix |
|---------|-----------|
| Permission denied | Use `sudo` |
| Port already in use | `sudo killall bt_interceptor` |
| Can't find adapter | `sudo hciconfig -a` |
| Link key missing | Re-pair device with bluetoothctl |
| App won't connect | Check IP, port, firewall |
| No audio | Verify source playing audio |

## Debugging

### Enable Debug Output
In `interceptor/config.h`:
```c
#define DEBUG 1  // Enable debug messages
```

### View Logs
```bash
sudo ./bt_interceptor -s ... -t ... 2>&1 | tee debug.log
```

### Check Connections
```bash
# On Raspberry Pi
sudo netstat -an | grep 8888
sudo hcitool con

# On Mobile Device
Check app statistics display
```

## Safety Checklist

- [ ] Own all devices being tested
- [ ] Have written authorization
- [ ] Testing in isolated environment
- [ ] Not in public space
- [ ] Compliant with local laws
- [ ] Devices will be reset after testing

## Project Structure

```
Bluetooth/
├── interceptor/          # C application for Pi
│   ├── main.c           # Main program
│   ├── bt_utils.c/h     # Bluetooth functions
│   ├── tcp_server.c/h   # TCP streaming
│   └── crypto.c/h       # Encryption (stub)
│
└── app/                  # React Native app
    ├── src/
    │   ├── components/  # UI components
    │   ├── services/    # Business logic
    │   └── screens/     # App screens
    └── android/         # Native modules
```

## Resources

- **Bluetooth Spec:** https://www.bluetooth.com/specifications/
- **BlueZ Docs:** http://www.bluez.org/documentation/
- **React Native:** https://reactnative.dev/docs/
- **TCP Sockets:** https://github.com/Rapsssito/react-native-tcp-socket

## Support

- Check README.md for detailed documentation
- See INTEGRATION_GUIDE.md for setup walkthrough
- Review component READMEs in subdirectories

---

**Remember:** Educational use only with proper authorization!
