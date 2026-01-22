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

#include "cJSON.h"
#include "api.h"
#include "sql.h"
#include "log.h"
#include "public.h"

typedef struct {
    const char *operation;
    int (*func)(cJSON *root, int socket_fd);
} OperationHandler;

int send_http_response(int socket_fd, char* data) {
    char *http_response = NULL;
    int result = 1;
    
    // 检查输入参数
    if (data == NULL) {
        LOG_ERROR("Invalid data parameter");
        return 1;
    }
    
    int data_len = strlen(data);
    
    const int HTTP_HEADER_LEN = 128; // 固定头部长度，预留足够空间
    
    // 分配内存存储完整的HTTP响应
    http_response = malloc(HTTP_HEADER_LEN + data_len + 1);
    if (http_response == NULL) {
        LOG_ERROR("Failed to allocate memory for HTTP response");
        return 1;
    }
    
    // 构造完整的HTTP响应
    int header_len = sprintf(http_response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", data_len);
    
    // 连接实际数据
    strcpy(http_response + header_len, data);

    // 发送HTTP响应
    int bytes_sent = send(socket_fd, http_response, strlen(http_response), 0);
    if (bytes_sent < 0) {
        perror("send failed");
        LOG_ERROR("Failed to send HTTP response");
    } else {
        LOG_DEBUG("Sent %d bytes HTTP response to client", bytes_sent);
        result = 0; // 成功
    }
    
    // 释放内存
    if (http_response) {
        free(http_response);
    }
    
    return result;
}

// JSON解析错误回复函数
int json_error_reply(int socket_fd) {
    cJSON *root = NULL;
    char *json_string = NULL;
    int result = 1;

    // 创建 JSON 对象
    root = cJSON_CreateObject();
    if (root == NULL) {
        return result;
    }

    // 添加错误信息到 JSON 对象
    cJSON_AddStringToObject(root, "Event", "Error");
    cJSON_AddStringToObject(root, "message", "Invalid format");
    cJSON_AddNumberToObject(root, "status", 1);

    // 将 JSON 对象转换为字符串
    json_string = cJSON_Print(root);
    if (json_string == NULL) {
        cJSON_Delete(root);
        return result;
    }

    result = send_http_response(socket_fd, json_string);
    
    if (root) 
        cJSON_Delete(root);
    if(json_string) {
        free(json_string);
    }

    return result;
}

int general_reply(char *Event, int status, int socket_fd, char *message) {
    cJSON *root = NULL;
    char *json_string = NULL;
    int result = 1;

    // 创建 JSON 对象
    root = cJSON_CreateObject();
    if (root == NULL) {
        goto end;
    }

    // 添加键值对到 JSON 对象
    cJSON_AddStringToObject(root, "Event", Event);
    cJSON_AddNumberToObject(root, "status", status);
    if(status == 0) {
        cJSON_AddStringToObject(root, "Message", "Success");
    }
    else {
        cJSON_AddStringToObject(root, "Message", message);
    }

    // 将 JSON 对象转换为字符串
    json_string = cJSON_Print(root);
    if (json_string == NULL) {
        goto end;
    }

    result = send_http_response(socket_fd, json_string);
    
end:
    if (root) cJSON_Delete(root);
    if(json_string) {
        free(json_string);
    }

    return result;
}

int Login_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    char cmd[256] = {0};
    char Username[64] = {0};
    char Password[64] = {0};
    char Passwoed_md5[64] = {0};
    char stored_password[64] = {0};
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Login", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }

    item = cJSON_GetObjectItem(root, "Username");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Username");
        general_reply("Login", 1, socket_fd, "Failed to get Username"); // 发送错误响应
        return 1;
    } else {
        strncpy(Username, item->valuestring, sizeof(Username) - 1);
    }

    item = cJSON_GetObjectItem(root, "Password");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Password");
        general_reply("Login", 1, socket_fd, "Failed to get Password"); // 发送错误响应
        return 1;
    } else {
        strncpy(Password, item->valuestring, sizeof(Password) - 1);
    }

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "echo -n \"%s\" | md5sum | cut -d' ' -f1", Password);
    call_sys(cmd, Passwoed_md5, sizeof(Passwoed_md5));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "cat %s", PASSWORD_FILE);
    call_sys(cmd, stored_password, sizeof(stored_password));
    
    if (strcmp(Username, "admin") == 0 && strcmp(Passwoed_md5, stored_password) == 0) { 
        result = general_reply("Login", 0, socket_fd, NULL);
    }
    else {
        result = general_reply("Login", 1, socket_fd, "Incorrect Password");
    }

    return result;
}

int Info_handler(cJSON *root, int socket_fd) {
    cJSON *return_json = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    char cmd[256];
    char buf[256] = {0};

    disk_stats_t disk_stats;
    char mem_total[20] = {0};
    char mem_used[20] = {0};
    char cpu_usage[20] = {0};
    char current_time[20] = {0};
    char temp[20] = {0};
    int result = 1;
    char *json_string = NULL;
    int bytes_sent;

    if (get_disk_stats(&disk_stats) != 0) {
        LOG_ERROR("获取磁盘统计信息失败");
        goto end;
    }

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "free | grep Mem | awk '{print $2}'");
    call_sys(cmd, mem_total, sizeof(mem_total));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "free | grep Mem | awk '{print $3}'");
    call_sys(cmd, mem_used, sizeof(mem_used));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "awk '{u=$2+$4; t=$2+$4+$5; if (NR==1){u1=u; t1=t;} else print ($2+$4-u1) * 100 / (t-t1)}' <(grep 'cpu ' /proc/stat) <(sleep 0.1; grep 'cpu ' /proc/stat)");
    call_sys(cmd, cpu_usage, sizeof(cpu_usage));

    get_current_time(current_time, sizeof(current_time));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "cat /sys/class/thermal/thermal_zone0/temp");
    call_sys(cmd, buf, sizeof(buf));
    snprintf(temp, sizeof(temp), "%d C", atoi(buf) / 1000);

    char disk_total_str[32];
    char disk_available_str[32];
    char disk_used_str[32];
    snprintf(disk_total_str, sizeof(disk_total_str), "%d", disk_stats.total_space);
    snprintf(disk_available_str, sizeof(disk_available_str), "%d", disk_stats.available_space);
    snprintf(disk_used_str, sizeof(disk_used_str), "%d", disk_stats.used_space);

    // 添加字段到data对象
    cJSON_AddStringToObject(data, "Id", "");
    cJSON_AddStringToObject(data, "Platform", "");
    cJSON_AddStringToObject(data, "Temp", temp);
    cJSON_AddStringToObject(data, "DiskTotal", disk_total_str);
    cJSON_AddStringToObject(data, "DiskAvailable", disk_available_str);
    cJSON_AddStringToObject(data, "DiskUsed", disk_used_str);
    cJSON_AddStringToObject(data, "MemTotal", mem_total);
    cJSON_AddStringToObject(data, "MemUsage", mem_used);
    cJSON_AddStringToObject(data, "Cpu", cpu_usage);
    cJSON_AddStringToObject(data, "Version", "0.0.53");
    cJSON_AddStringToObject(data, "Time", current_time);

    // 添加字段到返回的JSON对象
    cJSON_AddStringToObject(return_json, "Event", "Info");
    cJSON_AddItemToObject(return_json, "data", data);
    cJSON_AddNumberToObject(return_json, "Status", 0);
    cJSON_AddStringToObject(return_json, "Message", "Success");

    // 转换为字符串并发送
    json_string = cJSON_Print(return_json);
    if (json_string == NULL) {
        goto end;
    }

    result = send_http_response(socket_fd, json_string);

end:
    if (return_json) 
        cJSON_Delete(return_json);
    if (json_string)
        free(json_string);

    return result;
}

int Network_Query_handler(cJSON *root, int socket_fd) {
    cJSON *return_json = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    char cmd[256];
    char buf[256] = {0};
    char Address1[32] = {0}, Address2[32] = {0};
    char Dns1[32] = {0}, Dns2[32] = {0}, Dns3[32] = {0}, Dns4[32] = {0};
    char Gateway1[32] = {0}, Gateway2[32] = {0};
    char Mask1[32] = {0}, Mask2[32] = {0};
    char Mac1[32] = {0}, Mac2[32] = {0};
    int DhcpEnable_eth0 = 0, DhcpEnable_eth1 = 0; // 0表示静态，1表示动态
    cJSON *adapter_array = NULL;
    cJSON *adapter_obj1 = NULL;
    cJSON *adapter_obj2 = NULL;
    cJSON *dns_array1 = NULL;
    cJSON *dns_array2 = NULL;
    char *line;
    int dns_count = 0;
    int result = 1;
    
    // 判断eth1:1是否为dhcp配置
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "grep 'iface eth1:1' /etc/network/interfaces | grep 'inet dhcp'");
    if (call_sys(cmd, buf, sizeof(buf)) == 0 && strlen(buf) > 0) {
        DhcpEnable_eth0 = 1; // DHCP配置
    } else {
        DhcpEnable_eth0 = 0; // 静态配置
    }
    
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ifconfig eth1:1 | grep 'inet ' | awk '{print $2}' | awk -F: '{print $2}'");
    call_sys(cmd, Address1, sizeof(Address1));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ip route show dev eth1:1 | grep default | awk '{print $3}'");
    call_sys(cmd, Gateway1, sizeof(Gateway1));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ifconfig eth1:1 | grep 'inet ' | awk '{print $4}' | cut -d: -f2");
    call_sys(cmd, Mask1, sizeof(Mask1));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "cat /sys/class/net/eth1/address");
    call_sys(cmd, Mac1, sizeof(Mac1));

    // 获取eth0的DNS服务器地址
    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    snprintf(cmd, sizeof(cmd), "cat /etc/resolv.conf | grep nameserver | grep eth1 | awk '{print $2}'");
    call_sys(cmd, buf, sizeof(buf));

    // 解析eth0的DNS服务器地址
    line = strtok(buf, "\n");
    while (line != NULL && dns_count < 2) {
        switch (dns_count) {
            case 0:
                strncpy(Dns1, line, sizeof(Dns1) - 1);
                break;
            case 1:
                strncpy(Dns2, line, sizeof(Dns2) - 1);
                break;
        }
        dns_count++;
        line = strtok(NULL, "\n");
    }

    // 重置dns_count
    dns_count = 0;

    // 判断eth1是否为dhcp配置
    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    snprintf(cmd, sizeof(cmd), "grep 'iface eth1' /etc/network/interfaces | grep 'inet dhcp'");
    if (call_sys(cmd, buf, sizeof(buf)) == 0 && strlen(buf) > 0) {
        DhcpEnable_eth1 = 1; // DHCP配置
    } else {
        DhcpEnable_eth1 = 0; // 静态配置
    }
    
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ifconfig eth1 | grep 'inet ' | awk '{print $2}' | awk -F: '{print $2}'");
    call_sys(cmd, Address2, sizeof(Address2));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ip route show dev eth1 | grep default | awk '{print $3}'");
    call_sys(cmd, Gateway2, sizeof(Gateway2));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ifconfig eth1 | grep 'inet ' | awk '{print $4}' | cut -d: -f2");
    call_sys(cmd, Mask2, sizeof(Mask2));

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "cat /sys/class/net/eth1/address");
    call_sys(cmd, Mac2, sizeof(Mac2));

    // 获取eth1的DNS服务器地址
    memset(cmd, 0, sizeof(cmd));
    memset(buf, 0, sizeof(buf));
    snprintf(cmd, sizeof(cmd), "cat /etc/resolv.conf | grep nameserver | grep eth1 | awk '{print $2}'");
    call_sys(cmd, buf, sizeof(buf));

    // 解析eth1的DNS服务器地址
    line = strtok(buf, "\n");
    while (line != NULL && dns_count < 2) {
        switch (dns_count) {
            case 0:
                strncpy(Dns3, line, sizeof(Dns3) - 1);
                break;
            case 1:
                strncpy(Dns4, line, sizeof(Dns4) - 1);
                break;
        }
        dns_count++;
        line = strtok(NULL, "\n");
    }

    // 创建Adapter数组
    adapter_array = cJSON_CreateArray();
    
    // 创建eth0适配器对象
    adapter_obj1 = cJSON_CreateObject();
    cJSON_AddStringToObject(adapter_obj1, "Address", Address1);
    cJSON_AddStringToObject(adapter_obj1, "Device", "eth1:1");
    cJSON_AddBoolToObject(adapter_obj1, "DhcpEnable", DhcpEnable_eth0);
    
    // 创建eth0的DNS数组
    dns_array1 = cJSON_CreateArray();
    if (strlen(Dns1) > 0) {
        cJSON_AddItemToArray(dns_array1, cJSON_CreateString(Dns1));
    }
    if (strlen(Dns2) > 0) {
        cJSON_AddItemToArray(dns_array1, cJSON_CreateString(Dns2));
    }
    cJSON_AddItemToObject(adapter_obj1, "Dns", dns_array1);
    
    cJSON_AddStringToObject(adapter_obj1, "Gateway", Gateway1);
    cJSON_AddStringToObject(adapter_obj1, "Mac", Mac1);
    cJSON_AddStringToObject(adapter_obj1, "Mask", Mask1);
    
    cJSON_AddItemToArray(adapter_array, adapter_obj1);
    
    // 创建eth1适配器对象
    adapter_obj2 = cJSON_CreateObject();
    cJSON_AddStringToObject(adapter_obj2, "Address", Address2);
    cJSON_AddStringToObject(adapter_obj2, "Device", "eth1");
    cJSON_AddBoolToObject(adapter_obj2, "DhcpEnable", DhcpEnable_eth1);
    
    // 创建eth1的DNS数组
    dns_array2 = cJSON_CreateArray();
    if (strlen(Dns3) > 0) {
        cJSON_AddItemToArray(dns_array2, cJSON_CreateString(Dns3));
    }
    if (strlen(Dns4) > 0) {
        cJSON_AddItemToArray(dns_array2, cJSON_CreateString(Dns4));
    }
    cJSON_AddItemToObject(adapter_obj2, "Dns", dns_array2);
    
    cJSON_AddStringToObject(adapter_obj2, "Gateway", Gateway2);
    cJSON_AddStringToObject(adapter_obj2, "Mac", Mac2);
    cJSON_AddStringToObject(adapter_obj2, "Mask", Mask2);
    
    cJSON_AddItemToArray(adapter_array, adapter_obj2);
    
    // 添加到data对象
    cJSON_AddItemToObject(data, "Adapter", adapter_array);
    
    // 添加到返回的JSON对象
    cJSON_AddStringToObject(return_json, "Event", "Network_Query");
    cJSON_AddItemToObject(return_json, "data", data);
    cJSON_AddNumberToObject(return_json, "Status", 0);
    cJSON_AddStringToObject(return_json, "Message", "Success");
    
    // 发送响应
    char *json_string = cJSON_Print(return_json);
    if (json_string != NULL) {
        result = send_http_response(socket_fd, json_string);
        free(json_string);
    }
    
    // 清理内存
    cJSON_Delete(return_json);
    return result;
}

