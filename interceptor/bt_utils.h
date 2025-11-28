#ifndef BT_UTILS_H
#define BT_UTILS_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/rfcomm.h>

typedef struct {
    char addr[18];
    char name[248];
    int rssi;
    int connected;      // 1 if device is currently connected to another device
} bt_device_t;

int bt_get_adapter_address(char *adapter_addr, size_t size);
int bt_extract_link_key(const char *adapter_mac, const char *device_mac, char *key_out, size_t key_size);
int bt_spoof_mac_address(int adapter_id, const char *target_mac);
int bt_create_l2cap_socket(void);
int bt_connect_l2cap(int sock, const char *dest_addr, uint16_t psm);
int bt_bind_l2cap(int sock, const char *src_addr, uint16_t psm);
int bt_listen_l2cap(int sock, int backlog);
int bt_accept_l2cap(int server_sock, char *client_addr, size_t addr_size);
int bt_scan_devices(bt_device_t *devices, int max_devices, int scan_duration);
int bt_auto_pair_device(const char *device_mac);
int bt_disconnect_device(const char *device_mac);
int bt_force_disconnect(const char *source_mac, const char *target_mac);
int bt_check_connection_status(const char *device_mac);

#endif
