#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include "cJSON.h"
#include "api.h"
#include "sql.h"
#include "log.h"
#include "public.h"

#define PORT 8080
#define BUFFER_SIZE (1024 * 1024)

void handle_http_post_request(char* http_request, int socket) {
    // 解析请求行获取路径
    char method[16] = {0};
    char path[256] = {0};
    char version[16] = {0};
    int content_length = 0;
    
    // 解析第一行获取方法、路径和版本
    sscanf(http_request, "%15s %255s %15s", method, path, version);
    
    // 从路径中提取操作名（去掉开头的/）
    char* operation = path;
    if (operation[0] == '/') {
        operation++; // 跳过开头的 '/'
    }

    // 查找Content-Length头部
    char* content_length_header = strstr(http_request, "content-length:");
    if (!content_length_header) {
        content_length_header = strstr(http_request, "Content-Length:");
    }

    if (content_length_header) {
        if (content_length_header == strstr(http_request, "content-length:")) {
            sscanf(content_length_header, "content-length: %d", &content_length);
        } else {
            sscanf(content_length_header, "Content-Length: %d", &content_length);
        }
    }
    else {
        LOG_ERROR("no content-length");
        return;
    }

    LOG_DEBUG("content_length: %d", content_length);
    // 查找HTTP头部和正文的分隔符
    char* body_separator = strstr(http_request, "\r\n\r\n");
    if (body_separator) {
        body_separator += 4; // 跳过 \r\n\r\n
        
        // 计算已经接收到的body长度
        int received_body_length = strlen(body_separator);
        
        // 如果声明的content-length大于已接收的数据，则需要继续接收
        if (content_length > received_body_length) {
            // 分配足够的内存来存储完整数据
            int headers_length = body_separator - http_request;
            int total_length = headers_length + content_length + 1; // +1 for null terminator
            char* full_request = (char*)malloc(total_length);
            if (full_request == NULL) {
                LOG_ERROR("Failed to allocate memory for full request");
                json_error_reply(socket);
                return;
            }
            
            // 复制已接收的部分
            strcpy(full_request, http_request);
            
            // 计算body应该开始的位置
            char* full_body_separator = strstr(full_request, "\r\n\r\n") + 4;
            char* body_position = full_body_separator + received_body_length;
            
            // 继续接收剩余数据
            int remaining_length = content_length - received_body_length;
            int bytes_received = 0;
            time_t start_time = time(NULL);  // 记录开始时间
            const int TIMEOUT_SECONDS = 5;  // 设置5秒超时
            
            while (bytes_received < remaining_length) {
                // 检查是否超时
                if (difftime(time(NULL), start_time) > TIMEOUT_SECONDS) {
                    LOG_ERROR("Timeout while receiving HTTP body data");
                    free(full_request);
                    json_error_reply(socket);
                    return;
                }
                
                int valread = read(socket, body_position + bytes_received, remaining_length - bytes_received);
                if (valread <= 0) {
                    LOG_ERROR("Failed to read remaining data");
                    free(full_request);
                    json_error_reply(socket);
                    return;
                }
                bytes_received += valread;
            }
            
            // 确保字符串结束
            full_request[headers_length + content_length] = '\0';   
            LOG_DEBUG("full_request: %s", full_request);

            // 重新定位body_separator
            body_separator = strstr(full_request, "\r\n\r\n") + 4;
            
            // 使用完整数据处理请求
            if (strlen(operation) > 0 && content_length > 0) {
                message_handle_data(operation, body_separator, socket);
            } else if (strlen(operation) > 0 && content_length == 0) {
                message_handle_data(operation, NULL, socket);
            } else if (content_length > 0) {
                message_handle_data(NULL, body_separator, socket);
            } else {
                json_error_reply(socket);
            }
            
            free(full_request);
        } else {
            LOG_DEBUG("http_request: %s", http_request);
            // 数据已经完整，直接处理
            if (strlen(operation) > 0 && strlen(body_separator) > 0) {
                message_handle_data(operation, body_separator, socket);
            } else if (strlen(operation) > 0 && strlen(body_separator) == 0) {
                message_handle_data(operation, NULL, socket);
            } else if (strlen(body_separator) > 0) {
                message_handle_data(NULL, body_separator, socket);
            } else {
                json_error_reply(socket);
            }
        }
    } else {
        printf("no body");
        if (strlen(operation) > 0) {
            message_handle_data(operation, NULL, socket);
        }
    }
}