int Network_Set_handler(cJSON *root, int socket_fd) {
    cJSON *dns_array = NULL;
    cJSON *dns_item = NULL;
    char device[32] = {0};
    char address[32] = {0};
    char mask[32] = {0};
    char gateway[32] = {0};
    char mac[32] = {0};
    int dhcp_enable = 0;
    int dns_count = 0;
    int j;
    char dns_list[256] = {0};
    char cmd[512]; // 添加cmd变量声明
    FILE *fp;
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Network_Set", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }
    
    /*
    {"name":"","Device":"eth1","Address":"192.168.0.101","Mask":"255.255.255.0","Gateway":"192.168.0.1","Mac":"8e:02:62:2a:6c:99","DhcpEnable":false,"Dns":"114.114.114.114"}
    */
    
    // 获取设备名称
    cJSON *device_item = cJSON_GetObjectItem(root, "Device");
    if (device_item != NULL && cJSON_IsString(device_item)) {
        strncpy(device, device_item->valuestring, sizeof(device) - 1);
    }
    
    // 获取IP地址
    cJSON *address_item = cJSON_GetObjectItem(root, "Address");
    if (address_item != NULL && cJSON_IsString(address_item)) {
        strncpy(address, address_item->valuestring, sizeof(address) - 1);
    }
    
    // 获取子网掩码
    cJSON *mask_item = cJSON_GetObjectItem(root, "Mask");
    if (mask_item != NULL && cJSON_IsString(mask_item)) {
        strncpy(mask, mask_item->valuestring, sizeof(mask) - 1);
    }
    
    // 获取网关
    cJSON *gateway_item = cJSON_GetObjectItem(root, "Gateway");
    if (gateway_item != NULL && cJSON_IsString(gateway_item)) {
        strncpy(gateway, gateway_item->valuestring, sizeof(gateway) - 1);
    }
    
    // 获取MAC地址
    cJSON *mac_item = cJSON_GetObjectItem(root, "Mac");
    if (mac_item != NULL && cJSON_IsString(mac_item)) {
        strncpy(mac, mac_item->valuestring, sizeof(mac) - 1);
    }
    
    // 获取DHCP启用状态
    cJSON *dhcp_item = cJSON_GetObjectItem(root, "DhcpEnable");
    if (dhcp_item != NULL && cJSON_IsBool(dhcp_item)) {
        dhcp_enable = cJSON_IsTrue(dhcp_item) ? 1 : 0;
    }

    // 打印解析到的配置信息（用于调试）
    LOG_DEBUG("Device: %s", device);
    LOG_DEBUG("Address: %s", address);
    LOG_DEBUG("Mask: %s", mask);
    LOG_DEBUG("Gateway: %s", gateway);
    LOG_DEBUG("Mac: %s", mac);
    LOG_DEBUG("DhcpEnable: %d", dhcp_enable);

    // 先清空/etc/network/interfaces
    fp = fopen("/etc/network/interfaces", "w");
    if (fp == NULL) {
        LOG_ERROR("Failed to open /etc/network/interfaces for writing");
        general_reply("Network_Set", 1, socket_fd, "Failed to Write");
        return 1;
    }
     
    // 写入接口配置
    fprintf(fp, "auto lo\n");
    fprintf(fp, "iface lo inet loopback\n\n");
    fprintf(fp, "auto %s\n", device);
    if (dhcp_enable) {
        fprintf(fp, "iface %s inet dhcp\n", device);
    } else {
        fprintf(fp, "iface %s inet static\n", device);
        if (strlen(address) > 0) {
            fprintf(fp, "        address %s\n", address);
        }
        if (strlen(mask) > 0) {
            fprintf(fp, "        netmask %s\n", mask);
        }
        if (strlen(gateway) > 0) {
            fprintf(fp, "        gateway %s\n", gateway);
        }
    }
    fprintf(fp, "\n");
    fclose(fp);
    
    // 更新resolv.conf中的DNS配置
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "sed -i '/# %s$/d' /etc/resolv.conf", device);
    system(cmd);

    if (!dhcp_enable) {
        // 获取DNS数组
        dns_item = cJSON_GetObjectItem(root, "Dns");
        LOG_DEBUG("Dns: %s", dns_item->valuestring);
        if (dns_item != NULL && cJSON_IsString(dns_item)) {
            memset(cmd, 0, sizeof(cmd));
            snprintf(cmd, sizeof(cmd), "echo 'nameserver %s # %s' >> /etc/resolv.conf", dns_item->valuestring, device);
            LOG_DEBUG("cmd: %s", cmd);
            system(cmd);
        }
    }

    // 发送成功响应
    result = general_reply("Network_Set", 0, socket_fd, NULL);
    
    // 创建线程来重启网络服务
    if(result == 0)
        create_reboot_thread(REBOOT_TYPE_NETWORK);

    return result;
}

int Media_Fetch_handler(cJSON *root, int socket_fd) {
    cJSON *response = NULL;
    cJSON *data = NULL;
    cJSON *content_array = NULL;
    char *json_string = NULL;
    int result = 1;
    
    // 创建响应JSON对象
    response = cJSON_CreateObject();
    if (response == NULL) {
        goto end;
    }
    
    // 创建data对象
    data = cJSON_CreateObject();
    if (data == NULL) {
        goto end;
    }
    
    // 创建Content数组
    content_array = cJSON_CreateArray();
    if (content_array == NULL) {
        goto end;
    }
    
    // 查询数据库中的所有媒体信息
    MediaList media_list;
    if (query_media_to_struct(&media_list) != 0) {
        LOG_ERROR("Failed to query media data");
        goto end;
    }
    
    // 遍历查询结果，为每个媒体项创建JSON对象
    for (int i = 0; i < media_list.count; i++) {
        cJSON *media_item = cJSON_CreateObject();
        if (media_item == NULL) {
            continue;
        }
        
        // 添加媒体基本信息
        cJSON_AddStringToObject(media_item, "MediaDesc", media_list.medias[i].MediaDesc);
        cJSON_AddStringToObject(media_item, "MediaName", media_list.medias[i].MediaName);
        cJSON_AddStringToObject(media_item, "MediaUrl", media_list.medias[i].MediaUrl);
        cJSON_AddNumberToObject(media_item, "ProtocolType", media_list.medias[i].ProtocolType);
        
        // 创建MediaStatus对象
        cJSON *media_status = cJSON_CreateObject();
        if (media_status != NULL) {
            cJSON_AddStringToObject(media_status, "label", media_list.medias[i].StatusLabel);
            cJSON_AddNumberToObject(media_status, "type", media_list.medias[i].StatusType);
            cJSON_AddItemToObject(media_item, "MediaStatus", media_status);
        }
        
        // 将媒体项添加到Content数组
        cJSON_AddItemToArray(content_array, media_item);
    }
    
    // 释放查询结果内存
    free_media_list(&media_list);
    
    // 将Content数组添加到data对象
    cJSON_AddItemToObject(data, "Content", content_array);
    
    // 添加字段到返回的JSON对象
    cJSON_AddStringToObject(response, "Event", "Media_Fetch");
    cJSON_AddItemToObject(response, "data", data);
    cJSON_AddNumberToObject(response, "Status", 0);
    cJSON_AddStringToObject(response, "Message", "Success");
    
    // 转换为JSON字符串
    json_string = cJSON_Print(response);
    if (json_string == NULL) {
        goto end;
    }
    
    // 发送响应
    result = send_http_response(socket_fd, json_string);
    
end:
    if (response) {
        cJSON_Delete(response);
    }
    if (json_string) {
        free(json_string);
    }
    
    return result;
}

int Media_Add_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    MediaInfo media_info = {0}; // 使用MediaInfo结构体存储媒体信息
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Media_Add", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }

    // 获取 MediaDesc
    item = cJSON_GetObjectItem(root, "MediaDesc");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(media_info.MediaDesc, item->valuestring, sizeof(media_info.MediaDesc) - 1);
    }
    
    // 获取 MediaName
    item = cJSON_GetObjectItem(root, "MediaName");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaName");
        general_reply("Media_Add", 1, socket_fd, "Failed to get MediaName"); // 发送错误响应
        return 1;
    } else {
        strncpy(media_info.MediaName, item->valuestring, sizeof(media_info.MediaName) - 1);
    }
    
    // 获取 MediaUrl
    item = cJSON_GetObjectItem(root, "MediaUrl");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaUrl");
        general_reply("Media_Add", 1, socket_fd, "Failed to get MediaUrl"); // 发送错误响应
        return 1;
    } else {
        strncpy(media_info.MediaUrl, item->valuestring, sizeof(media_info.MediaUrl) - 1);
    }
    
    // 获取 ProtocolType
    item = cJSON_GetObjectItem(root, "ProtocolType");
    if (item != NULL && cJSON_IsNumber(item)) {
        media_info.ProtocolType = item->valueint;
    }

    media_info.StatusType = 2;
      
    // 打印解析到的信息（用于调试）
    LOG_DEBUG("MediaDesc: %s", media_info.MediaDesc);
    LOG_DEBUG("MediaName: %s", media_info.MediaName);
    LOG_DEBUG("MediaUrl: %s", media_info.MediaUrl);
    LOG_DEBUG("ProtocolType: %d", media_info.ProtocolType);
    
    // 将媒体信息添加到数据库
    if (add_media(&media_info) != 0) {
        LOG_ERROR("Failed to add media to database");
        general_reply("Media_Add", 1, socket_fd, "Failed to add media to database"); // 发送错误响应
        return 1;
    }
    
    // 发送成功响应
    result = general_reply("Media_Add", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_MEDIA);
    
    return result;
}

