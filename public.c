#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h> 
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "public.h"
#include "log.h"
#include "cJSON.h"

// Base64编码表
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

void* reboot_thread(void* arg) {
    reboot_type_t* reboot_type = (reboot_type_t*)arg;
    
    // 等待一段时间确保主程序已返回响应
    sleep(3);
    
    // 根据类型执行不同的重启操作
    switch (*reboot_type) {
        case REBOOT_TYPE_SYSTEM:
            LOG_INFO("Rebooting system...");
            system("reboot");
            break;
        case REBOOT_TYPE_NETWORK:
            LOG_INFO("Restarting network...");
            system("/etc/init.d/S40network restart");
            break;
        default:
            LOG_INFO("Unknown reboot type");
            break;
    }
    
    // 释放参数内存
    free(reboot_type);
    
    // 线程执行完毕后自动退出
    return NULL;
}

// 封装创建重启线程的函数
int create_reboot_thread(reboot_type_t type) {
    pthread_t reboot_thread_id;
    reboot_type_t* reboot_type = malloc(sizeof(reboot_type_t));
    
    if (reboot_type == NULL) {
        LOG_ERROR("Failed to allocate memory for reboot type");
        return -1;
    }
    
    *reboot_type = type;
    
    // 创建线程来执行重启操作
    if (pthread_create(&reboot_thread_id, NULL, reboot_thread, reboot_type) != 0) {
        LOG_ERROR("Failed to create reboot thread");
        free(reboot_type);
        return -1;
    } else {
        // 分离线程，使其在完成后自动释放资源
        pthread_detach(reboot_thread_id);
        return 0;
    }
}

int call_sys(const char *cmd, char *buf, size_t bufsize) {
    FILE *fp;
    char line[1024];
    size_t total_len = 0; // 记录总长度

    // 使用 popen 执行 shell 指令
    fp = popen(cmd, "r");
    if (fp == NULL) {
        LOG_ERROR("popen failed");
        return 1;
    }

    // 逐行读取输出并存储在 buf 中
    buf[0] = '\0'; // 初始化缓冲区
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        // 检查是否有足够空间
        if (total_len + len + 1 < bufsize - 1) { // +1 用于换行符或终止符
            if (total_len == 0) {
                strcpy(buf, line);
            } else {
                strcat(buf, "\n");  // 添加换行符分隔多行
                strcat(buf, line);
            }
            total_len += len + 1;
        } else {
            break;
        }
    }

    // 关闭管道
    pclose(fp);

    return 0;
}

void get_current_time(char *buffer, size_t buffer_size) {
    time_t rawtime;
    struct tm *timeinfo;
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M.%S", timeinfo);
}

int tcp_client_send(char* data) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    int addrlen = sizeof(serv_addr);
    int result = -1;
    int data_len = strlen(data);
    fd_set fdset;
    struct timeval tv;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LOG_ERROR("Socket creation error");
        return -1;
    }
    
    // 设置socket为非阻塞模式
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    
    if(inet_pton(AF_INET, LOCAL_HOST, &serv_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid address/ Address not supported");
        goto cleanup;
    }
    
    // 尝试连接
    int connect_result = connect(sock, (struct sockaddr *)&serv_addr, addrlen);
    if (connect_result < 0) {
        if (errno != EINPROGRESS) {
            LOG_ERROR("Connection failed immediately");
            goto cleanup;
        }
        
        // 使用select等待连接完成，设置3秒超时
        FD_ZERO(&fdset);
        FD_SET(sock, &fdset);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        int select_result = select(sock + 1, NULL, &fdset, NULL, &tv);
        if (select_result <= 0) {
            if (select_result == 0) {
                LOG_ERROR("Connection timeout (3s)");
            } else {
                LOG_ERROR("Connection error");
            }
            goto cleanup;
        }
        
        // 检查连接是否成功
        int optval;
        socklen_t optlen = sizeof(optval);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
            LOG_ERROR("getsockopt failed");
            goto cleanup;
        }
        
        if (optval != 0) {
            LOG_ERROR("Connection failed with error: %d", optval);
            goto cleanup;
        }
    }
    
    // 恢复socket为阻塞模式
    fcntl(sock, F_SETFL, flags);
    
    int bytes_sent = send(sock, data, data_len, 0);
    if (bytes_sent != data_len) {
        LOG_ERROR("Failed to send all data. Sent: %d, Expected: %d", bytes_sent, data_len);
        goto cleanup;
    }
    
    LOG_DEBUG("Successfully send %s to %s:%d", data, LOCAL_HOST, SERVER_PORT);
    result = 0;
    
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}