void handle_http_get_request(char* http_request, int socket) {
    // 解析请求行获取路径
    char method[16] = {0};
    char path[256] = {0};
    char version[16] = {0};
    
    // 解析第一行获取方法、路径和版本
    sscanf(http_request, "%15s %255s %15s", method, path, version);
    
    // 从路径中提取操作名（去掉开头的/）
    char* operation = path;
    if (operation[0] == '/') {
        operation++; // 跳过开头的 '/'
    }
            
    if(strlen(operation) > 0)
        message_handle_data(operation, NULL, socket);
    else
        json_error_reply(socket);
}

// 打印使用说明
void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [-l <log_level>] [-h]\n", program_name);
    fprintf(stderr, "  -l <log_level>  Set log level (debug, info, warn, error)\n");
    fprintf(stderr, "  -h            Show this help message\n");
}

int main(int argc, char *argv[]) {
    LogLevel log_level = LOGS_INFO;  // 默认日志等级为 INFO
    int opts;
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // 解析命令行参数
    while ((opts = getopt(argc, argv, "l:h")) != -1) {
        switch (opts) {
            case 'l':
                if (strcmp(optarg, "debug") == 0) {
                    log_level = LOGS_DEBUG;
                } else if (strcmp(optarg, "info") == 0) {
                    log_level = LOGS_INFO;
                } else if (strcmp(optarg, "warn") == 0) {
                    log_level = LOGS_WARN;
                } else if (strcmp(optarg, "error") == 0) {
                    log_level = LOGS_ERROR;
                } else {
                    fprintf(stderr, "Invalid log level: %s\n", optarg);
                    print_usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    printf("log level: %d\n",log_level);
    // 初始化日志文件，设置日志等级
    log_init(log_level);
    
    if (init_database() != 0) {
        fprintf(stderr, "Failed to initialize database\n");
        exit(EXIT_FAILURE);
    }

    // 创建socket文件描述符
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // 设置socket选项，允许地址重用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    // 配置服务器地址和端口
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // 绑定socket到指定地址和端口
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // 开始监听连接请求
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    // 接受连接并处理消息
    while (1) {
        // 接受新的连接
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }
        
        LOG_DEBUG("Connection accepted from %s:%d", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        
        // 持续监听这个连接
        int valread;
        while (1) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(new_socket, &readfds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;  // 1秒超时
            timeout.tv_usec = 0;
            
            int activity = select(new_socket + 1, &readfds, NULL, NULL, &timeout);
            
            if (activity > 0) {
                if (FD_ISSET(new_socket, &readfds)) {
                    valread = read(new_socket, buffer, BUFFER_SIZE - 1);
                    LOG_DEBUG("Received %d bytes: %s", valread, buffer);
                    if (valread > 0) {
                        buffer[valread] = '\0';  // 确保字符串结束
                        if (strstr(buffer, "POST") == buffer) {
                            handle_http_post_request(buffer, new_socket);
                        }
                        else if (strstr(buffer, "GET") == buffer) {
                            handle_http_get_request(buffer, new_socket);
                        }
                        memset(buffer, 0, BUFFER_SIZE);                 
                    } else if (valread == 0) {
                        LOG_DEBUG("Client disconnected");
                        break;
                    } else {
                        LOG_ERROR("read");
                        break;
                    }
                }
            } else if (activity == 0) {
                usleep(10000);
                break;
            } else {
                if (errno == EINTR) {
                    usleep(10000);
                    continue;
                }
                LOG_ERROR("select");
                break;
            }
        }
        
        // 关闭连接
        close(new_socket);
        LOG_DEBUG("Connection closed\n");
    }
    
    close(server_fd);
    close_database();
    return 0;
}