int Media_Del_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    char MediaName[64] = {0};
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Media_Del", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }

    // 获取 MediaName
    item = cJSON_GetObjectItem(root, "MediaName");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaName");
        general_reply("Media_Del", 1, socket_fd, "Failed to get MediaName"); // 发送错误响应
        return 1;
    } else {
        strncpy(MediaName, item->valuestring, sizeof(MediaName) - 1);
        int index = atoi(MediaName);
    }

    // 打印解析到的信息（用于调试）
    LOG_DEBUG("MediaName: %s", MediaName);
    
    // 在这里可以添加实际的媒体处理逻辑
    if(delete_media_by_name(MediaName) != 0) {
        LOG_ERROR("Failed to del media to database");
        general_reply("Media_Del", 1, socket_fd, "Failed to del media to database"); // 发送错误响应
        return 1;
    }

    // 发送成功响应
    result = general_reply("Media_Del", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_MEDIA);

    return result;
}

int Media_Modify_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    MediaInfo media_info = {0}; // 使用MediaInfo结构体存储媒体信息
    char original_media_name[64] = {0}; // 存储原始的媒体名称用于查询
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Media_Modify", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }
    
    // 获取 MediaDesc
    item = cJSON_GetObjectItem(root, "MediaDesc");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(media_info.MediaDesc, item->valuestring, sizeof(media_info.MediaDesc) - 1);
    }
    
    // 获取 MediaName（新的媒体名称）
    item = cJSON_GetObjectItem(root, "MediaName");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaName");
        general_reply("Media_Modify", 1, socket_fd, "Failed to get MediaName"); // 发送错误响应
        return 1;
    } else {
        strncpy(media_info.MediaName, item->valuestring, sizeof(media_info.MediaName) - 1);
        // 保存原始媒体名称用于数据库查询
        strncpy(original_media_name, item->valuestring, sizeof(original_media_name) - 1);
    }
    
    // 获取 MediaUrl
    item = cJSON_GetObjectItem(root, "MediaUrl");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaUrl");
        general_reply("Media_Modify", 1, socket_fd, "Failed to get MediaUrl"); // 发送错误响应
        return 1;
    } else {
        strncpy(media_info.MediaUrl, item->valuestring, sizeof(media_info.MediaUrl) - 1);
    }
    
    // 获取 ProtocolType
    item = cJSON_GetObjectItem(root, "ProtocolType");
    if (item != NULL && cJSON_IsNumber(item)) {
        media_info.ProtocolType = item->valueint;
    }
    
    // 打印解析到的信息（用于调试）
    LOG_DEBUG("Original MediaName: %s", original_media_name);
    LOG_DEBUG("New MediaDesc: %s", media_info.MediaDesc);
    LOG_DEBUG("New MediaName: %s", media_info.MediaName);
    LOG_DEBUG("New MediaUrl: %s", media_info.MediaUrl);
    LOG_DEBUG("New ProtocolType: %d", media_info.ProtocolType);
    
    // 使用原始媒体名称修改数据库中的记录
    if (modify_media_by_name(original_media_name, &media_info) != 0) {
        LOG_ERROR("Failed to modify media in database");
        general_reply("Media_Modify", 1, socket_fd, "Failed to modify media in database"); // 发送错误响应
        return 1;
    }
    
    // 发送成功响应
    result = general_reply("Media_Modify", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_MEDIA);

    return result;
}

int Task_Fetch_handler(cJSON *root, int socket_fd) {
    cJSON *response = NULL;
    cJSON *data = NULL;
    cJSON *content_array = NULL;
    cJSON *task_item = NULL;
    char *json_string = NULL;
    int result = 1;
    
    // 创建响应JSON对象
    response = cJSON_CreateObject();
    if (response == NULL) {
        goto end;
    }
    
    // 创建data对象
    data = cJSON_CreateObject();
    if (data == NULL) {
        goto end;
    }
    
    // 创建Content数组
    content_array = cJSON_CreateArray();
    if (content_array == NULL) {
        goto end;
    }
    
    // 查询数据库中的所有任务信息
    TaskList task_list;
    if (query_task_to_struct(&task_list) != 0) {
        LOG_ERROR("Failed to query task data");
        goto end;
    }
    
    // 遍历查询结果，为每个任务项创建JSON对象
    for (int i = 0; i < task_list.count; i++) {
        task_item = cJSON_CreateObject();
        if (task_item == NULL) {
            continue;
        }
        
        // 添加任务基本信息
        cJSON_AddStringToObject(task_item, "AlgTaskSession", task_list.tasks[i].AlgTaskSession);
        cJSON_AddStringToObject(task_item, "MediaName", task_list.tasks[i].MediaName);
        cJSON_AddStringToObject(task_item, "TaskDesc", task_list.tasks[i].TaskDesc);
        cJSON_AddStringToObject(task_item, "Week", task_list.tasks[i].Week);
        cJSON_AddStringToObject(task_item, "Start_Time", task_list.tasks[i].Start_Time);
        cJSON_AddStringToObject(task_item, "End_Time", task_list.tasks[i].End_Time);
        
        // 创建AlgTaskStatus对象
        cJSON *task_status = cJSON_CreateObject();
        if (task_status != NULL) {
            cJSON_AddStringToObject(task_status, "label", task_list.tasks[i].StatusLabel);
            cJSON_AddNumberToObject(task_status, "type", task_list.tasks[i].StatusType);
            cJSON_AddItemToObject(task_item, "AlgTaskStatus", task_status);
        }
        
        // 创建BaseAlgItem数组
        cJSON *base_alg_array = cJSON_CreateArray();
        if (base_alg_array != NULL) {
            // 使用循环根据AlgorithmCount添加算法项
            for (int j = 0; j < task_list.tasks[i].AlgorithmCount && j < 3; j++) {
                cJSON *alg_item = cJSON_CreateObject();
                if (alg_item != NULL) {
                    int alg_id = 0;
                    char* alg_name = NULL;
                    
                    // 根据索引选择对应的算法信息
                    switch (j) {
                        case 0:
                            alg_id = task_list.tasks[i].Alg1.AlgId;
                            alg_name = task_list.tasks[i].Alg1.Name;
                            break;
                        case 1:
                            alg_id = task_list.tasks[i].Alg2.AlgId;
                            alg_name = task_list.tasks[i].Alg2.Name;
                            break;
                        case 2:
                            alg_id = task_list.tasks[i].Alg3.AlgId;
                            alg_name = task_list.tasks[i].Alg3.Name;
                            break;
                    }
                    
                    // 只有当AlgId大于0时才添加算法项
                    if (alg_id > 0) {
                        cJSON_AddNumberToObject(alg_item, "AlgId", alg_id);
                        cJSON_AddStringToObject(alg_item, "Name", alg_name);
                        cJSON_AddItemToArray(base_alg_array, alg_item);
                    } else {
                        // 如果AlgId不大于0，释放已创建的对象
                        cJSON_Delete(alg_item);
                    }
                }
            }
            
            cJSON_AddItemToObject(task_item, "BaseAlgItem", base_alg_array);
        }
        
        // 将任务项添加到Content数组
        cJSON_AddItemToArray(content_array, task_item);
    }
    
    // 释放查询结果内存
    free_task_list(&task_list);
    
    // 将Content数组添加到data对象
    cJSON_AddItemToObject(data, "Content", content_array);
    
    // 添加字段到返回的JSON对象
    cJSON_AddStringToObject(response, "Event", "Task_Fetch");
    cJSON_AddItemToObject(response, "data", data);
    cJSON_AddNumberToObject(response, "Status", 0);
    cJSON_AddStringToObject(response, "Message", "Success");
    
    // 转换为JSON字符串
    json_string = cJSON_Print(response);
    if (json_string == NULL) {
        goto end;
    }
    
    // 发送响应
    result = send_http_response(socket_fd, json_string);

end:
    if (response) {
        cJSON_Delete(response);
    }
    if (json_string) {
        free(json_string);
    }
    
    return result;
}

int Task_Add_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    cJSON *base_alg_array = NULL;
    cJSON *alg_item = NULL;
    TaskInfo task_info = {0}; // 使用TaskInfo结构体存储任务信息
    int alg_count = 0;
    int i;
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Task_Add", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }
    
    // 获取 AlgTaskSession
    item = cJSON_GetObjectItem(root, "AlgTaskSession");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get AlgTaskSession");
        general_reply("Task_Add", 1, socket_fd, "Failed to get AlgTaskSession"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.AlgTaskSession, item->valuestring, sizeof(task_info.AlgTaskSession) - 1);
    }
    
    // 获取 MediaName
    item = cJSON_GetObjectItem(root, "MediaName");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaName");
        general_reply("Task_Add", 1, socket_fd, "Failed to get MediaName"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.MediaName, item->valuestring, sizeof(task_info.MediaName) - 1);
    }
    
    // 获取 TaskDesc
    item = cJSON_GetObjectItem(root, "TaskDesc");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(task_info.TaskDesc, item->valuestring, sizeof(task_info.TaskDesc) - 1);
    }
    
    // 获取 Week
    item = cJSON_GetObjectItem(root, "Week");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Week");
        general_reply("Task_Add", 1, socket_fd, "Failed to get Week"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.Week, item->valuestring, sizeof(task_info.Week) - 1);
    }
    
    // 获取 Start_Time
    item = cJSON_GetObjectItem(root, "Start_Time");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Start_Time");
        general_reply("Task_Add", 1, socket_fd, "Failed to get Start_Time"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.Start_Time, item->valuestring, sizeof(task_info.Start_Time) - 1);
    }
    
    // 获取 End_Time
    item = cJSON_GetObjectItem(root, "End_Time");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get End_Time");
        general_reply("Task_Add", 1, socket_fd, "Failed to get Emd_Time"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.End_Time, item->valuestring, sizeof(task_info.End_Time) - 1);
    }
    
    // 获取 BaseAlgItem 数组
    base_alg_array = cJSON_GetObjectItem(root, "BaseAlgItem");
    if (base_alg_array == NULL || !cJSON_IsArray(base_alg_array)) {
        LOG_ERROR("Failed to get BaseAlgItem array");
        general_reply("Task_Add", 1, socket_fd, "Failed to get BaseAlgItem array"); // 发送错误响应
        return 1;
    }
    
    alg_count = cJSON_GetArraySize(base_alg_array);
    if (alg_count > 3) alg_count = 3; // 最多处理3个算法项
    
    // 设置实际算法数量
    task_info.AlgorithmCount = alg_count;
    
    // 打印解析到的基本信息
    LOG_DEBUG("AlgTaskSession: %s", task_info.AlgTaskSession);
    LOG_DEBUG("MediaName: %s", task_info.MediaName);
    LOG_DEBUG("TaskDesc: %s", task_info.TaskDesc);
    LOG_DEBUG("Week: %s", task_info.Week);
    LOG_DEBUG("Start_Time: %s", task_info.Start_Time);
    LOG_DEBUG("End_Time: %s", task_info.End_Time);
    LOG_DEBUG("Algorithm count: %d", alg_count);
    
    // 遍历算法项并填充到TaskInfo结构体中
    for (i = 0; i < alg_count; i++) {
        alg_item = cJSON_GetArrayItem(base_alg_array, i);
        if (alg_item == NULL) {
            continue;
        }
        
        // 获取 AlgId
        item = cJSON_GetObjectItem(alg_item, "AlgId");
        int alg_id = 0;
        if (item != NULL && cJSON_IsNumber(item)) {
            alg_id = item->valueint;
        }
        
        // 获取 Name
        item = cJSON_GetObjectItem(alg_item, "Name");
        char name[256] = {0};
        if (item != NULL && cJSON_IsString(item)) {
            strncpy(name, item->valuestring, sizeof(name) - 1);
        }
        
        // 根据索引填充算法信息
        switch (i) {
            case 0:
                task_info.Alg1.AlgId = alg_id;
                strncpy(task_info.Alg1.Name, name, sizeof(task_info.Alg1.Name) - 1);
                break;
            case 1:
                task_info.Alg2.AlgId = alg_id;
                strncpy(task_info.Alg2.Name, name, sizeof(task_info.Alg2.Name) - 1);
                break;
            case 2:
                task_info.Alg3.AlgId = alg_id;
                strncpy(task_info.Alg3.Name, name, sizeof(task_info.Alg3.Name) - 1);
                break;
        }
        
        // 打印算法信息
        LOG_DEBUG("Algorithm %d - AlgId: %d, Name: %s", i+1, alg_id, name);
    }
    
    // 使用sql.c中的add_task函数将任务信息添加到数据库
    if (add_task(&task_info) != 0) {
        LOG_ERROR("Failed to add task to database");
        general_reply("Task_Add", 1, socket_fd, "Failed to add task to database"); // 发送错误响应
        return 1;
    }
    
    // 发送成功响应
    result = general_reply("Task_Add", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_TASK);

    return result;
}

