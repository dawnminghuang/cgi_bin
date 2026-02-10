#ifndef SQL_H
#define SQL_H

#include "sqlite3.h"
#include "api.h"

// Media结构体定义
typedef struct {
    int id;
    char MediaName[32];
    char MediaDesc[256];
    char MediaUrl[128];
    int ProtocolType;
    int StatusType;
    char StatusLabel[128];
} MediaInfo;

// Media列表结构体定义
typedef struct {
    MediaInfo* medias;
    int count;
    int capacity;
} MediaList;

// Task算法信息结构体
typedef struct {
    int AlgId;
    char Name[32];
} TaskAlgorithm;

// Task信息结构体
typedef struct {
    int id;
    char AlgTaskSession[64];
    char MediaName[32];
    char TaskDesc[256];
    char Week[32];
    char Start_Time[32];
    char End_Time[32];
    int StatusType;
    char StatusLabel[128];
    int AlgorithmCount;  // 实际配置的算法数量
    // 算法信息
    TaskAlgorithm Alg1;
    TaskAlgorithm Alg2;
    TaskAlgorithm Alg3;
} TaskInfo;

// Task列表结构体
typedef struct {
    TaskInfo* tasks;
    int count;
    int capacity;
} TaskList;

// Task配置信息结构体
typedef struct {
    char AlgTaskSession[64];
    char MediaName[32];
    char TaskDesc[256];
    // BaseAlgItem信息（暂时只支持一个算法项）
    AlgItem alg_item;
    // RuleProperty信息
    RuleProperty rule_property;
} TaskConfig;

// 告警信息结构体
typedef struct {
    int id;
    char AlarmId[64];
    char TaskSession[64];
    char MediaName[64];
    char ImageData[256];
    char Time[32];
    long long TimeStamp;
    int Type;
} AlarmInfo;

// 告警列表结构体
typedef struct {
    AlarmInfo* alarms;
    int all_count;
    int count;
    int capacity;
} AlarmList;

// 告警查询条件结构体
typedef struct {
    char* task_session;     // 任务会话
    char* media_name;       // 媒体名称/通道
    long long begin_time;   // 开始时间
    long long end_time;     // 结束时间
    int type;               // 告警类型
    int type_specified;     // 是否指定了类型(1表示指定，0表示未指定)
    int page;   
} AlarmQueryCondition;

// 人物信息结构体
typedef struct {
    int photoId;
    char photoName[128];
    char croppedImage[256];
    char info[256];
    long long regTime;
} PersonInfo;

// 人物信息列表结构体
typedef struct {
    PersonInfo* persons;
    int count;
    int capacity;
} PersonList;

// 全局参数结构体
typedef struct {
    int Id;
    char ConfigKey[64];
    char ConfigValue[256];
    char ConfigDesc[256];
    int ConfigType;
} ConfigInfo;

// 全局参数列表结构体
typedef struct {
    ConfigInfo* configs;
    int count;
    int capacity;
} ConfigList;

typedef struct {
    int Id;
    char FtpIpaddr[256];   // FTP平台地址
    int FtpPort;           // FTP平台端口
    char FtpId[128];       // 设备地址（设备ID）
    char FtpUser[128];     // FTP平台用户名
    char FtpPasswd[128];   // FTP平台密码
} FtpConfigInfo;

// SZ651平台配置信息结构体
typedef struct {
    int Id;
    char SzIpaddr[256];   // SZ651平台地址
    int SzPort;           // SZ651平台端口
    char SzAddr[256];     // 测站地址
    char SzUser[256];     // 中心站地址
    char SzPasswd[256];   // 中心站密码
} SzConfigInfo;

// SZ651全局参数列表结构体
typedef struct {
    SzConfigInfo* configs;
    int count;
    int capacity;
} SzConfigList;

// 初始化数据库
int init_database(void);

// 关闭数据库
void close_database();

// 执行SQL语句
int execute_sql(const char* sql);

int query_media_to_struct(MediaList* media_list);
int add_media(const MediaInfo* media_info);
int delete_media_by_name(const char* media_name);
int modify_media_by_name(const char* media_name, const MediaInfo* media_info);
void free_media_list(MediaList* media_list);

int query_task_to_struct(TaskList* task_list);
int add_task(const TaskInfo* task_info);
int delete_task_by_session(const char* alg_task_session);
int modify_task_by_session(const char* alg_task_session, const TaskInfo* task_info);
void free_task_list(TaskList* task_list);

int save_task_config(const TaskConfig* task_config);
int del_task_config_by_session(const char* alg_task_session);
int query_task_config_by_session(const char* alg_task_session, TaskConfig* task_config);

int delete_alarm_by_id(const char* alarm_id);
int query_alarms_to_struct(AlarmList* alarm_list, const AlarmQueryCondition* condition);
void free_alarm_list(AlarmList* alarm_list);
int delete_all_alarms(void);

int add_person(const PersonInfo* person_info);
int query_persons_to_struct(PersonList* person_list);
int delete_person_by_id(int photoId);
void free_person_list(PersonList* personlist);

int add_config(const ConfigInfo* config_info);
int delete_config_by_Id(const int Id);
int modify_config_by_id(const ConfigInfo* config_info);
int query_all_configs(ConfigList* config_list);
void free_config_list(ConfigList* config_list);

int update_ftp_config(const FtpConfigInfo* ftp_config);
int query_ftp_config(FtpConfigInfo* ftp_config);

int query_all_sz651_configs(SzConfigList* sz651_list);
int add_sz651_config(const SzConfigInfo* sz651_info);
int delete_sz651_config_by_id(int id);
int modify_sz651_config_by_id(const SzConfigInfo* sz651_info);
void free_sz651_config_list(SzConfigList* sz651_list);

#endif