/**
 * 调用df命令获取磁盘信息并计算总空间和已使用空间
 * @param stats 存储磁盘统计信息的结构体指针
 * @return 0 成功, -1 失败
 */
int get_disk_stats(disk_stats_t* stats) {
    FILE *fp;
    char line[512];
    unsigned int total_blocks = 0;
    unsigned int total_used = 0;
    unsigned int total_available = 0;
    
    // 初始化统计数据
    if (stats == NULL) {
        return -1;
    }
    
    memset(stats, 0, sizeof(disk_stats_t));
    
    fp = popen("df", "r");
    if (fp == NULL) {
        perror("popen failed");
        return -1;
    }
    
    // 跳过标题行
    if (fgets(line, sizeof(line), fp) == NULL) {
        pclose(fp);
        return -1;
    }
    
    // 逐行读取并解析数据
    while (fgets(line, sizeof(line), fp) != NULL) {
        // 解析每一行的数据
        char filesystem[256];
        unsigned int blocks, used, available;
        int use_percent;
        char mounted_on[256];
        char use_percent_str[8];
        
        int parsed = sscanf(line, "%255s %u %u %u %7s %255s",
                           filesystem, 
                           &blocks,
                           &used,
                           &available,
                           use_percent_str,
                           mounted_on);
        
        if (parsed >= 6) {              
            // 累加所有文件系统的空间
            total_blocks += blocks;
            total_used += used;
            total_available += available;
        }
    }
    
    // 关闭管道
    pclose(fp);
    
    // 填充返回结构体
    stats->total_space = total_blocks;
    stats->used_space = total_used;
    stats->available_space = total_available;
    
    // 计算使用百分比
    if (total_blocks > 0) {
        stats->usage_percentage = ((double)total_used / (double)total_blocks) * 100.0;
    } else {
        stats->usage_percentage = 0.0;
    }
    
    return 0;
}

// Base64解码函数
int base64_decode(const char *data, unsigned char *out, int out_len) {
    static const unsigned char base64_table[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 0, 0, 0, 0,
        0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0, 0, 0, 0,
        0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    int len = strlen(data);
    int pad = 0;
    int i, j, k = 0;
    unsigned char buf[4];
    
    // 计算填充字符数
    if (len > 0 && data[len-1] == '=') pad++;
    if (len > 1 && data[len-2] == '=') pad++;
    
    // 计算预期输出长度
    int expected_len = ((len - pad) * 3) / 4;
    if (out_len < expected_len) {
        return -1; // 输出缓冲区太小
    }
    
    for (i = 0; i < len; i += 4) {
        // 清空缓冲区
        memset(buf, 0, 4);
        
        // 读取4个字符
        for (j = 0; j < 4 && i + j < len; j++) {
            unsigned char c = data[i + j];
            if (c == '=') {
                buf[j] = 0;
                break;
            }
            buf[j] = base64_table[c];
        }
        
        // 解码
        if (k < out_len) out[k++] = (buf[0] << 2) | (buf[1] >> 4);
        if (j > 2 && k < out_len) out[k++] = (buf[1] << 4) | (buf[2] >> 2);
        if (j > 3 && k < out_len) out[k++] = (buf[2] << 6) | buf[3];
    }
    
    return k; // 返回实际写入的字节数
}

// Base64编码函数
static char* base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    *output_length = 4 * ((input_length + 2) / 3);
    
    char *encoded_data = malloc(*output_length + 1);
    if (encoded_data == NULL) return NULL;
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded_data[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }
    
    // 添加填充字符
    for (size_t i = 0; i < (3 - (input_length % 3)) % 3; i++) {
        encoded_data[*output_length - 1 - i] = '=';
    }
    
    encoded_data[*output_length] = '\0';
    return encoded_data;
}