int Task_Del_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    char AlgTaskSession[64] = {0};
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Task_Del", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }

    // 获取 AlgTaskSession
    item = cJSON_GetObjectItem(root, "AlgTaskSession");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get AlgTaskSession");
        general_reply("Task_Del", 1, socket_fd, "Failed to get AlgTaskSession"); // 发送错误响应
        return 1;
    } else {
        strncpy(AlgTaskSession, item->valuestring, sizeof(AlgTaskSession) - 1);
    }

    // 打印解析到的信息（用于调试）
    LOG_DEBUG("AlgTaskSession: %s", AlgTaskSession);
    
    // 删除数据库中的任务记录
    if (delete_task_by_session(AlgTaskSession) != 0) {
        LOG_ERROR("Failed to delete task from database");
        general_reply("Task_Del", 1, socket_fd, "Failed to delete task from database"); // 发送错误响应
        return 1;
    }

    // 删除任务的配置
    if (del_task_config_by_session(AlgTaskSession) != 0) {
        LOG_ERROR("Failed to delete task from database");
        general_reply("Task_Del", 1, socket_fd, "Failed to delete task from database"); // 发送错误响应
        return 1;
    }

    // 发送成功响应
    result = general_reply("Task_Del", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_TASK);

    return result;
}

int Task_Modify_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    cJSON *base_alg_array = NULL;
    cJSON *alg_item = NULL;
    TaskInfo task_info = {0}; // 使用TaskInfo结构体存储任务信息
    char original_alg_task_session[64] = {0}; // 存储原始的任务会话ID用于查询
    int alg_count = 0;
    int i;
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Task_Modify", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }
    
    // 获取 AlgTaskSession
    item = cJSON_GetObjectItem(root, "AlgTaskSession");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get AlgTaskSession");
        general_reply("Task_Modify", 1, socket_fd, "Failed to get AlgTaskSession"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.AlgTaskSession, item->valuestring, sizeof(task_info.AlgTaskSession) - 1);
        // 保存原始任务会话ID用于数据库查询
        strncpy(original_alg_task_session, item->valuestring, sizeof(original_alg_task_session) - 1);
    }
    
    // 获取 MediaName
    item = cJSON_GetObjectItem(root, "MediaName");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaName");
        general_reply("Task_Modify", 1, socket_fd, "Failed to get MediaName"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.MediaName, item->valuestring, sizeof(task_info.MediaName) - 1);
    }
    
    // 获取 TaskDesc
    item = cJSON_GetObjectItem(root, "TaskDesc");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(task_info.TaskDesc, item->valuestring, sizeof(task_info.TaskDesc) - 1);
    }
    
    // 获取 Week
    item = cJSON_GetObjectItem(root, "Week");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Week");
        general_reply("Task_Modify", 1, socket_fd, "Failed to get Week"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.Week, item->valuestring, sizeof(task_info.Week) - 1);
    }
    
    // 获取 Start_Time
    item = cJSON_GetObjectItem(root, "Start_Time");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Start_Time");
        general_reply("Task_Modify", 1, socket_fd, "Failed to get Start_Time"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.Start_Time, item->valuestring, sizeof(task_info.Start_Time) - 1);
    }
    
    // 获取 End_Time
    item = cJSON_GetObjectItem(root, "End_Time");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get End_Time");
        general_reply("Task_Modify", 1, socket_fd, "Failed to get End_Time"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_info.End_Time, item->valuestring, sizeof(task_info.End_Time) - 1);
    }
    
    // 获取 BaseAlgItem 数组
    base_alg_array = cJSON_GetObjectItem(root, "BaseAlgItem");
    if (base_alg_array == NULL || !cJSON_IsArray(base_alg_array)) {
        LOG_ERROR("Failed to get BaseAlgItem array");
        general_reply("Task_Modify", 1, socket_fd, "Failed to get BaseAlgItem array"); // 发送错误响应
        return 1;
    }
    
    alg_count = cJSON_GetArraySize(base_alg_array);
    if (alg_count > 3) alg_count = 3; // 最多处理3个算法项
    
    // 设置实际算法数量
    task_info.AlgorithmCount = alg_count;
    
    // 打印解析到的基本信息
    LOG_DEBUG("Original AlgTaskSession: %s", original_alg_task_session);
    LOG_DEBUG("New AlgTaskSession: %s", task_info.AlgTaskSession);
    LOG_DEBUG("MediaName: %s", task_info.MediaName);
    LOG_DEBUG("TaskDesc: %s", task_info.TaskDesc);
    LOG_DEBUG("Week: %s", task_info.Week);
    LOG_DEBUG("Start_Time: %s", task_info.Start_Time);
    LOG_DEBUG("End_Time: %s", task_info.End_Time);
    LOG_DEBUG("Algorithm count: %d", alg_count);
    
    // 遍历算法项并填充到TaskInfo结构体中
    for (i = 0; i < alg_count; i++) {
        alg_item = cJSON_GetArrayItem(base_alg_array, i);
        if (alg_item == NULL) {
            continue;
        }
        
        // 获取 AlgId
        item = cJSON_GetObjectItem(alg_item, "AlgId");
        int alg_id = 0;
        if (item != NULL && cJSON_IsNumber(item)) {
            alg_id = item->valueint;
        }
        
        // 获取 Name
        item = cJSON_GetObjectItem(alg_item, "Name");
        char name[256] = {0};
        if (item != NULL && cJSON_IsString(item)) {
            strncpy(name, item->valuestring, sizeof(name) - 1);
        }
        
        // 根据索引填充算法信息
        switch (i) {
            case 0:
                task_info.Alg1.AlgId = alg_id;
                strncpy(task_info.Alg1.Name, name, sizeof(task_info.Alg1.Name) - 1);
                break;
            case 1:
                task_info.Alg2.AlgId = alg_id;
                strncpy(task_info.Alg2.Name, name, sizeof(task_info.Alg2.Name) - 1);
                break;
            case 2:
                task_info.Alg3.AlgId = alg_id;
                strncpy(task_info.Alg3.Name, name, sizeof(task_info.Alg3.Name) - 1);
                break;
        }
        
        // 打印算法信息
        LOG_DEBUG("Algorithm %d - AlgId: %d, Name: %s", i+1, alg_id, name);
    }
    
    // 使用原始任务会话ID修改数据库中的记录
    if (modify_task_by_session(original_alg_task_session, &task_info) != 0) {
        LOG_ERROR("Failed to modify task in database");
        general_reply("Task_Modify", 1, socket_fd, "Failed to modify task in database"); // 发送错误响应
        return 1;
    }
    
    // 发送成功响应
    result = general_reply("Task_Modify", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_TASK);

    return result;
}

int Task_Control_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    char AlgTaskSession[64] = {0};
    char Control[32] = {0};
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Task_Control", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }
    
    // 获取 AlgTaskSession
    item = cJSON_GetObjectItem(root, "AlgTaskSession");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get AlgTaskSession");
        general_reply("Task_Control", 1, socket_fd, "Failed to get AlgTaskSession"); // 发送错误响应
        return 1;
    } else {
        strncpy(AlgTaskSession, item->valuestring, sizeof(AlgTaskSession) - 1);
    }
    
    // 获取 Control
    item = cJSON_GetObjectItem(root, "Control");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Control");
        general_reply("Task_Control", 1, socket_fd, "Failed to get Control"); // 发送错误响应
        return 1;
    } else {
        strncpy(Control, item->valuestring, sizeof(Control) - 1);
    }
    
    // 打印解析到的信息（用于调试）
    LOG_DEBUG("AlgTaskSession: %s", AlgTaskSession);
    LOG_DEBUG("Control: %s", Control);
    
    // 在这里可以添加实际的任务控制逻辑
    if (strcmp(Control, "start") == 0) {
        LOG_DEBUG("Starting task session: %s", AlgTaskSession);
        // 添加启动任务的代码
    } else if (strcmp(Control, "stop") == 0) {
        LOG_DEBUG("Stopping task session: %s", AlgTaskSession);
        // 添加停止任务的代码
    } else {
        LOG_DEBUG("Unknown control command: %s", Control);
    }
    
    // 发送成功响应
    result = general_reply("Task_Control", 0, socket_fd, NULL);
    
    return result;
}

