#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include "log.h"

static FILE *log_file = NULL;
static const char *filename = "/var/log/cgi_bin/cgi.log";
static LogLevel current_log_level = LOGS_INFO; // 默认日志等级为 INFO

// 初始化日志文件
void log_init(LogLevel level) {
    log_file = fopen(filename, "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file\n");
        // exit(EXIT_FAILURE);
    }
    current_log_level = level;
    printf("current log level: %d\n", current_log_level);
}

// 日志记录函数
void log_message(LogLevel level, const char *function, int line, const char *format, ...) {
    if (!log_file) {
        fprintf(stderr, "Log file not initialized\n");
        return;
    }

    // 检查日志等级
    if (level < current_log_level) {
        return;
    }

    // 获取当前时间
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // 日志级别字符串
    const char *level_str;
    switch (level) {
        case LOGS_DEBUG: level_str = "DEBUG"; break;
        case LOGS_INFO: level_str = "INFO"; break;
        case LOGS_WARN: level_str = "WARN"; break;
        case LOGS_ERROR: level_str = "ERROR"; break;
        default: level_str = "UNKNOWN"; break;
    }

    // 写入日志文件
    va_list args;
    va_start(args, format);
    fprintf(log_file, "[%s] [%s] [%s:%d] ", time_str, level_str, function, line);
    vfprintf(log_file, format, args);
    fprintf(log_file, "\n");
    va_end(args);

    // 刷新缓冲区
    fflush(log_file);
}

// 关闭日志文件
void log_close() {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}