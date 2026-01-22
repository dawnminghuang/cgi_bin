#ifndef PUBLIC_H
#define PUBLIC_H

// 通用重启线程函数
typedef enum {
    REBOOT_TYPE_SYSTEM,
    REBOOT_TYPE_NETWORK
} reboot_type_t;

typedef struct {
    unsigned int total_space;    // 总空间 (KB)
    unsigned int used_space;     // 已使用空间 (KB)
    unsigned int available_space; // 可用空间 (KB)
    double usage_percentage;  // 使用百分比
} disk_stats_t;

#define PASSWORD_FILE "/home/password.txt"
#define SERVER_PORT 8888
#define TCP_SERVER_PORT 8081
#define LOCAL_HOST "127.0.0.1"

void* reboot_thread(void* arg);

int create_reboot_thread(reboot_type_t type);

int call_sys(const char *cmd, char *buf, size_t bufsize);

void get_current_time(char *buffer, size_t buffer_size);

int tcp_client_send(char* data);

int get_disk_stats(disk_stats_t* stats);

int base64_decode(const char *data, unsigned char *out, int out_len);

int start_tcp_server_thread();

#endif // PUBLIC_H