int Task_Config_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    cJSON *base_alg_array = NULL;
    cJSON *rule_property_array = NULL;
    cJSON *alg_item = NULL;
    cJSON *rule_item = NULL;
    cJSON *points_array = NULL;
    cJSON *rule_points_array = NULL;
    cJSON *point_item = NULL;
    char AlgTaskSession[64] = {0};
    char MediaName[64] = {0};
    char TaskDesc[256] = {0};
    TaskConfig task_config = {0}; // 使用TaskConfig结构体存储任务配置信息
    int alg_count = 0;
    int rule_count = 0;
    int i, j;
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Task_Config", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }
    // 获取 AlgTaskSession
    item = cJSON_GetObjectItem(root, "AlgTaskSession");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get AlgTaskSession");
        general_reply("Task_Config", 1, socket_fd, "Failed to get AlgTaskSession"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_config.AlgTaskSession, item->valuestring, sizeof(task_config.AlgTaskSession) - 1);
        strncpy(AlgTaskSession, item->valuestring, sizeof(AlgTaskSession) - 1);
    }
    
    // 获取 MediaName
    item = cJSON_GetObjectItem(root, "MediaName");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get MediaName");
        general_reply("Task_Config", 1, socket_fd, "Failed to get MediaName"); // 发送错误响应
        return 1;
    } else {
        strncpy(task_config.MediaName, item->valuestring, sizeof(task_config.MediaName) - 1);
        strncpy(MediaName, item->valuestring, sizeof(MediaName) - 1);
    }
    
    // 获取 TaskDesc
    item = cJSON_GetObjectItem(root, "TaskDesc");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(task_config.TaskDesc, item->valuestring, sizeof(task_config.TaskDesc) - 1);
        strncpy(TaskDesc, item->valuestring, sizeof(TaskDesc) - 1);
    }
    
    // 获取 BaseAlgItem 数组
    base_alg_array = cJSON_GetObjectItem(root, "BaseAlgItem");
    if (base_alg_array == NULL || !cJSON_IsArray(base_alg_array)) {
        LOG_ERROR("Failed to get BaseAlgItem array");
        general_reply("Task_Config", 1, socket_fd, "Failed to get BaseAlgItem array"); // 发送错误响应
        return 1;
    }
    
    alg_count = cJSON_GetArraySize(base_alg_array);
    if (alg_count > 1) alg_count = 1; // 最多处理1个算法项
    
    // 获取 RuleProperty 数组
    rule_property_array = cJSON_GetObjectItem(root, "RuleProperty");
    if (rule_property_array == NULL || !cJSON_IsArray(rule_property_array)) {
        LOG_ERROR("Failed to get RuleProperty array");
        general_reply("Task_Config", 1, socket_fd, "Failed to get RuleProperty array"); // 发送错误响应
        return 1;
    }
    
    rule_count = cJSON_GetArraySize(rule_property_array);
    if (rule_count > 1) rule_count = 1; // 最多处理1个规则属性
    
    // 打印解析到的基本信息
    LOG_DEBUG("AlgTaskSession: %s", AlgTaskSession);
    LOG_DEBUG("MediaName: %s", MediaName);
    LOG_DEBUG("TaskDesc: %s", TaskDesc);
    LOG_DEBUG("Algorithm count: %d", alg_count);
    LOG_DEBUG("Rule count: %d", rule_count);
    
    // 遍历算法项（最多处理1个）
    if (alg_count > 0) {
        alg_item = cJSON_GetArrayItem(base_alg_array, 0);
        if (alg_item != NULL) {
            // 获取 AlgId
            item = cJSON_GetObjectItem(alg_item, "AlgId");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.alg_item.AlgId = item->valueint;
            }
            
            // 获取 Name
            item = cJSON_GetObjectItem(alg_item, "Name");
            if (item != NULL && cJSON_IsString(item)) {
                strncpy(task_config.alg_item.Name, item->valuestring, sizeof(task_config.alg_item.Name) - 1);
            }
            
            // 打印算法信息
            LOG_DEBUG("Algorithm - AlgId: %d, Name: %s", task_config.alg_item.AlgId, task_config.alg_item.Name);
        }
    }

    // 遍历规则属性项（最多处理1个）
    if (rule_count > 0) {
        rule_item = cJSON_GetArrayItem(rule_property_array, 0);
        if (rule_item != NULL) {
            // 获取 RuleId
            item = cJSON_GetObjectItem(rule_item, "RuleId");
            if (item != NULL && cJSON_IsString(item)) {
                strncpy(task_config.rule_property.RuleId, item->valuestring, sizeof(task_config.rule_property.RuleId) - 1);
            }
            
            // 获取 RuleType
            item = cJSON_GetObjectItem(rule_item, "RuleType");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.RuleType = item->valueint;
            }
            
            // 获取 RuleTypeName
            item = cJSON_GetObjectItem(rule_item, "RuleTypeName");
            if (item != NULL && cJSON_IsString(item)) {
                strncpy(task_config.rule_property.RuleTypeName, item->valuestring, sizeof(task_config.rule_property.RuleTypeName) - 1);
            }

            // 获取 Baseelevation
            item = cJSON_GetObjectItem(rule_item, "Baseelevation");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.Baseelevation = item->valuedouble;
            }

            // 获取新增的水尺相关字段
            // 获取 Watermark
            item = cJSON_GetObjectItem(rule_item, "Watermark");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.Watermark = item->valuedouble;
            }
            
            // 获取 TimeInterval
            item = cJSON_GetObjectItem(rule_item, "TimeInterval");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.TimeInterval = item->valueint;
            }
            
            // 获取 WaterHighAlarm
            item = cJSON_GetObjectItem(rule_item, "WaterHighAlarm");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.WaterHighAlarm = item->valuedouble;
            }
            
            // 获取 WaterHighWarn
            item = cJSON_GetObjectItem(rule_item, "WaterHighWarn");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.WaterHighWarn = item->valuedouble;
            }
            
            // 获取 WaterLowAlarm
            item = cJSON_GetObjectItem(rule_item, "WaterLowAlarm");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.WaterLowAlarm = item->valuedouble;
            }
            
            // 获取 WaterLowWarn
            item = cJSON_GetObjectItem(rule_item, "WaterLowWarn");
            if (item != NULL && cJSON_IsNumber(item)) {
                task_config.rule_property.WaterLowWarn = item->valuedouble;
            }
            
            // 打印规则基本信息
            LOG_DEBUG("Rule - RuleId: %s, RuleType: %d, RuleTypeName: %s, Baseelevation: %f", 
                   task_config.rule_property.RuleId, task_config.rule_property.RuleType, task_config.rule_property.RuleTypeName, task_config.rule_property.Baseelevation);
            
            // 获取 Points 数组
            points_array = cJSON_GetObjectItem(rule_item, "Points");
            if (points_array != NULL && cJSON_IsArray(points_array)) {
                task_config.rule_property.PointsCount = cJSON_GetArraySize(points_array);
                if (task_config.rule_property.PointsCount > 4) task_config.rule_property.PointsCount = 4; // 最多4个点
                LOG_DEBUG("Points count: %d", task_config.rule_property.PointsCount);
                
                // 遍历Points并存储到结构体
                for (j = 0; j < task_config.rule_property.PointsCount; j++) {
                    point_item = cJSON_GetArrayItem(points_array, j);
                    if (point_item != NULL) {
                        cJSON *x_item = cJSON_GetObjectItem(point_item, "X");
                        cJSON *y_item = cJSON_GetObjectItem(point_item, "Y");
                        
                        if (x_item != NULL && cJSON_IsNumber(x_item)) {
                            task_config.rule_property.Points[j].X = x_item->valuedouble;
                        }
                        
                        if (y_item != NULL && cJSON_IsNumber(y_item)) {
                            task_config.rule_property.Points[j].Y = y_item->valuedouble;
                        }
                        
                        LOG_DEBUG("Point %d: X=%.6f, Y=%.6f", j, task_config.rule_property.Points[j].X, task_config.rule_property.Points[j].Y);
                    }
                }
            }
            
            // 获取 RulePoints 数组
            rule_points_array = cJSON_GetObjectItem(rule_item, "RulePoints");
            if (rule_points_array != NULL && cJSON_IsArray(rule_points_array)) {
                task_config.rule_property.RulePointsCount = cJSON_GetArraySize(rule_points_array);
                if (task_config.rule_property.RulePointsCount > 4) task_config.rule_property.RulePointsCount = 4; // 最多4个点
                LOG_DEBUG("RulePoints count: %d", task_config.rule_property.RulePointsCount);
                
                // 遍历RulePoints并存储到结构体
                for (j = 0; j < task_config.rule_property.RulePointsCount; j++) {
                    point_item = cJSON_GetArrayItem(rule_points_array, j);
                    if (point_item != NULL) {
                        cJSON *x_item = cJSON_GetObjectItem(point_item, "X");
                        cJSON *y_item = cJSON_GetObjectItem(point_item, "Y");
                        
                        if (x_item != NULL && cJSON_IsNumber(x_item)) {
                            task_config.rule_property.RulePoints[j].X = x_item->valuedouble;
                        }
                        
                        if (y_item != NULL && cJSON_IsNumber(y_item)) {
                            task_config.rule_property.RulePoints[j].Y = y_item->valuedouble;
                        }
                        
                        LOG_DEBUG("RulePoint %d: X=%.6f, Y=%.6f", j, task_config.rule_property.RulePoints[j].X, task_config.rule_property.RulePoints[j].Y);
                    }
                }
            }
        }
    }
    
    // 使用save_task_config函数保存配置信息到数据库
    if (save_task_config(&task_config) != 0) {
        LOG_ERROR("Failed to save task config to database");
        general_reply("Task_Config", 1, socket_fd, "Failed to save task config to database"); // 发送错误响应
        return 1;
    }
    
    // 发送成功响应
    result = general_reply("Task_Config", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_TASK_CONFIG);

    return result;
}

int Reboot_handler(cJSON *root, int socket_fd) {
    int result = 1;

    // 打印调试信息
    LOG_INFO("System reboot requested");
    
    // 先发送成功响应
    result = general_reply("Reboot", 0, socket_fd, NULL);

    // 创建线程来处理实际的系统重启操作
    if(result == 0)
        create_reboot_thread(REBOOT_TYPE_SYSTEM);
    
    return result;
}

int Time_Sync_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    char Date[64] = {0};
    char Timezone[64] = {0};
    char cmd[256];
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Time_Sync", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }
    
    // 获取 Date
    item = cJSON_GetObjectItem(root, "Date");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Date");
        general_reply("Time_Sync", 1, socket_fd, "Failed to get Date"); // 发送错误响应
        return 1;
    } else {
        strncpy(Date, item->valuestring, sizeof(Date) - 1);
    }
    
    // 获取 Timezone
    item = cJSON_GetObjectItem(root, "Timezone");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get Timezone");
        general_reply("Time_Sync", 1, socket_fd, "Failed to get Timezone"); // 发送错误响应
        return 1;
    } else {
        strncpy(Timezone, item->valuestring, sizeof(Timezone) - 1);
    }
    
    // 打印解析到的信息（用于调试）
    LOG_DEBUG("Date: %s", Date);
    LOG_DEBUG("Timezone: %s", Timezone);
    
    // 设置系统时间
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "date -s '%s'", Date);
    system(cmd);
    
    // 设置时区
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ln -sf /usr/share/zoneinfo/%s /etc/localtime", Timezone);
    system(cmd);
    
    // 保存时区设置到文件
    FILE *tz_file = fopen("/etc/timezone", "w");
    if (tz_file != NULL) {
        fprintf(tz_file, "%s\n", Timezone);
        fclose(tz_file);
    }

    // 发送成功响应
    result = general_reply("Time_Sync", 0, socket_fd, NULL);
    
    return result;
}

int Upgrade_handler(cJSON *root, int socket_fd) {
    int result = 1;

    return result;
}

int Passwd_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    char cmd[256] = {0};
    char OldPasswd[64] = {0};
    char NewPasswd[64] = {0};
    char old_passwd_md5[64] = {0};
    char new_passwd_md5[64] = {0};
    char stored_password[64] = {0};
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Passwd", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }

    item = cJSON_GetObjectItem(root, "OldPasswd");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get OldPasswd");
        general_reply("Passwd", 1, socket_fd, "Failed to get OldPasswd"); // 发送错误响应
        return 1;
    } else {
        strncpy(OldPasswd, item->valuestring, sizeof(OldPasswd) - 1);
    }

    item = cJSON_GetObjectItem(root, "NewPasswd");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get NewPasswd");
        general_reply("Passwd", 1, socket_fd, "Failed to get NewPasswd"); // 发送错误响应
        return 1;
    } else {
        strncpy(NewPasswd, item->valuestring, sizeof(NewPasswd) - 1);
    }
    
    // 计算旧密码的MD5值
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "echo -n \"%s\" | md5sum | cut -d' ' -f1", OldPasswd);
    if (call_sys(cmd, old_passwd_md5, sizeof(old_passwd_md5)) != 0) {
        LOG_ERROR("Failed to calculate old password MD5");
        general_reply("Passwd", 1, socket_fd, "Failed to process old password"); // 发送错误响应
        return 1;
    }
    
    // 计算新密码的MD5值
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "echo -n \"%s\" | md5sum | cut -d' ' -f1", NewPasswd);
    if (call_sys(cmd, new_passwd_md5, sizeof(new_passwd_md5)) != 0) {
        LOG_ERROR("Failed to calculate new password MD5");
        general_reply("Passwd", 1, socket_fd, "Failed to process new password"); // 发送错误响应
        return 1;
    }
    
    // 从PASSWORD_FILE文件中读取当前存储的密码
    snprintf(cmd, sizeof(cmd), "cat %s", PASSWORD_FILE);
    if (call_sys(cmd, stored_password, sizeof(stored_password)) != 0) {
        LOG_ERROR("Failed to read password file");
        general_reply("Passwd", 1, socket_fd, "Failed to read password file"); // 发送错误响应
        return 1;
    }
    
    // 移除可能的换行符
    stored_password[strcspn(stored_password, "\r\n")] = '\0';
    
    // 验证旧密码是否正确
    if (strcmp(old_passwd_md5, stored_password) != 0) {
        LOG_ERROR("Old password is incorrect");
        general_reply("Passwd", 1, socket_fd, "Old password is incorrect"); // 发送错误响应
        return 1;
    }
    
    // 更新密码到PASSWORD_FILE文件（存储新密码的MD5值）
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "echo '%s' > %s", new_passwd_md5, PASSWORD_FILE);
    if (system(cmd) != 0) {
        LOG_ERROR("Failed to update password file");
        general_reply("Passwd", 1, socket_fd, "Failed to update password file"); // 发送错误响应
        return 1;
    }
    
    // 发送成功响应
    result = general_reply("Passwd", 0, socket_fd, NULL);
    
    return result;
}

