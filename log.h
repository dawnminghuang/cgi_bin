#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>

// 日志级别定义
typedef enum {
    LOGS_DEBUG,
    LOGS_INFO,
    LOGS_WARN,
    LOGS_ERROR
} LogLevel;

// 日志初始化函数
void log_init(LogLevel level);

// 日志记录函数
void log_message(LogLevel level, const char *function, int line, const char *format, ...);

// 宏定义，简化日志记录
#define LOG_DEBUG(fmt, ...) log_message(LOGS_DEBUG, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_message(LOGS_INFO, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_message(LOGS_WARN, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_message(LOGS_ERROR, __func__, __LINE__, fmt, ##__VA_ARGS__)

#endif // LOG_H