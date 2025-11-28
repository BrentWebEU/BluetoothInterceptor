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