int Task_snap_handler(cJSON *root, int socket_fd) {
    cJSON *response = NULL;
    cJSON *data = NULL;
    cJSON *item = NULL;
    char AlgTaskSession[8] = {0};
    char ImageDate[128] = {0};
    char *json_string = NULL;
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Task_snap", 1, socket_fd, "Failed to get Data"); // 发送错误响应
        return result;
    }

    // 获取 AlgTaskSession
    item = cJSON_GetObjectItem(root, "AlgTaskSession");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get AlgTaskSession");
        general_reply("Task_snap", 1, socket_fd, "Failed to get AlgTaskSession");
        return 1;
    } else {
        strncpy(AlgTaskSession, item->valuestring, sizeof(AlgTaskSession) - 1);
    }

    // 打印调试信息
    LOG_INFO("AlgTaskSession: %s", AlgTaskSession);

    //todo 根据AlgTaskSession获取图片和路径

    // 创建响应JSON对象
    response = cJSON_CreateObject();
    if (response == NULL) {
        goto end;
    }

    // 创建data对象
    data = cJSON_CreateObject();
    if (data == NULL) {
        goto end;
    }

    // 添加字段到data对象
    cJSON_AddStringToObject(data, "AlgTaskSession", AlgTaskSession);
    cJSON_AddStringToObject(data, "ImageDate", "/zoneconfig/alert_20250908_074637_808006.jpg");

    // 添加字段到返回的JSON对象
    cJSON_AddStringToObject(response, "Event", "Task_snap");
    cJSON_AddItemToObject(response, "data", data);
    cJSON_AddNumberToObject(response, "Status", 0);
    cJSON_AddStringToObject(response, "Message", "Success");

    // 转换为JSON字符串
    json_string = cJSON_Print(response);
    if (json_string == NULL) {
        goto end;
    }

    // 发送响应
    result = send_http_response(socket_fd, json_string);

end:
    if (response) {
        cJSON_Delete(response);
    }
    if (json_string) {
        free(json_string);
    }

    return result;
}

int Alarm_Fetch_handler(cJSON *root, int socket_fd) {
    cJSON *response = NULL;
    cJSON *data = NULL;
    cJSON *alarm_array = NULL;
    cJSON *item = NULL;
    char *json_string = NULL;
    int result = 1;
    
    // 获取查询参数
    char* task = NULL;
    char* channel = NULL;
    long long begin_time = 0;
    long long end_time = 0;
    int type = 0;
    int type_specified = 0;
    int page = 0;
    
    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Alarm_Fetch", 1, socket_fd, "Failed to get Data");
        return result;
    }
    
    // 获取 Task
    item = cJSON_GetObjectItem(root, "Task");
    if (item != NULL && cJSON_IsString(item)) {
        task = item->valuestring;
    }
    
    // 获取 Channel
    item = cJSON_GetObjectItem(root, "Channel");
    if (item != NULL && cJSON_IsString(item)) {
        channel = item->valuestring;
    }
    
    // 获取 Begin_time
    item = cJSON_GetObjectItem(root, "Begin_time");
    if (item != NULL && cJSON_IsNumber(item)) {
        begin_time = item->valuedouble;
    }
    
    // 获取 End_time
    item = cJSON_GetObjectItem(root, "End_time");
    if (item != NULL && cJSON_IsNumber(item)) {
        end_time = item->valuedouble;
    }
    
    // 获取 Type
    item = cJSON_GetObjectItem(root, "Type");
    if (item != NULL && cJSON_IsString(item) && strlen(item->valuestring) > 0) {
        type = atoi(item->valuestring);
        type_specified = 1;
    }

    // 获取 Page
    item = cJSON_GetObjectItem(root, "Page");
    if (item != NULL && cJSON_IsNumber(item)) {
        page = item->valueint;
    }

    // 创建响应JSON对象
    response = cJSON_CreateObject();
    if (response == NULL) {
        goto end;
    }
    
    // 创建data对象
    data = cJSON_CreateObject();
    if (data == NULL) {
        goto end;
    }
    
    // 创建Alarm数组
    alarm_array = cJSON_CreateArray();
    if (alarm_array == NULL) {
        goto end;
    }
    
    // 查询数据库中的告警信息
    AlarmList alarm_list;
    AlarmQueryCondition condition = {0};
    condition.task_session = task;
    condition.media_name = channel;
    condition.begin_time = begin_time;
    condition.end_time = end_time;
    condition.type = type;
    condition.type_specified = type_specified;
    condition.page = page;

    if (query_alarms_to_struct(&alarm_list, &condition) != 0) {
        LOG_ERROR("Failed to query alarm data");
        free_alarm_list(&alarm_list);
        goto end;
    }

    // printf("=== Alarm List ===\n");
    // printf("alarm_list.count: %d\n", alarm_list.count);
    
    // 遍历查询结果，为每个告警项创建JSON对象
    for (int i = 0; i < alarm_list.count; i++) {
        cJSON *alarm_item = cJSON_CreateObject();
        if (alarm_item == NULL) {
            continue;
        }
        
        // 添加告警信息
        cJSON_AddStringToObject(alarm_item, "AlarmId", alarm_list.alarms[i].AlarmId);
        cJSON_AddStringToObject(alarm_item, "TaskSession", alarm_list.alarms[i].TaskSession);
        cJSON_AddStringToObject(alarm_item, "MediaName", alarm_list.alarms[i].MediaName);
        cJSON_AddStringToObject(alarm_item, "ImageData", alarm_list.alarms[i].ImageData);
        cJSON_AddStringToObject(alarm_item, "Time", alarm_list.alarms[i].Time);
        cJSON_AddNumberToObject(alarm_item, "TimeStamp", alarm_list.alarms[i].TimeStamp);
        cJSON_AddNumberToObject(alarm_item, "Type", alarm_list.alarms[i].Type);
        
        // 将告警项添加到Alarm数组
        cJSON_AddItemToArray(alarm_array, alarm_item);
    }

    // 添加AlarmCount字段
    cJSON_AddNumberToObject(data, "AlarmCount", alarm_list.all_count);
        
    // 将Alarm数组添加到data对象
    cJSON_AddItemToObject(data, "Alarm", alarm_array);
    
    // 添加字段到返回的JSON对象
    cJSON_AddStringToObject(response, "Event", "Alarm_Fetch");
    cJSON_AddItemToObject(response, "data", data);
    cJSON_AddNumberToObject(response, "Status", 0);
    cJSON_AddStringToObject(response, "Message", "Success");
    
    // 转换为JSON字符串
    json_string = cJSON_Print(response);
    if (json_string == NULL) {
        goto end;
    }
    
    // 释放查询结果内存
    free_alarm_list(&alarm_list);

    // 发送响应
    result = send_http_response(socket_fd, json_string);
    
end:
    if (response) {
        cJSON_Delete(response);
    }
    if (json_string) {
        free(json_string);
    }
    
    return result;
}

int Alarm_Del_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    int result = 1;
    int i = 0;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Alarm_Del", 1, socket_fd, "Failed to get Data");
        return result;
    }

    // 获取 AlarmId 数组
    item = cJSON_GetObjectItem(root, "AlarmId");
    if (item == NULL || !cJSON_IsArray(item)) {
        LOG_ERROR("Failed to get AlarmId array");
        general_reply("Alarm_Del", 1, socket_fd, "Failed to get AlarmId array");
        return 1;
    }
    
    // 遍历数组，依次删除每个AlarmId
    int array_size = cJSON_GetArraySize(item);
    for (i = 0; i < array_size; i++) {
        cJSON *alarm_item = cJSON_GetArrayItem(item, i);
        if (alarm_item != NULL && cJSON_IsString(alarm_item)) {
            // 删除数据库中的任务记录
            LOG_DEBUG("delete Alarm from database, AlarmId: %s", alarm_item->valuestring);
            if (delete_alarm_by_id(alarm_item->valuestring) != 0) {
                LOG_ERROR("Failed to delete Alarm from database, AlarmId: %s", alarm_item->valuestring);
                general_reply("Alarm_Del", 1, socket_fd, "Failed to delete task from database");
                return 1;
            }
            LOG_DEBUG("Successfully deleted AlarmId: %s", alarm_item->valuestring);
        } else {
            LOG_ERROR("Invalid AlarmId at index %d", i);
            general_reply("Alarm_Del", 1, socket_fd, "Invalid AlarmId format");
            return 1;
        }
    }
    
    // 所有AlarmId删除成功，发送成功响应
    result = general_reply("Alarm_Del", 0, socket_fd, NULL);

    return result;
}

int Alarm_Clear_handler(cJSON *root, int socket_fd) {

    int result = 1;

    if(delete_all_alarms() != 0) {
        LOG_ERROR("Failed to delete all alarms");
        general_reply("Alarm_Clear", 1, socket_fd, "Failed to delete all alarms");
        return result;
    }

    // 发送成功响应
    LOG_DEBUG("Successfully deleted all alarms");
    result = general_reply("Alarm_Clear", 0, socket_fd, NULL);

    return result;
}

int Face_Query_handler(cJSON *root, int socket_fd) {
    cJSON *response = NULL;
    cJSON *data = NULL;
    cJSON *face_array = NULL;
    char *json_string = NULL;
    int result = 1;
    
    // 创建响应JSON对象
    response = cJSON_CreateObject();
    if (response == NULL) {
        goto end;
    }
    
    // 创建data对象
    data = cJSON_CreateObject();
    if (data == NULL) {
        goto end;
    }
    
    // 创建Face数组
    face_array = cJSON_CreateArray();
    if (face_array == NULL) {
        goto end;
    }
    
    // 查询数据库中的所有人物信息
    PersonList person_list;
    if (query_persons_to_struct(&person_list) != 0) {
        LOG_ERROR("Failed to query person data");
        goto end;
    }
    
    // 添加total字段
    cJSON_AddNumberToObject(data, "total", person_list.count);
    
    // 遍历查询结果，为每个人物项创建JSON对象
    for (int i = 0; i < person_list.count; i++) {
        cJSON *face_item = cJSON_CreateObject();
        if (face_item == NULL) {
            continue;
        }
        
        // 添加人物信息
        cJSON_AddStringToObject(face_item, "croppedImage", person_list.persons[i].croppedImage);
        cJSON_AddNumberToObject(face_item, "photoId", person_list.persons[i].photoId);
        cJSON_AddStringToObject(face_item, "photoName", person_list.persons[i].photoName);
        cJSON_AddStringToObject(face_item, "info", person_list.persons[i].info);
        cJSON_AddNumberToObject(face_item, "regTime", person_list.persons[i].regTime);
        
        // 将人物项添加到Face数组
        cJSON_AddItemToArray(face_array, face_item);
    }
    
    // 释放查询结果内存
    free_person_list(&person_list);
    
    // 将Face数组添加到data对象
    cJSON_AddItemToObject(data, "Face", face_array);
    
    // 添加字段到返回的JSON对象
    cJSON_AddStringToObject(response, "Event", "Face");
    cJSON_AddItemToObject(response, "data", data);
    cJSON_AddNumberToObject(response, "Status", 0);
    cJSON_AddStringToObject(response, "Message", "Success");
    
    // 转换为JSON字符串
    json_string = cJSON_Print(response);
    if (json_string == NULL) {
        goto end;
    }
    
    // 发送响应
    result = send_http_response(socket_fd, json_string);

end:
    if (response) {
        cJSON_Delete(response);
    }
    if (json_string) {
        free(json_string);
    }
    
    return result;
}

