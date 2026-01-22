#ifndef API_H
#define API_H

// 定义算法项结构体
typedef struct {
    int AlgId;
    char Name[128];
} AlgItem;

// 定义点坐标结构体
typedef struct {
    double X;
    double Y;
} Point;

typedef struct {
    char RuleId[32];
    int RuleType;
    char RuleTypeName[128];
    double Baseelevation;
    Point Points[4];         // 最多4个点
    Point RulePoints[4];
    int PointsCount;
    int RulePointsCount;
    // 新增水尺相关字段
    double Watermark;         // 水尺读数
    int TimeInterval;         // 抓拍间隔（分钟）
    double WaterHighAlarm;    // 水位报警上限（米）
    double WaterHighWarn;     // 水位预警上限（米）
    double WaterLowAlarm;     // 水位报警下限（米）
    double WaterLowWarn;      // 水位预警下限（米）
} RuleProperty;

#define CONFIG_MEDIA "CONFIG_MEDIA"
#define CONFIG_TASK "CONFIG_TASK"
#define CONFIG_TASK_CONFIG  "CONFIG_TASK_CONFIG"
#define CONFIG_TASK_SNAP "CONFIG_TASK_SNAP"
#define CONFIG_GLOBAL "CONFIG_GLOBAL"

int json_error_reply(int socket_fd);

int general_reply(char *Event, int status, int socket_fd, char *message);

int message_handle_data(char *operation, char *json_string, int socket_fd);

#endif