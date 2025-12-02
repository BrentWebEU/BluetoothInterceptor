#ifndef CONFIG_H
#define CONFIG_H

#define TCP_SERVER_PORT 8888
#define BLUETOOTH_INFO_PATH "/var/lib/bluetooth"
#define MAX_BUFFER_SIZE 4096
#define BACKLOG 5

#define DEBUG 1

#if DEBUG
#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) 
#endif

#define ERROR_PRINT(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define WARN_PRINT(fmt, ...) fprintf(stdout, "[WARN] " fmt "\n", ##__VA_ARGS__)
#define INFO_PRINT(fmt, ...) fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)

#endif