int Face_Register_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    PersonInfo person_info = {0};
    char image_path[128] = {0};
    char *image_data = NULL;
    unsigned char *decoded_data = NULL;
    int decoded_len = 0;
    int result = 1;
    FILE *img_file = NULL;
    time_t current_time;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Face_Register", 1, socket_fd, "Failed to get Data");
        return result;
    }

    // 获取 name
    item = cJSON_GetObjectItem(root, "name");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get name");
        general_reply("Face_Register", 1, socket_fd, "Failed to get name");
        return 1;
    } else {
        strncpy(person_info.photoName, item->valuestring, sizeof(person_info.photoName) - 1);
    }

    // 获取 info
    item = cJSON_GetObjectItem(root, "info");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(person_info.info, item->valuestring, sizeof(person_info.info) - 1);
    }

    // 获取 imageBase64
    item = cJSON_GetObjectItem(root, "imageBase64");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get imageBase64");
        general_reply("Face_Register", 1, socket_fd, "Failed to get imageBase64");
        return 1;
    }
    
    image_data = item->valuestring;

    snprintf(image_path, sizeof(image_path), "%s.jpg", person_info.photoName);
    snprintf(person_info.croppedImage, sizeof(person_info.croppedImage), "/userdata/face_image/%s", image_path);

    // 分配解码缓冲区
    int base64_len = strlen(image_data);
    int max_decoded_len = (base64_len * 3) / 4 + 4;
    decoded_data = malloc(max_decoded_len);
    if (decoded_data == NULL) {
        LOG_ERROR("Failed to allocate memory for decoded image data");
        general_reply("Face_Register", 1, socket_fd, "Memory allocation failed");
        return 1;
    }
    
    // 解码Base64数据
    decoded_len = base64_decode(image_data, decoded_data, max_decoded_len);
    if (decoded_len <= 0) {
        LOG_ERROR("Failed to decode base64 image data");
        general_reply("Face_Register", 1, socket_fd, "Failed to decode image data");
        free(decoded_data);
        return 1;
    }

    // 创建目录（如果不存在）
    system("mkdir -p /userdata/face_image");

    // 保存图像文件
    img_file = fopen(person_info.croppedImage, "wb");
    if (img_file == NULL) {
        LOG_ERROR("Failed to create image file: %s", person_info.croppedImage);
        general_reply("Face_Register", 1, socket_fd, "Failed to save image file");
        free(decoded_data);
        return 1;
    }
    
    // 写入解码后的数据
    if (fwrite(decoded_data, 1, decoded_len, img_file) != decoded_len) {
        LOG_ERROR("Failed to write image data to file");
        general_reply("Face_Register", 1, socket_fd, "Failed to write image file");
        fclose(img_file);
        free(decoded_data);
        return 1;
    }
    
    fclose(img_file);
    free(decoded_data);

    // 设置其他信息 id由数据库自动生成
    time(&current_time);
    person_info.regTime = (long long)current_time;
    
    // 打印调试信息
    LOG_DEBUG("Registering face: name=%s, info=%s, image_path=%s", 
              person_info.photoName, person_info.info, person_info.croppedImage);
    
    // 将人脸信息添加到数据库
    if (add_person(&person_info) != 0) {
        LOG_ERROR("Failed to add person to database");
        general_reply("Face_Register", 1, socket_fd, "Failed to add person to database");
        // 删除已创建的文件
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "rm -f %s", person_info.croppedImage);
        system(cmd);
        return 1;
    }
    
    // 发送成功响应
    result = general_reply("Face_Register", 0, socket_fd, NULL);

    return result;
}

int Face_Del_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    int photoId = 0;
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Face_Del", 1, socket_fd, "Failed to get Data");
        return result;
    }

    // 获取 photoId
    item = cJSON_GetObjectItem(root, "photoId");
    if (item == NULL || (!cJSON_IsString(item) && !cJSON_IsNumber(item))) {
        LOG_ERROR("Failed to get photoId");
        general_reply("Face_Del", 1, socket_fd, "Failed to get photoId");
        return 1;
    }
    
    if (cJSON_IsString(item)) {
        photoId = atoi(item->valuestring);
    } else {
        photoId = item->valueint;
    }

    // 打印解析到的信息（用于调试）
    LOG_DEBUG("photoId: %d", photoId);
    
    // 先查询要删除的人脸记录，获取图片路径
    PersonList person_list = {0};
    char image_to_delete[256] = {0};
    int image_found = 0;
    
    if (query_persons_to_struct(&person_list) == 0) {
        for (int i = 0; i < person_list.count; i++) {
            if (person_list.persons[i].photoId == photoId) {
                // 保存要删除的图片路径
                if (strlen(person_list.persons[i].croppedImage) > 0) {
                    strncpy(image_to_delete, person_list.persons[i].croppedImage, sizeof(image_to_delete) - 1);
                    image_found = 1;
                }
                break;
            }
        }
        free_person_list(&person_list);
    }
    
    // 先删除数据库中的人脸记录
    if (delete_person_by_id(photoId) != 0) {
        LOG_ERROR("Failed to delete person from database");
        general_reply("Face_Del", 1, socket_fd, "Failed to delete person from database");
        return 1;
    }
    
    // 数据库删除成功后再删除图片文件
    if (image_found) {
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "rm -f %s", image_to_delete);
        system(cmd);
        LOG_DEBUG("Deleted image file: %s", image_to_delete);
    }

    // 发送成功响应
    result = general_reply("Face_Del", 0, socket_fd, NULL);

    return result;
}

int Task_Config_Get_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    cJSON *response_root = NULL;
    cJSON *data_obj = NULL;
    cJSON *base_alg_array = NULL;
    cJSON *alg_item = NULL;
    cJSON *rule_property_array = NULL;
    cJSON *rule_item = NULL;
    cJSON *points_array = NULL;
    cJSON *rule_points_array = NULL;
    cJSON *point_item = NULL;
    char AlgTaskSession[64] = {0};
    TaskConfig task_config = {0};
    int result = 1;
    char *json_string = NULL;
    int i;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to get Data");
        return 1;
    }

    // 获取 AlgTaskSession
    item = cJSON_GetObjectItem(root, "AlgTaskSession");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get AlgTaskSession");
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to get AlgTaskSession");
        return 1;
    } else {
        strncpy(AlgTaskSession, item->valuestring, sizeof(AlgTaskSession) - 1);
    }

    tcp_client_send(CONFIG_TASK_SNAP);

    // 使用query_task_config_by_session查询数据库
    result = query_task_config_by_session(AlgTaskSession, &task_config);
    if (result != 0) {
        LOG_ERROR("Failed to query task config from database, result: %d", result);
        general_reply("Task_Config_Get", 1, socket_fd, "/userdata/zoneconfig/streamSnap.jpeg");
        return 1;
    }

    // 构建响应JSON
    response_root = cJSON_CreateObject();
    if (response_root == NULL) {
        LOG_ERROR("Failed to create response JSON object");
        general_reply("Task_Config_Get", 1, socket_fd, "/userdata/zoneconfig/streamSnap.jpeg");
        return 1;
    }

    cJSON_AddStringToObject(response_root, "Event", "Task_Config_Get");
    cJSON_AddNumberToObject(response_root, "Status", 0);
    cJSON_AddStringToObject(response_root, "Message", "");

    // 创建data对象
    data_obj = cJSON_CreateObject();
    if (data_obj == NULL) {
        LOG_ERROR("Failed to create data JSON object");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
        return 1;
    }
    cJSON_AddItemToObject(response_root, "data", data_obj);

    // 添加基本字段到data对象
    cJSON_AddStringToObject(data_obj, "AlgTaskSession", task_config.AlgTaskSession);
    cJSON_AddStringToObject(data_obj, "MediaName", task_config.MediaName);
    cJSON_AddStringToObject(data_obj, "TaskDesc", task_config.TaskDesc);
    cJSON_AddStringToObject(data_obj, "TasKConfigImg", "/userdata/zoneconfig/streamSnap.jpeg");

    // 创建BaseAlgItem数组
    base_alg_array = cJSON_CreateArray();
    if (base_alg_array == NULL) {
        LOG_ERROR("Failed to create BaseAlgItem array");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
        return 1;
    }
    cJSON_AddItemToObject(data_obj, "BaseAlgItem", base_alg_array);

    // 添加算法项到BaseAlgItem数组
    alg_item = cJSON_CreateObject();
    if (alg_item == NULL) {
        LOG_ERROR("Failed to create algorithm item");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
        return 1;
    }
    cJSON_AddItemToArray(base_alg_array, alg_item);
    
    cJSON_AddNumberToObject(alg_item, "AlgId", task_config.alg_item.AlgId);
    cJSON_AddStringToObject(alg_item, "Name", task_config.alg_item.Name);

    // 创建RuleProperty数组
    rule_property_array = cJSON_CreateArray();
    if (rule_property_array == NULL) {
        LOG_ERROR("Failed to create RuleProperty array");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
        return 1;
    }
    cJSON_AddItemToObject(data_obj, "RuleProperty", rule_property_array);

    // 添加规则属性到RuleProperty数组
    rule_item = cJSON_CreateObject();
    if (rule_item == NULL) {
        LOG_ERROR("Failed to create rule property item");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
        return 1;
    }
    cJSON_AddItemToArray(rule_property_array, rule_item);

    cJSON_AddStringToObject(rule_item, "RuleId", task_config.rule_property.RuleId);
    cJSON_AddNumberToObject(rule_item, "RuleType", task_config.rule_property.RuleType);
    cJSON_AddStringToObject(rule_item, "RuleTypeName", task_config.rule_property.RuleTypeName);
    cJSON_AddNumberToObject(rule_item, "Baseelevation", task_config.rule_property.Baseelevation);

    // 添加新增的水尺相关字段
    cJSON_AddNumberToObject(rule_item, "Watermark", task_config.rule_property.Watermark);
    cJSON_AddNumberToObject(rule_item, "TimeInterval", task_config.rule_property.TimeInterval);
    cJSON_AddNumberToObject(rule_item, "WaterHighAlarm", task_config.rule_property.WaterHighAlarm);
    cJSON_AddNumberToObject(rule_item, "WaterHighWarn", task_config.rule_property.WaterHighWarn);
    cJSON_AddNumberToObject(rule_item, "WaterLowAlarm", task_config.rule_property.WaterLowAlarm);
    cJSON_AddNumberToObject(rule_item, "WaterLowWarn", task_config.rule_property.WaterLowWarn);

    // 创建Points数组
    points_array = cJSON_CreateArray();
    if (points_array == NULL) {
        LOG_ERROR("Failed to create Points array");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
        return 1;
    }
    cJSON_AddItemToObject(rule_item, "Points", points_array);

    // 添加点到Points数组
    for (i = 0; i < task_config.rule_property.PointsCount; i++) {
        point_item = cJSON_CreateObject();
        if (point_item == NULL) {
            LOG_ERROR("Failed to create point item");
            cJSON_Delete(response_root);
            general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
            return 1;
        }
        cJSON_AddItemToArray(points_array, point_item);
        
        cJSON_AddNumberToObject(point_item, "X", task_config.rule_property.Points[i].X);
        cJSON_AddNumberToObject(point_item, "Y", task_config.rule_property.Points[i].Y);
    }

    // 创建RulePoints数组
    rule_points_array = cJSON_CreateArray();
    if (rule_points_array == NULL) {
        LOG_ERROR("Failed to create RulePoints array");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
        return 1;
    }
    cJSON_AddItemToObject(rule_item, "RulePoints", rule_points_array);

    // 添加点到RulePoints数组
    for (i = 0; i < task_config.rule_property.RulePointsCount; i++) {
        point_item = cJSON_CreateObject();
        if (point_item == NULL) {
            LOG_ERROR("Failed to create rule point item");
            cJSON_Delete(response_root);
            general_reply("Task_Config_Get", 1, socket_fd, "Failed to create response data");
            return 1;
        }
        cJSON_AddItemToArray(rule_points_array, point_item);
        
        cJSON_AddNumberToObject(point_item, "X", task_config.rule_property.RulePoints[i].X);
        cJSON_AddNumberToObject(point_item, "Y", task_config.rule_property.RulePoints[i].Y);
    }

    // 转换为JSON字符串并发送
    json_string = cJSON_Print(response_root);
    if (json_string == NULL) {
        LOG_ERROR("Failed to print JSON response");
        cJSON_Delete(response_root);
        general_reply("Task_Config_Get", 1, socket_fd, "Failed to generate response");
        return 1;
    }

    result = send_http_response(socket_fd, json_string);

    // 清理资源
    if (json_string) {
        free(json_string);
    }
    if (response_root) {
        cJSON_Delete(response_root);
    }

    return result;
}

int Config_Fetch_handler(cJSON *root, int socket_fd) {
    cJSON *response = NULL;
    cJSON *data = NULL;
    cJSON *config_array = NULL;
    char *json_string = NULL;
    int result = 1;

    // 创建响应JSON对象
    response = cJSON_CreateObject();
    if (response == NULL) {
        LOG_ERROR("Failed to create response JSON object");
        goto end;
    }

    // 直接创建配置数组作为data
    data = cJSON_CreateArray();
    if (data == NULL) {
        LOG_ERROR("Failed to create data JSON array");
        goto end;
    }

    // 查询所有配置
    ConfigList config_list = {0};
    int query_result = query_all_configs(&config_list);
    
    if (query_result == 0) {
        // 遍历所有配置项，直接添加到data数组中
        for (int i = 0; i < config_list.count; i++) {
            cJSON *config_item = cJSON_CreateObject();
            if (config_item == NULL) {
                continue;
            }
            cJSON_AddNumberToObject(config_item, "Id", config_list.configs[i].Id);
            cJSON_AddStringToObject(config_item, "ConfigKey", config_list.configs[i].ConfigKey);
            cJSON_AddStringToObject(config_item, "ConfigValue", config_list.configs[i].ConfigValue);
            cJSON_AddStringToObject(config_item, "ConfigDesc", config_list.configs[i].ConfigDesc);
            cJSON_AddNumberToObject(config_item, "ConfigType", config_list.configs[i].ConfigType);

            cJSON_AddItemToArray(data, config_item);  // 直接添加到data数组
        }

        // 释放查询结果内存
        free_config_list(&config_list);
    }

    // 添加字段到返回的JSON对象
    cJSON_AddStringToObject(response, "Event", "Config_Fetch");
    cJSON_AddItemToObject(response, "data", data);  // data现在是数组
    cJSON_AddNumberToObject(response, "Status", 0);
    cJSON_AddStringToObject(response, "Message", "success");

    // 转换为JSON字符串
    json_string = cJSON_Print(response);
    if (json_string == NULL) {
        LOG_ERROR("Failed to print JSON response");
        goto end;
    }

    // 发送响应
    result = send_http_response(socket_fd, json_string);

end:
    if (response) {
        cJSON_Delete(response);
    }
    if (json_string) {
        free(json_string);
    }

    return result;
}

int Config_Update_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    ConfigInfo config_info = {0};
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Config_Update", 1, socket_fd, "Failed to get Data");
        return result;
    }

    // 获取 Id
    item = cJSON_GetObjectItem(root, "Id");
    if (item != NULL && cJSON_IsNumber(item)) {
        config_info.Id = item->valueint;
    }

    // 获取 ConfigKey
    item = cJSON_GetObjectItem(root, "ConfigKey");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(config_info.ConfigKey, item->valuestring, sizeof(config_info.ConfigKey) - 1);
    }

    // 获取 ConfigValue
    item = cJSON_GetObjectItem(root, "ConfigValue");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(config_info.ConfigValue, item->valuestring, sizeof(config_info.ConfigValue) - 1);
    }

    // 获取 ConfigDesc
    item = cJSON_GetObjectItem(root, "ConfigDesc");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(config_info.ConfigDesc, item->valuestring, sizeof(config_info.ConfigDesc) - 1);
    }

    // 获取 ConfigType
    item = cJSON_GetObjectItem(root, "ConfigType");
    if (item != NULL && cJSON_IsNumber(item)) {
        config_info.ConfigType = item->valueint;
    }

    // 打印解析到的信息（用于调试）
    LOG_DEBUG("ConfigKey: %s, ConfigValue: %s, ConfigDesc: %s, ConfigType: %d, Id: %d", 
              config_info.ConfigKey, config_info.ConfigValue, config_info.ConfigDesc, config_info.ConfigType, config_info.Id);

    // 更新数据库中的唯一配置记录
    if (modify_config_by_id(&config_info) != 0) {
        LOG_ERROR("Failed to update config in database");
        general_reply("Config_Update", 1, socket_fd, "Failed to update config in database");
        return 1;
    }

    // 发送成功响应
    result = general_reply("Config_Update", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_GLOBAL);

    return result;
}

int Config_Add_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    ConfigInfo config_info = {0};
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Config_Add", 1, socket_fd, "Failed to get Data");
        return result;
    }

    // 获取 Id
    item = cJSON_GetObjectItem(root, "Id");
    if (item != NULL && cJSON_IsNumber(item)) {
        config_info.Id = item->valueint;
    }

    // 获取 ConfigKey
    item = cJSON_GetObjectItem(root, "ConfigKey");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get ConfigKey");
        general_reply("Config_Add", 1, socket_fd, "Failed to get ConfigKey");
        return 1;
    } else {
        strncpy(config_info.ConfigKey, item->valuestring, sizeof(config_info.ConfigKey) - 1);
    }

    // 获取 ConfigValue
    item = cJSON_GetObjectItem(root, "ConfigValue");
    if (item == NULL || !cJSON_IsString(item)) {
        LOG_ERROR("Failed to get ConfigValue");
        general_reply("Config_Add", 1, socket_fd, "Failed to get ConfigValue");
        return 1;
    } else {
        strncpy(config_info.ConfigValue, item->valuestring, sizeof(config_info.ConfigValue) - 1);
    }

    // 获取 ConfigDesc
    item = cJSON_GetObjectItem(root, "ConfigDesc");
    if (item != NULL && cJSON_IsString(item)) {
        strncpy(config_info.ConfigDesc, item->valuestring, sizeof(config_info.ConfigDesc) - 1);
    }

    // 获取 ConfigType
    item = cJSON_GetObjectItem(root, "ConfigType");
    if (item != NULL && cJSON_IsNumber(item)) {
        config_info.ConfigType = item->valueint;
    }

    // 打印解析到的信息
    LOG_DEBUG("ConfigKey: %s, ConfigValue: %s, ConfigDesc: %s, ConfigType: %d, Id: %d", 
        config_info.ConfigKey, config_info.ConfigValue, config_info.ConfigDesc, config_info.ConfigType, config_info.Id);

    // 向数据库添加配置
    if (add_config(&config_info) != 0) {
        LOG_ERROR("Failed to add config to database");
        general_reply("Config_Add", 1, socket_fd, "Failed to add config to database");
        return 1;
    }

    // 发送成功响应
    result = general_reply("Config_Add", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_GLOBAL);

    return result;
}

int Config_Del_handler(cJSON *root, int socket_fd) {
    cJSON *item = NULL;
    int Id = 0;
    int result = 1;

    if(root == NULL) {
        LOG_ERROR("root is null");
        general_reply("Config_Del", 1, socket_fd, "Failed to get Data");
        return result;
    }

    // 获取 Id
    item = cJSON_GetObjectItem(root, "Id");
    if (item != NULL && cJSON_IsNumber(item)) {
        Id = item->valueint;
    }

    // 打印解析到的信息（用于调试）
    LOG_DEBUG("Id: %d", Id);
    
    // 从数据库删除配置
    if (delete_config_by_Id(Id) != 0) {
        LOG_ERROR("Failed to delete config from database");
        general_reply("Config_Del", 1, socket_fd, "Failed to delete config from database");
        return 1;
    }

    // 发送成功响应
    result = general_reply("Config_Del", 0, socket_fd, NULL);
    tcp_client_send(CONFIG_GLOBAL);

    return result;
}

OperationHandler operation_handlers[] = {
    {"Login", Login_handler},
    {"Info", Info_handler},
    {"Network_Query", Network_Query_handler},
    {"Network_Set", Network_Set_handler},
    {"Media_Fetch", Media_Fetch_handler},
    {"Media_Add", Media_Add_handler},
    {"Media_Del", Media_Del_handler},
    {"Media_Modify", Media_Modify_handler},
    {"Task_Fetch", Task_Fetch_handler},
    {"Task_Add", Task_Add_handler},
    {"Task_Del", Task_Del_handler},
    {"Task_Modify", Task_Modify_handler},
    {"Task_Control", Task_Control_handler},
    {"Task_Config", Task_Config_handler},
    {"Reboot", Reboot_handler},
    {"Time_Sync", Time_Sync_handler},
    {"Upgrade", Upgrade_handler},
    {"Passwd", Passwd_handler},
    {"Task_snap", Task_snap_handler},
    {"Alarm_Fetch", Alarm_Fetch_handler},
    {"Alarm_Del", Alarm_Del_handler},
    {"Alarm_Clear", Alarm_Clear_handler},
    {"Face", Face_Query_handler},
    {"Face_Register", Face_Register_handler},
    {"Face_Del", Face_Del_handler},
    {"Task_Config_Get", Task_Config_Get_handler},
    {"Config_Fetch", Config_Fetch_handler},
    {"Config_Update", Config_Update_handler},
    {"Config_Add", Config_Add_handler},
    {"Config_Del", Config_Del_handler},
};

int num_operation_handlers = sizeof(operation_handlers) / sizeof(operation_handlers[0]);

int process_operation(const char *operation, cJSON *root, int socket_fd) {
    LOG_DEBUG("Operation: %s", operation);
    for (int i = 0; i < num_operation_handlers; i++) {
        if (strcmp(operation_handlers[i].operation, operation) == 0) {
            return operation_handlers[i].func(root, socket_fd);
        }
    }
    return 1;
}

int message_handle_data(char *operation, char *json_string, int socket_fd) {
    // 解析 JSON 字符串以获取操作名
    if(operation == NULL) {
        cJSON *root = cJSON_Parse(json_string);
        if (root == NULL) {
            LOG_ERROR("Failed to parse JSON string");
            json_error_reply(socket_fd);
            return 1;
        }
        cJSON *operate_item = cJSON_GetObjectItem(root, "Event");
        if (cJSON_IsString(operate_item) && (operate_item->valuestring != NULL)) {
            const char *operations = operate_item->valuestring;
            // 调用相应的处理函数
            if (process_operation(operations, root, socket_fd) != 0) {
                LOG_ERROR("Failed to process operations: %s", operations);
                json_error_reply(socket_fd);
            }
        } else {
            LOG_ERROR("Failed to get Event field"); // 修正字段名
            json_error_reply(socket_fd);
        }


        cJSON_Delete(root);
        return 0;
    }
    else {
        if(json_string == NULL) {
            if (process_operation(operation, NULL, socket_fd) != 0) {
                LOG_ERROR("Failed to process operation: %s", operation);
                json_error_reply(socket_fd);
            }
        }
        else{
            cJSON *root = cJSON_Parse(json_string);
            if (root == NULL) {
                LOG_ERROR("Failed to parse JSON string");
                json_error_reply(socket_fd);
                return 1;
            }
            if (process_operation(operation, root, socket_fd) != 0) {
                LOG_ERROR("Failed to process operation: %s", operation);
                json_error_reply(socket_fd);
            }
        }
    }
}