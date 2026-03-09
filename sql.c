#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sql.h"
#include "log.h"

sqlite3* db = NULL;

static int create_tables() {
    // 创建 Media 表
    const char* sql_media = "CREATE TABLE IF NOT EXISTS Media ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "MediaName TEXT NOT NULL,"
                           "MediaDesc TEXT,"
                           "MediaUrl TEXT,"
                           "ProtocolType INTEGER,"
                           "StatusType INTEGER,"
                           "StatusLabel TEXT);";
    
    int rc = execute_sql(sql_media);
    if (rc != 0) {
        return rc;
    }

    // 创建 Task 表，将算法信息整合到表中，每条Task最多包含3个算法
    const char* sql_task = "CREATE TABLE IF NOT EXISTS Task ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "AlgTaskSession TEXT NOT NULL,"
                            "MediaName TEXT,"
                            "TaskDesc TEXT,"
                            "Week TEXT,"
                            "Start_Time TEXT,"
                            "End_Time TEXT,"
                            "StatusType INTEGER DEFAULT 0,"
                            "StatusLabel TEXT DEFAULT '未运行',"
                            "AlgorithmCount INTEGER DEFAULT 0,"  // 实际配置的算法数量
                            // 算法1信息
                            "Alg1_Id INTEGER,"
                            "Alg1_Name TEXT,"
                            // 算法2信息
                            "Alg2_Id INTEGER,"
                            "Alg2_Name TEXT,"
                            // 算法3信息
                            "Alg3_Id INTEGER,"
                            "Alg3_Name TEXT,"
                            "created_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                            "updated_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";

    rc = execute_sql(sql_task);
    if (rc != 0) {
        return rc;
    }

    // 创建 TaskConfig 表，用于存储任务配置信息（Task_Config）
    const char* sql_task_config = "CREATE TABLE IF NOT EXISTS TaskConfig ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "AlgTaskSession TEXT NOT NULL,"
                                "MediaName TEXT,"
                                "TaskDesc TEXT,"
                                // BaseAlgItem 信息（暂时只支持一个算法项）
                                "AlgId INTEGER,"
                                "AlgName TEXT,"
                                // RuleProperty 信息
                                "RuleId TEXT,"
                                "RuleType INTEGER,"
                                "RuleTypeName TEXT,"
                                "Baseelevation REAL,"
                                // Points 信息（最多4个点）
                                "Point1_X REAL,"
                                "Point1_Y REAL,"
                                "Point2_X REAL,"
                                "Point2_Y REAL,"
                                "Point3_X REAL,"
                                "Point3_Y REAL,"
                                "Point4_X REAL,"
                                "Point4_Y REAL,"
                                // RulePoints 信息（最多4个点）
                                "RulePoint1_X REAL,"
                                "RulePoint1_Y REAL,"
                                "RulePoint2_X REAL,"
                                "RulePoint2_Y REAL,"
                                "RulePoint3_X REAL,"
                                "RulePoint3_Y REAL,"
                                "RulePoint4_X REAL,"
                                "RulePoint4_Y REAL,"
                                // 新增水尺相关字段
                                "Watermark REAL,"              // 水尺读数
                                "TimeInterval INTEGER,"      // 抓拍间隔（分钟）
                                "WaterHighAlarm REAL,"       // 水位报警上限（米）
                                "WaterHighWarn REAL,"        // 水位预警上限（米）
                                "WaterLowAlarm REAL,"        // 水位报警下限（米）
                                "WaterLowWarn REAL,"         // 水位预警下限（米）
                                "created_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";

    rc = execute_sql(sql_task_config);
    if (rc != 0) {
        return rc;
    }

     // 创建 Alarm 表
     const char* sql_alarm = "CREATE TABLE IF NOT EXISTS Alarm ("
     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
     "AlarmId TEXT NOT NULL UNIQUE,"
     "TaskSession TEXT,"
     "MediaName TEXT,"
     "ImageData TEXT,"
     "Time TEXT,"
     "TimeStamp INTEGER,"
     "Type INTEGER,"
     "created_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";

    rc = execute_sql(sql_alarm);
    if (rc != 0) {
        return rc;
    }

    // 创建 Person 表，用于存储人物信息
    const char* sql_person = "CREATE TABLE IF NOT EXISTS Person ("
    "photoId INTEGER PRIMARY KEY AUTOINCREMENT,"
    "photoName TEXT NOT NULL,"
    "croppedImage TEXT,"
    "info TEXT,"
    "regTime INTEGER);";

    rc = execute_sql(sql_person);
    if (rc != 0) {
        return rc;
    }

    // 创建 Config 表，用于存储系统配置信息
    const char* sql_config = "CREATE TABLE IF NOT EXISTS Config ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "ConfigKey TEXT NOT NULL UNIQUE,"
    "ConfigValue TEXT,"
    "ConfigDesc TEXT,"
    "ConfigType INTEGER);";

    rc = execute_sql(sql_config);
    if (rc != 0) {
        return rc;
    }

    // 创建 FtpConfig 表，用于存储FTP配置信息
    const char* sql_ftp_config = "CREATE TABLE IF NOT EXISTS FtpConfig ("
    "Id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "FtpIpaddr TEXT NOT NULL,"
    "FtpPort INTEGER,"
    "FtpId TEXT,"
    "FtpUser TEXT,"
    "FtpPasswd TEXT,"
    "created_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";

    rc = execute_sql(sql_ftp_config);
    if (rc != 0) {
        return rc;
    }

    // 创建 Sz651Config 表，用于存储SZ651平台配置信息
    const char* sql_sz651_config = "CREATE TABLE IF NOT EXISTS Sz651Config ("
    "Id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "SzIpaddr TEXT NOT NULL,"           // SZ651平台地址
    "SzPort INTEGER,"                   // SZ651平台端口
    "SzAddr TEXT,"                      // 测站地址
    "SzUser TEXT,"                      // 中心站地址
    "SzPasswd TEXT,"                    // 中心站密码
    "created_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";

    rc = execute_sql(sql_sz651_config);
    if (rc != 0) {
        return rc;
    }

    return 0; // 添加返回值
}

static int config_init() {
    sqlite3_stmt* stmt;
    const char* sql_count = "SELECT COUNT(*) FROM Config;";
    
    int rc = sqlite3_prepare_v2(db, sql_count, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare count statement: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    rc = sqlite3_step(stmt);
    int count = 0;
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    if (count == 0) {
        // 表为空，插入初始配置数据
        ConfigInfo waterAlarmConfig = {0};
        strncpy(waterAlarmConfig.ConfigKey, "WaterAlarmSignalSec", sizeof(waterAlarmConfig.ConfigKey) - 1);
        strncpy(waterAlarmConfig.ConfigValue, "300", sizeof(waterAlarmConfig.ConfigValue) - 1);
        strncpy(waterAlarmConfig.ConfigDesc, "水位数据上报间隔单位秒", sizeof(waterAlarmConfig.ConfigDesc) - 1);
        waterAlarmConfig.ConfigType = 1;
        
        rc = add_config(&waterAlarmConfig);
        if (rc != 0) {
            LOG_ERROR("Failed to insert initial config WaterAlarmSignalSec");
            return rc;
        }
        
        ConfigInfo remoteCloudConfig = {0};
        strncpy(remoteCloudConfig.ConfigKey, "RemoteCloudIp", sizeof(remoteCloudConfig.ConfigKey) - 1);
        strncpy(remoteCloudConfig.ConfigValue, "", sizeof(remoteCloudConfig.ConfigValue) - 1);
        strncpy(remoteCloudConfig.ConfigDesc, "云端管理平台地址", sizeof(remoteCloudConfig.ConfigDesc) - 1);
        remoteCloudConfig.ConfigType = 1;
        
        rc = add_config(&remoteCloudConfig);
        if (rc != 0) {
            LOG_ERROR("Failed to insert initial config RemoteCloudIp");
            return rc;
        }

        ConfigInfo remotePortConfig = {0};
        strncpy(remotePortConfig.ConfigKey, "RemoteCloudPort", sizeof(remotePortConfig.ConfigKey) - 1);
        strncpy(remotePortConfig.ConfigValue, "", sizeof(remotePortConfig.ConfigValue) - 1);
        strncpy(remotePortConfig.ConfigDesc, "云端管理平台端口", sizeof(remotePortConfig.ConfigDesc) - 1);
        remotePortConfig.ConfigType = 1;
        rc = add_config(&remotePortConfig);
        if (rc != 0) {
            LOG_ERROR("Failed to insert initial config RemoteCloudIp");
            return rc;
        }
        

        ConfigInfo manualReadingWaterDataConfig = {0};
        strncpy(manualReadingWaterDataConfig.ConfigKey, "ManualReadingWaterData", sizeof(manualReadingWaterDataConfig.ConfigKey) - 1);
        strncpy(manualReadingWaterDataConfig.ConfigValue, "", sizeof(manualReadingWaterDataConfig.ConfigValue) - 1);
        strncpy(manualReadingWaterDataConfig.ConfigDesc, "人工读水尺读数", sizeof(manualReadingWaterDataConfig.ConfigDesc) - 1);
        manualReadingWaterDataConfig.ConfigType = 1;
        rc = add_config(&manualReadingWaterDataConfig);
        if (rc != 0) {
            LOG_ERROR("Failed to insert initial config ManualReadingWaterData");
            return rc;
        }

        LOG_INFO("Initial configuration inserted successfully");
    } else {
        LOG_INFO("Configuration table already has %d records, skipping initialization", count);
    }
    
    return 0;
}

int init_database(void) {
    int rc = sqlite3_open("/home/test.db", &db);
    
    if (rc) {
        LOG_ERROR("Can't open database: %s",sqlite3_errmsg(db));
        return rc;
    } else {
        LOG_INFO("Opened database successfully");
    }
    
    // 创建表
    create_tables();

    // 初始化配置项
    config_init();

    return 0;
}

void close_database() {
    if (db) {
        sqlite3_close(db);
        db = NULL;
        LOG_INFO("Database closed successfully");
    }
}

int execute_sql(const char* sql) {
    char* errMsg = 0;
    int rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQL error: %s",errMsg);
        sqlite3_free(errMsg);
        return rc;
    }
    
    return 0;
}

int query_media_to_struct(MediaList* media_list) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, MediaName, MediaDesc, MediaUrl, ProtocolType, StatusType, StatusLabel FROM Media;";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to fetch data: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    // 初始化MediaList
    media_list->count = 0;
    media_list->capacity = 10; // 初始容量
    media_list->medias = malloc(sizeof(MediaInfo) * media_list->capacity);
    
    if (!media_list->medias) {
        LOG_ERROR("Failed to allocate memory for media list");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 遍历查询结果
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // 如果需要，扩展容量
        if (media_list->count >= media_list->capacity) {
            media_list->capacity *= 2;
            MediaInfo* temp = realloc(media_list->medias, sizeof(MediaInfo) * media_list->capacity);
            if (!temp) {
                LOG_ERROR("Failed to allocate memory for media list");
                free(media_list->medias);
                sqlite3_finalize(stmt);
                return -1;
            }
            media_list->medias = temp;
        }
        
        // 填充当前MediaInfo结构体
        MediaInfo* media = &media_list->medias[media_list->count];
        media->id = sqlite3_column_int(stmt, 0);
        
        const char* temp_str = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(media->MediaName, temp_str ? temp_str : "", sizeof(media->MediaName) - 1);
        media->MediaName[sizeof(media->MediaName) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 2);
        strncpy(media->MediaDesc, temp_str ? temp_str : "", sizeof(media->MediaDesc) - 1);
        media->MediaDesc[sizeof(media->MediaDesc) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 3);
        strncpy(media->MediaUrl, temp_str ? temp_str : "", sizeof(media->MediaUrl) - 1);
        media->MediaUrl[sizeof(media->MediaUrl) - 1] = '\0';
        
        media->ProtocolType = sqlite3_column_int(stmt, 4);
        media->StatusType = sqlite3_column_int(stmt, 5);
        
        temp_str = (const char*)sqlite3_column_text(stmt, 6);
        strncpy(media->StatusLabel, temp_str ? temp_str : "", sizeof(media->StatusLabel) - 1);
        media->StatusLabel[sizeof(media->StatusLabel) - 1] = '\0';
        
        media_list->count++;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

int add_media(const MediaInfo* media_info) {
    char sql[1024];
    snprintf(sql, sizeof(sql), 
             "INSERT INTO Media (MediaName, MediaDesc, MediaUrl, ProtocolType, StatusType, StatusLabel) "
             "VALUES ('%s', '%s', '%s', %d, %d, '%s');",
             media_info->MediaName[0] ? media_info->MediaName : "",
             media_info->MediaDesc[0] ? media_info->MediaDesc : "",
             media_info->MediaUrl[0] ? media_info->MediaUrl : "",
             media_info->ProtocolType, 
             media_info->StatusType, 
             media_info->StatusLabel[0] ? media_info->StatusLabel : "");
    
    return execute_sql(sql);
}

int delete_media_by_name(const char* media_name) {
    char sql[512];
    snprintf(sql, sizeof(sql), 
             "DELETE FROM Media WHERE MediaName = '%s';",
             media_name && media_name[0] ? media_name : "");
    
    return execute_sql(sql);
}

int modify_media_by_name(const char* media_name, const MediaInfo* media_info) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "UPDATE Media SET MediaName = '%s', MediaDesc = '%s', MediaUrl = '%s', "
             "ProtocolType = %d, StatusType = %d, StatusLabel = '%s' "
             "WHERE MediaName = '%s';",
             media_info->MediaName[0] ? media_info->MediaName : "",
             media_info->MediaDesc[0] ? media_info->MediaDesc : "",
             media_info->MediaUrl[0] ? media_info->MediaUrl : "",
             media_info->ProtocolType, 
             media_info->StatusType, 
             media_info->StatusLabel[0] ? media_info->StatusLabel : "",
             media_name && media_name[0] ? media_name : "");
    
    return execute_sql(sql);
}

void free_media_list(MediaList* media_list) {
    if (media_list && media_list->medias) {
        free(media_list->medias);
        media_list->medias = NULL;
        media_list->count = 0;
        media_list->capacity = 0;
    }
}

int query_task_to_struct(TaskList* task_list) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, AlgTaskSession, MediaName, TaskDesc, Week, Start_Time, End_Time, "
                      "StatusType, StatusLabel, AlgorithmCount, Alg1_Id, Alg1_Name, Alg2_Id, Alg2_Name, Alg3_Id, Alg3_Name "
                      "FROM Task;";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to fetch data: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    // 初始化TaskList
    task_list->count = 0;
    task_list->capacity = 10; // 初始容量
    task_list->tasks = malloc(sizeof(TaskInfo) * task_list->capacity);
    
    if (!task_list->tasks) {
        LOG_ERROR("Failed to allocate memory for task list");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 遍历查询结果
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // 如果需要，扩展容量
        if (task_list->count >= task_list->capacity) {
            task_list->capacity *= 2;
            TaskInfo* temp = realloc(task_list->tasks, sizeof(TaskInfo) * task_list->capacity);
            if (!temp) {
                LOG_ERROR("Failed to reallocate memory for task list");
                free(task_list->tasks);
                sqlite3_finalize(stmt);
                return -1;
            }
            task_list->tasks = temp;
        }
        
        // 填充当前TaskInfo结构体
        TaskInfo* task = &task_list->tasks[task_list->count];
        task->id = sqlite3_column_int(stmt, 0);
        
        const char* temp_str = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(task->AlgTaskSession, temp_str ? temp_str : "", sizeof(task->AlgTaskSession) - 1);
        task->AlgTaskSession[sizeof(task->AlgTaskSession) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 2);
        strncpy(task->MediaName, temp_str ? temp_str : "", sizeof(task->MediaName) - 1);
        task->MediaName[sizeof(task->MediaName) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 3);
        strncpy(task->TaskDesc, temp_str ? temp_str : "", sizeof(task->TaskDesc) - 1);
        task->TaskDesc[sizeof(task->TaskDesc) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 4);
        strncpy(task->Week, temp_str ? temp_str : "", sizeof(task->Week) - 1);
        task->Week[sizeof(task->Week) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 5);
        strncpy(task->Start_Time, temp_str ? temp_str : "", sizeof(task->Start_Time) - 1);
        task->Start_Time[sizeof(task->Start_Time) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 6);
        strncpy(task->End_Time, temp_str ? temp_str : "", sizeof(task->End_Time) - 1);
        task->End_Time[sizeof(task->End_Time) - 1] = '\0';
        
        task->StatusType = sqlite3_column_int(stmt, 7);
        
        temp_str = (const char*)sqlite3_column_text(stmt, 8);
        strncpy(task->StatusLabel, temp_str ? temp_str : "", sizeof(task->StatusLabel) - 1);
        task->StatusLabel[sizeof(task->StatusLabel) - 1] = '\0';
        
        // 实际配置的算法数量
        task->AlgorithmCount = sqlite3_column_int(stmt, 9);
        
        // 算法1信息
        task->Alg1.AlgId = sqlite3_column_int(stmt, 10);
        temp_str = (const char*)sqlite3_column_text(stmt, 11);
        strncpy(task->Alg1.Name, temp_str ? temp_str : "", sizeof(task->Alg1.Name) - 1);
        task->Alg1.Name[sizeof(task->Alg1.Name) - 1] = '\0';
        
        // 算法2信息
        task->Alg2.AlgId = sqlite3_column_int(stmt, 12);
        temp_str = (const char*)sqlite3_column_text(stmt, 13);
        strncpy(task->Alg2.Name, temp_str ? temp_str : "", sizeof(task->Alg2.Name) - 1);
        task->Alg2.Name[sizeof(task->Alg2.Name) - 1] = '\0';
        
        // 算法3信息
        task->Alg3.AlgId = sqlite3_column_int(stmt, 14);
        temp_str = (const char*)sqlite3_column_text(stmt, 15);
        strncpy(task->Alg3.Name, temp_str ? temp_str : "", sizeof(task->Alg3.Name) - 1);
        task->Alg3.Name[sizeof(task->Alg3.Name) - 1] = '\0';
        
        task_list->count++;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

int add_task(const TaskInfo* task_info) {
    char sql[2048];
    snprintf(sql, sizeof(sql),
             "INSERT INTO Task (AlgTaskSession, MediaName, TaskDesc, Week, Start_Time, End_Time, "
             "StatusType, StatusLabel, AlgorithmCount, Alg1_Id, Alg1_Name, Alg2_Id, Alg2_Name, Alg3_Id, Alg3_Name) "
             "VALUES ('%s', '%s', '%s', '%s', '%s', '%s', %d, '%s', %d, %d, '%s', %d, '%s', %d, '%s');",
             task_info->AlgTaskSession[0] ? task_info->AlgTaskSession : "",
             task_info->MediaName[0] ? task_info->MediaName : "",
             task_info->TaskDesc[0] ? task_info->TaskDesc : "",
             task_info->Week[0] ? task_info->Week : "",
             task_info->Start_Time[0] ? task_info->Start_Time : "",
             task_info->End_Time[0] ? task_info->End_Time : "",
             task_info->StatusType,
             task_info->StatusLabel[0] ? task_info->StatusLabel : "未运行",
             task_info->AlgorithmCount,
             task_info->Alg1.AlgId,
             task_info->Alg1.Name[0] ? task_info->Alg1.Name : "",
             task_info->Alg2.AlgId,
             task_info->Alg2.Name[0] ? task_info->Alg2.Name : "",
             task_info->Alg3.AlgId,
             task_info->Alg3.Name[0] ? task_info->Alg3.Name : "");
    
    return execute_sql(sql);
}

int delete_task_by_session(const char* alg_task_session) {
    char sql[512];
    snprintf(sql, sizeof(sql), 
             "DELETE FROM Task WHERE AlgTaskSession = '%s';",
             alg_task_session[0] ? alg_task_session : "");
    
    return execute_sql(sql);
}

int modify_task_by_session(const char* alg_task_session, const TaskInfo* task_info) {
    char sql[2048];
    snprintf(sql, sizeof(sql),
             "UPDATE Task SET "
             "AlgTaskSession = '%s', MediaName = '%s', TaskDesc = '%s', Week = '%s', "
             "Start_Time = '%s', End_Time = '%s', StatusType = %d, StatusLabel = '%s', "
             "AlgorithmCount = %d, Alg1_Id = %d, Alg1_Name = '%s', Alg2_Id = %d, Alg2_Name = '%s', "
             "Alg3_Id = %d, Alg3_Name = '%s' "
             "WHERE AlgTaskSession = '%s';",
             task_info->AlgTaskSession[0] ? task_info->AlgTaskSession : "",
             task_info->MediaName[0] ? task_info->MediaName : "",
             task_info->TaskDesc[0] ? task_info->TaskDesc : "",
             task_info->Week[0] ? task_info->Week : "",
             task_info->Start_Time[0] ? task_info->Start_Time : "",
             task_info->End_Time[0] ? task_info->End_Time : "",
             task_info->StatusType,
             task_info->StatusLabel[0] ? task_info->StatusLabel : "未运行",
             task_info->AlgorithmCount,
             task_info->Alg1.AlgId,
             task_info->Alg1.Name[0] ? task_info->Alg1.Name : "",
             task_info->Alg2.AlgId,
             task_info->Alg2.Name[0] ? task_info->Alg2.Name : "",
             task_info->Alg3.AlgId,
             task_info->Alg3.Name[0] ? task_info->Alg3.Name : "",
             alg_task_session[0] ? alg_task_session : "");
    
    return execute_sql(sql);
}

void free_task_list(TaskList* task_list) {
    if (task_list && task_list->tasks) {
        free(task_list->tasks);
        task_list->tasks = NULL;
        task_list->count = 0;
        task_list->capacity = 0;
    }
}

// 添加TaskConfig相关函数
int save_task_config(const TaskConfig* task_config) {
    // 首先检查是否存在相同的AlgTaskSession
    sqlite3_stmt* stmt;
    char sql_check[256];
    int rc;
    
    snprintf(sql_check, sizeof(sql_check), 
             "SELECT COUNT(*) FROM TaskConfig WHERE AlgTaskSession = '%s';", 
             task_config->AlgTaskSession);
    
    rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    rc = sqlite3_step(stmt);
    int count = 0;
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    // 构建Points字段（最多4个点）
    double point1_x = 0, point1_y = 0;
    double point2_x = 0, point2_y = 0;
    double point3_x = 0, point3_y = 0;
    double point4_x = 0, point4_y = 0;
    
    if (task_config->rule_property.PointsCount > 0) {
        point1_x = task_config->rule_property.Points[0].X;
        point1_y = task_config->rule_property.Points[0].Y;
    }
    if (task_config->rule_property.PointsCount > 1) {
        point2_x = task_config->rule_property.Points[1].X;
        point2_y = task_config->rule_property.Points[1].Y;
    }
    if (task_config->rule_property.PointsCount > 2) {
        point3_x = task_config->rule_property.Points[2].X;
        point3_y = task_config->rule_property.Points[2].Y;
    }
    if (task_config->rule_property.PointsCount > 3) {
        point4_x = task_config->rule_property.Points[3].X;
        point4_y = task_config->rule_property.Points[3].Y;
    }
    
    // 构建RulePoints字段（最多4个点）
    double rule_point1_x = 0, rule_point1_y = 0;
    double rule_point2_x = 0, rule_point2_y = 0;
    double rule_point3_x = 0, rule_point3_y = 0;
    double rule_point4_x = 0, rule_point4_y = 0;
    
    if (task_config->rule_property.RulePointsCount > 0) {
        rule_point1_x = task_config->rule_property.RulePoints[0].X;
        rule_point1_y = task_config->rule_property.RulePoints[0].Y;
    }
    if (task_config->rule_property.RulePointsCount > 1) {
        rule_point2_x = task_config->rule_property.RulePoints[1].X;
        rule_point2_y = task_config->rule_property.RulePoints[1].Y;
    }
    if (task_config->rule_property.RulePointsCount > 2) {
        rule_point3_x = task_config->rule_property.RulePoints[2].X;
        rule_point3_y = task_config->rule_property.RulePoints[2].Y;
    }
    if (task_config->rule_property.RulePointsCount > 3) {
        rule_point4_x = task_config->rule_property.RulePoints[3].X;
        rule_point4_y = task_config->rule_property.RulePoints[3].Y;
    }
    
    char sql[2048];
    if (count > 0) {
        // 更新现有记录
        snprintf(sql, sizeof(sql),
                 "UPDATE TaskConfig SET "
                 "MediaName = '%s', TaskDesc = '%s', "
                 "AlgId = %d, AlgName = '%s', "
                 "RuleId = '%s', RuleType = %d, RuleTypeName = '%s', Baseelevation = %f, "
                 "Point1_X = %f, Point1_Y = %f, "
                 "Point2_X = %f, Point2_Y = %f, "
                 "Point3_X = %f, Point3_Y = %f, "
                 "Point4_X = %f, Point4_Y = %f, "
                 "RulePoint1_X = %f, RulePoint1_Y = %f, "
                 "RulePoint2_X = %f, RulePoint2_Y = %f, "
                 "RulePoint3_X = %f, RulePoint3_Y = %f, "
                 "RulePoint4_X = %f, RulePoint4_Y = %f, "
                 "Watermark = %f, TimeInterval = %d, "
                 "WaterHighAlarm = %f, WaterHighWarn = %f, "
                 "WaterLowAlarm = %f, WaterLowWarn = %f "
                 "WHERE AlgTaskSession = '%s';",
                 task_config->MediaName,
                 task_config->TaskDesc,
                 task_config->alg_item.AlgId,
                 task_config->alg_item.Name,
                 task_config->rule_property.RuleId,
                 task_config->rule_property.RuleType,
                 task_config->rule_property.RuleTypeName,
                 task_config->rule_property.Baseelevation,
                 point1_x, point1_y, point2_x, point2_y, point3_x, point3_y, point4_x, point4_y,
                 rule_point1_x, rule_point1_y, rule_point2_x, rule_point2_y,
                 rule_point3_x, rule_point3_y, rule_point4_x, rule_point4_y,
                 task_config->rule_property.Watermark, task_config->rule_property.TimeInterval,
                 task_config->rule_property.WaterHighAlarm, task_config->rule_property.WaterHighWarn,
                 task_config->rule_property.WaterLowAlarm, task_config->rule_property.WaterLowWarn,
                 task_config->AlgTaskSession);
    } else {
        // 插入新记录
        snprintf(sql, sizeof(sql),
        "INSERT INTO TaskConfig ("
        "AlgTaskSession, MediaName, TaskDesc, "
        "AlgId, AlgName, "
        "RuleId, RuleType, RuleTypeName, Baseelevation,"
        "Point1_X, Point1_Y, Point2_X, Point2_Y, Point3_X, Point3_Y, Point4_X, Point4_Y, "
        "RulePoint1_X, RulePoint1_Y, RulePoint2_X, RulePoint2_Y, "
        "RulePoint3_X, RulePoint3_Y, RulePoint4_X, RulePoint4_Y, "
        "Watermark, TimeInterval, WaterHighAlarm, WaterHighWarn, WaterLowAlarm, WaterLowWarn) "
        "VALUES ('%s', '%s', '%s', %d, '%s', '%s', %d, '%s', %f, "  
        "%f, %f, %f, %f, %f, %f, %f, %f, "
        "%f, %f, %f, %f, %f, %f, %f, %f, "
        "%f, %d, %f, %f, %f, %f);",
        task_config->AlgTaskSession,
        task_config->MediaName,
        task_config->TaskDesc,
        task_config->alg_item.AlgId,
        task_config->alg_item.Name,
        task_config->rule_property.RuleId,
        task_config->rule_property.RuleType,
        task_config->rule_property.RuleTypeName,
        task_config->rule_property.Baseelevation,
        point1_x, point1_y, point2_x, point2_y, point3_x, point3_y, point4_x, point4_y,
        rule_point1_x, rule_point1_y, rule_point2_x, rule_point2_y,
        rule_point3_x, rule_point3_y, rule_point4_x, rule_point4_y,
        task_config->rule_property.Watermark, task_config->rule_property.TimeInterval,
        task_config->rule_property.WaterHighAlarm, task_config->rule_property.WaterHighWarn,
        task_config->rule_property.WaterLowAlarm, task_config->rule_property.WaterLowWarn);
    }
    
    return execute_sql(sql);
}

// ... existing code ...
int query_task_config_by_session(const char* alg_task_session, TaskConfig* task_config) {
    sqlite3_stmt* stmt;
    char sql[512];
    
    // 构造SQL查询语句
    snprintf(sql, sizeof(sql),
             "SELECT AlgTaskSession, MediaName, TaskDesc, "
             "AlgId, AlgName, "
             "RuleId, RuleType, RuleTypeName, Baseelevation, "
             "Point1_X, Point1_Y, Point2_X, Point2_Y, Point3_X, Point3_Y, Point4_X, Point4_Y, "
             "RulePoint1_X, RulePoint1_Y, RulePoint2_X, RulePoint2_Y, "
             "RulePoint3_X, RulePoint3_Y, RulePoint4_X, RulePoint4_Y, "
             "Watermark, TimeInterval, WaterHighAlarm, WaterHighWarn, WaterLowAlarm, WaterLowWarn "
             "FROM TaskConfig WHERE AlgTaskSession = ?;");
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    // 绑定参数
    sqlite3_bind_text(stmt, 1, alg_task_session, -1, SQLITE_STATIC);
    
    // 执行查询
    rc = sqlite3_step(stmt);
    
    if (rc == SQLITE_ROW) {
        // 填充TaskConfig结构体
        const char* temp_str = (const char*)sqlite3_column_text(stmt, 0);
        strncpy(task_config->AlgTaskSession, temp_str ? temp_str : "", sizeof(task_config->AlgTaskSession) - 1);
        task_config->AlgTaskSession[sizeof(task_config->AlgTaskSession) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(task_config->MediaName, temp_str ? temp_str : "", sizeof(task_config->MediaName) - 1);
        task_config->MediaName[sizeof(task_config->MediaName) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 2);
        strncpy(task_config->TaskDesc, temp_str ? temp_str : "", sizeof(task_config->TaskDesc) - 1);
        task_config->TaskDesc[sizeof(task_config->TaskDesc) - 1] = '\0';
        
        // 填充AlgItem信息
        task_config->alg_item.AlgId = sqlite3_column_int(stmt, 3);
        temp_str = (const char*)sqlite3_column_text(stmt, 4);
        strncpy(task_config->alg_item.Name, temp_str ? temp_str : "", sizeof(task_config->alg_item.Name) - 1);
        task_config->alg_item.Name[sizeof(task_config->alg_item.Name) - 1] = '\0';
        
        // 填充RuleProperty信息
        temp_str = (const char*)sqlite3_column_text(stmt, 5);
        strncpy(task_config->rule_property.RuleId, temp_str ? temp_str : "", sizeof(task_config->rule_property.RuleId) - 1);
        task_config->rule_property.RuleId[sizeof(task_config->rule_property.RuleId) - 1] = '\0';
        
        task_config->rule_property.RuleType = sqlite3_column_int(stmt, 6);
        
        temp_str = (const char*)sqlite3_column_text(stmt, 7);
        strncpy(task_config->rule_property.RuleTypeName, temp_str ? temp_str : "", sizeof(task_config->rule_property.RuleTypeName) - 1);
        task_config->rule_property.RuleTypeName[sizeof(task_config->rule_property.RuleTypeName) - 1] = '\0';
        
        task_config->rule_property.Baseelevation = sqlite3_column_double(stmt, 8);
        
        // 填充Points信息
        task_config->rule_property.Points[0].X = sqlite3_column_double(stmt, 9);
        task_config->rule_property.Points[0].Y = sqlite3_column_double(stmt, 10);
        task_config->rule_property.Points[1].X = sqlite3_column_double(stmt, 11);
        task_config->rule_property.Points[1].Y = sqlite3_column_double(stmt, 12);
        task_config->rule_property.Points[2].X = sqlite3_column_double(stmt, 13);
        task_config->rule_property.Points[2].Y = sqlite3_column_double(stmt, 14);
        task_config->rule_property.Points[3].X = sqlite3_column_double(stmt, 15);
        task_config->rule_property.Points[3].Y = sqlite3_column_double(stmt, 16);
        
        // 计算实际点的数量（非零点）
        task_config->rule_property.PointsCount = 0;
        for (int i = 0; i < 4; i++) {
            if (task_config->rule_property.Points[i].X != 0 || task_config->rule_property.Points[i].Y != 0) {
                task_config->rule_property.PointsCount++;
            }
        }
        
        // 填充RulePoints信息
        task_config->rule_property.RulePoints[0].X = sqlite3_column_double(stmt, 17);
        task_config->rule_property.RulePoints[0].Y = sqlite3_column_double(stmt, 18);
        task_config->rule_property.RulePoints[1].X = sqlite3_column_double(stmt, 19);
        task_config->rule_property.RulePoints[1].Y = sqlite3_column_double(stmt, 20);
        task_config->rule_property.RulePoints[2].X = sqlite3_column_double(stmt, 21);
        task_config->rule_property.RulePoints[2].Y = sqlite3_column_double(stmt, 22);
        task_config->rule_property.RulePoints[3].X = sqlite3_column_double(stmt, 23);
        task_config->rule_property.RulePoints[3].Y = sqlite3_column_double(stmt, 24);
        
        // 计算实际RulePoints的数量（非零点）
        task_config->rule_property.RulePointsCount = 0;
        for (int i = 0; i < 4; i++) {
            if (task_config->rule_property.RulePoints[i].X != 0 || task_config->rule_property.RulePoints[i].Y != 0) {
                task_config->rule_property.RulePointsCount++;
            }
        }
        
        // 填充新增的水尺相关字段 (列索引从25开始)
        task_config->rule_property.Watermark = sqlite3_column_double(stmt, 25);
        task_config->rule_property.TimeInterval = sqlite3_column_int(stmt, 26);
        task_config->rule_property.WaterHighAlarm = sqlite3_column_double(stmt, 27);
        task_config->rule_property.WaterHighWarn = sqlite3_column_double(stmt, 28);
        task_config->rule_property.WaterLowAlarm = sqlite3_column_double(stmt, 29);
        task_config->rule_property.WaterLowWarn = sqlite3_column_double(stmt, 30);
        
        sqlite3_finalize(stmt);
        return 0; // 成功找到并填充数据
    } else if (rc == SQLITE_DONE) {
        // 没有找到匹配的记录
        sqlite3_finalize(stmt);
        return 1; // 未找到记录
    } else {
        // 查询出错
        LOG_ERROR("Failed to query TaskConfig: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }
}

int del_task_config_by_session(const char* alg_task_session) {
    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM TaskConfig WHERE AlgTaskSession = '%s';", alg_task_session);
    return execute_sql(sql);
}

int query_alarms_to_struct(AlarmList* alarm_list, const AlarmQueryCondition* condition) {
    sqlite3_stmt* stmt;
    char sql[1024];
    
    // 构建基础SQL查询
    strcpy(sql, "SELECT id, AlarmId, TaskSession, MediaName, ImageData, Time, TimeStamp, Type FROM Alarm WHERE 1=1");
    
    // 添加任务会话过滤条件
    if (condition->task_session && condition->task_session[0]) {
        strcat(sql, " AND TaskSession = ?");
    }
    
    // 添加媒体名称(通道)过滤条件
    if (condition->media_name && condition->media_name[0]) {
        strcat(sql, " AND MediaName = ?");
    }
    
    // 添加开始时间过滤条件
    if (condition->begin_time > 0) {
        strcat(sql, " AND TimeStamp >= ?");
    }
    
    // 添加结束时间过滤条件
    if (condition->end_time > 0) {
        strcat(sql, " AND TimeStamp <= ?");
    }
    
    // 添加类型过滤条件（只有当类型被指定时才添加）
    if (condition->type_specified) {
        strcat(sql, " AND Type = ?");
    }
    
    // 按时间戳降序排列（最新的告警在前）
    strcat(sql, " ORDER BY TimeStamp DESC");
    
    // 添加分页限制，每页最多12个条目
    char limit_clause[64];
    int offset = condition->page * 12;
    snprintf(limit_clause, sizeof(limit_clause), " LIMIT 12 OFFSET %d", offset);
    strcat(sql, limit_clause);
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: %s", sql);
        return rc;
    }
    
    // 绑定参数
    int param_index = 1;
    
    if (condition->task_session && condition->task_session[0]) {
        sqlite3_bind_text(stmt, param_index++, condition->task_session, -1, SQLITE_STATIC);
    }
    
    if (condition->media_name && condition->media_name[0]) {
        sqlite3_bind_text(stmt, param_index++, condition->media_name, -1, SQLITE_STATIC);
    }
    
    if (condition->begin_time > 0) {
        sqlite3_bind_int64(stmt, param_index++, condition->begin_time);
    }
    
    if (condition->end_time > 0) {
        sqlite3_bind_int64(stmt, param_index++, condition->end_time);
    }
    
    if (condition->type_specified) {
        sqlite3_bind_int(stmt, param_index++, condition->type);
    }
    
    // 初始化AlarmList
    alarm_list->count = 0;
    alarm_list->capacity = 12; // 固定容量为12
    alarm_list->alarms = malloc(sizeof(AlarmInfo) * alarm_list->capacity);
    
    if (!alarm_list->alarms) {
        LOG_ERROR("Failed to allocate memory for alarm list");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 遍历查询结果，最多处理12条记录
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && alarm_list->count < 12) {
        // 填充当前AlarmInfo结构体
        AlarmInfo* alarm = &alarm_list->alarms[alarm_list->count];
        alarm->id = sqlite3_column_int(stmt, 0);
        
        const char* temp_str = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(alarm->AlarmId, temp_str ? temp_str : "", sizeof(alarm->AlarmId) - 1);
        alarm->AlarmId[sizeof(alarm->AlarmId) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 2);
        strncpy(alarm->TaskSession, temp_str ? temp_str : "", sizeof(alarm->TaskSession) - 1);
        alarm->TaskSession[sizeof(alarm->TaskSession) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 3);
        strncpy(alarm->MediaName, temp_str ? temp_str : "", sizeof(alarm->MediaName) - 1);
        alarm->MediaName[sizeof(alarm->MediaName) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 4);
        strncpy(alarm->ImageData, temp_str ? temp_str : "", sizeof(alarm->ImageData) - 1);
        alarm->ImageData[sizeof(alarm->ImageData) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 5);
        strncpy(alarm->Time, temp_str ? temp_str : "", sizeof(alarm->Time) - 1);
        alarm->Time[sizeof(alarm->Time) - 1] = '\0';
        
        alarm->TimeStamp = sqlite3_column_int64(stmt, 6);
        alarm->Type = sqlite3_column_int(stmt, 7);
        
        alarm_list->count++;
    }
    
    sqlite3_finalize(stmt);
    
    // 查询总记录数
    strcpy(sql, "SELECT COUNT(*) FROM Alarm WHERE 1=1");
    
    // 添加相同的过滤条件
    if (condition->task_session && condition->task_session[0]) {
        strcat(sql, " AND TaskSession = ?");
    }
    
    if (condition->media_name && condition->media_name[0]) {
        strcat(sql, " AND MediaName = ?");
    }
    
    if (condition->begin_time > 0) {
        strcat(sql, " AND TimeStamp >= ?");
    }
    
    if (condition->end_time > 0) {
        strcat(sql, " AND TimeStamp <= ?");
    }
    
    if (condition->type_specified) {
        strcat(sql, " AND Type = ?");
    }
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare count statement: %s", sql);
        return rc;
    }
    
    // 重新绑定参数
    param_index = 1;
    
    if (condition->task_session && condition->task_session[0]) {
        sqlite3_bind_text(stmt, param_index++, condition->task_session, -1, SQLITE_STATIC);
    }
    
    if (condition->media_name && condition->media_name[0]) {
        sqlite3_bind_text(stmt, param_index++, condition->media_name, -1, SQLITE_STATIC);
    }
    
    if (condition->begin_time > 0) {
        sqlite3_bind_int64(stmt, param_index++, condition->begin_time);
    }
    
    if (condition->end_time > 0) {
        sqlite3_bind_int64(stmt, param_index++, condition->end_time);
    }
    
    if (condition->type_specified) {
        sqlite3_bind_int(stmt, param_index++, condition->type);
    }
    
    // 获取总记录数
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        alarm_list->all_count = sqlite3_column_int(stmt, 0);
    } else {
        alarm_list->all_count = 0;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

// 释放AlarmList内存
void free_alarm_list(AlarmList* alarm_list) {
    if (alarm_list && alarm_list->alarms) {
        free(alarm_list->alarms);
        alarm_list->alarms = NULL;
        alarm_list->count = 0;
        alarm_list->capacity = 0;
    }
}

// 根据AlarmId删除告警记录
int delete_alarm_by_id(const char* alarm_id) {
    char sql[512];
    snprintf(sql, sizeof(sql), 
             "DELETE FROM Alarm WHERE AlarmId = '%s';",
             alarm_id && alarm_id[0] ? alarm_id : "");
    
    return execute_sql(sql);
}

// 清除所有告警记录
int delete_all_alarms() {
    const char* sql = "DELETE FROM Alarm;";
    return execute_sql(sql);
}

// 在文件末尾添加人物信息相关函数实现
// 创建人物信息表
int create_person_table() {
    const char* sql_person = "CREATE TABLE IF NOT EXISTS Person ("
     "photoId INTEGER PRIMARY KEY,"
     "photoName TEXT NOT NULL,"
     "croppedImage TEXT,"
     "info TEXT,"
     "regTime INTEGER);";
     
    return execute_sql(sql_person);
}

// 添加人物信息
int add_person(const PersonInfo* person_info) {
    char sql[1024];
    snprintf(sql, sizeof(sql), 
             "INSERT INTO Person (photoName, croppedImage, info, regTime) "
             "VALUES ('%s', '%s', '%s', %lld);",
             person_info->photoName[0] ? person_info->photoName : "",
             person_info->croppedImage[0] ? person_info->croppedImage : "",
             person_info->info[0] ? person_info->info : "",
             person_info->regTime);
    
    return execute_sql(sql);
}

// 查询所有人物信息到结构体
int query_persons_to_struct(PersonList* person_list) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT photoId, photoName, croppedImage, info, regTime FROM Person;";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to fetch data: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    // 初始化PersonList
    person_list->count = 0;
    person_list->capacity = 5; // 初始容量
    person_list->persons = malloc(sizeof(PersonInfo) * person_list->capacity);
    
    if (!person_list->persons) {
        LOG_ERROR("Failed to allocate memory for person list");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 遍历查询结果
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // 如果需要，扩展容量
        if (person_list->count >= person_list->capacity) {
            person_list->capacity *= 2;
            PersonInfo* temp = realloc(person_list->persons, sizeof(PersonInfo) * person_list->capacity);
            if (!temp) {
                LOG_ERROR("Failed to allocate memory for person list");
                free(person_list->persons);
                sqlite3_finalize(stmt);
                return -1;
            }
            person_list->persons = temp;
        }
        
        // 填充当前PersonInfo结构体
        PersonInfo* person = &person_list->persons[person_list->count];
        person->photoId = sqlite3_column_int(stmt, 0);
        
        const char* temp_str = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(person->photoName, temp_str ? temp_str : "", sizeof(person->photoName) - 1);
        person->photoName[sizeof(person->photoName) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 2);
        strncpy(person->croppedImage, temp_str ? temp_str : "", sizeof(person->croppedImage) - 1);
        person->croppedImage[sizeof(person->croppedImage) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 3);
        strncpy(person->info, temp_str ? temp_str : "", sizeof(person->info) - 1);
        person->info[sizeof(person->info) - 1] = '\0';
        
        person->regTime = sqlite3_column_int64(stmt, 4);
        
        person_list->count++;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

// 根据ID删除人物信息
int delete_person_by_id(int photoId) {
    char sql[256];
    snprintf(sql, sizeof(sql), 
             "DELETE FROM Person WHERE photoId = %d;",
             photoId);
    
    return execute_sql(sql);
}

void free_person_list(PersonList* personlist) {
    if (personlist && personlist->persons) {
        free(personlist->persons);
        personlist->persons = NULL;
        personlist->count = 0;
        personlist->capacity = 0;
    }
}

int add_config(const ConfigInfo* config_info) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO Config (ConfigKey, ConfigValue, ConfigDesc, ConfigType) "
             "VALUES ('%s', '%s', '%s', %d);",
             config_info->ConfigKey[0] ? config_info->ConfigKey : "",
             config_info->ConfigValue[0] ? config_info->ConfigValue : "",
             config_info->ConfigDesc[0] ? config_info->ConfigDesc : "",
             config_info->ConfigType);
    
    return execute_sql(sql);
}

int delete_config_by_Id(const int Id) {
    char sql[512];
    snprintf(sql, sizeof(sql),
             "DELETE FROM Config WHERE id = '%d';",Id);
    
    return execute_sql(sql);
}

int modify_config_by_id(const ConfigInfo* config_info) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "UPDATE Config SET ConfigKey = '%s', ConfigValue = '%s', ConfigDesc = '%s', ConfigType = %d "
             "WHERE id = %d;",
             config_info->ConfigKey[0] ? config_info->ConfigKey : "",
             config_info->ConfigValue[0] ? config_info->ConfigValue : "",
             config_info->ConfigDesc[0] ? config_info->ConfigDesc : "",
             config_info->ConfigType,
             config_info->Id);
    
    return execute_sql(sql);
}

int query_all_configs(ConfigList* config_list) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT Id, ConfigKey, ConfigValue, ConfigDesc, ConfigType FROM Config;";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to fetch data: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    // 初始化ConfigList
    config_list->count = 0;
    config_list->capacity = 10; // 初始容量
    config_list->configs = malloc(sizeof(ConfigInfo) * config_list->capacity);
    
    if (!config_list->configs) {
        LOG_ERROR("Failed to allocate memory for config list");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 遍历查询结果
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // 如果需要，扩展容量
        if (config_list->count >= config_list->capacity) {
            config_list->capacity *= 2;
            ConfigInfo* temp = realloc(config_list->configs, sizeof(ConfigInfo) * config_list->capacity);
            if (!temp) {
                LOG_ERROR("Failed to allocate memory for config list");
                free(config_list->configs);
                sqlite3_finalize(stmt);
                return -1;
            }
            config_list->configs = temp;
        }
        
        // 填充当前ConfigInfo结构体
        ConfigInfo* config = &config_list->configs[config_list->count];

        config->Id = sqlite3_column_int(stmt, 0);
        
        const char* temp_key = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(config->ConfigKey, temp_key ? temp_key : "", sizeof(config->ConfigKey) - 1);
        config->ConfigKey[sizeof(config->ConfigKey) - 1] = '\0';
        
        const char* temp_value = (const char*)sqlite3_column_text(stmt, 2);
        strncpy(config->ConfigValue, temp_value ? temp_value : "", sizeof(config->ConfigValue) - 1);
        config->ConfigValue[sizeof(config->ConfigValue) - 1] = '\0';
        
        const char* temp_desc = (const char*)sqlite3_column_text(stmt, 3);
        strncpy(config->ConfigDesc, temp_desc ? temp_desc : "", sizeof(config->ConfigDesc) - 1);
        config->ConfigDesc[sizeof(config->ConfigDesc) - 1] = '\0';
        
        config->ConfigType = sqlite3_column_int(stmt, 4);
        
        config_list->count++;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

void free_config_list(ConfigList* config_list) {
    if (config_list && config_list->configs) {
        free(config_list->configs);
        config_list->configs = NULL;
        config_list->count = 0;
        config_list->capacity = 0;
    }
}

// 修改FTP配置，如果没有ID=1的记录则插入新记录
int update_ftp_config(const FtpConfigInfo* ftp_config) {
    // 首先检查是否存在ID=1的记录
    sqlite3_stmt* stmt;
    const char* sql_check = "SELECT COUNT(*) FROM FtpConfig WHERE Id = 1;";
    
    int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare check statement: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    rc = sqlite3_step(stmt);
    int count = 0;
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    if (count > 0) {
        // ID=1的记录存在，执行更新操作
        char sql[1024];
        snprintf(sql, sizeof(sql),
                 "UPDATE FtpConfig SET FtpIpaddr = '%s', FtpPort = %d, FtpId = '%s', FtpUser = '%s', FtpPasswd = '%s' "
                 "WHERE Id = 1;",
                 ftp_config->FtpIpaddr[0] ? ftp_config->FtpIpaddr : "",
                 ftp_config->FtpPort,
                 ftp_config->FtpId[0] ? ftp_config->FtpId : "",
                 ftp_config->FtpUser[0] ? ftp_config->FtpUser : "",
                 ftp_config->FtpPasswd[0] ? ftp_config->FtpPasswd : "");
        
        return execute_sql(sql);
    } else {
        // ID=1的记录不存在，插入新记录
        char sql[1024];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO FtpConfig (Id, FtpIpaddr, FtpPort, FtpId, FtpUser, FtpPasswd) "
                 "VALUES (1, '%s', %d, '%s', '%s', '%s');",
                 ftp_config->FtpIpaddr[0] ? ftp_config->FtpIpaddr : "",
                 ftp_config->FtpPort,
                 ftp_config->FtpId[0] ? ftp_config->FtpId : "",
                 ftp_config->FtpUser[0] ? ftp_config->FtpUser : "",
                 ftp_config->FtpPasswd[0] ? ftp_config->FtpPasswd : "");
        
        return execute_sql(sql);
    }
}

// 根据ID查询单个FTP配置
int query_ftp_config(FtpConfigInfo* ftp_config) {
    sqlite3_stmt* stmt;
    char sql[512];
    
    snprintf(sql, sizeof(sql),
         "SELECT Id, FtpIpaddr, FtpPort, FtpId, FtpUser, FtpPasswd "
         "FROM FtpConfig WHERE Id = 1;");

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    // 执行查询
    rc = sqlite3_step(stmt);
    
    if (rc == SQLITE_ROW) {
        // 填充FtpConfigInfo结构体
        ftp_config->Id = sqlite3_column_int(stmt, 0);
        
        const char* temp_ip = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(ftp_config->FtpIpaddr, temp_ip ? temp_ip : "", sizeof(ftp_config->FtpIpaddr) - 1);
        ftp_config->FtpIpaddr[sizeof(ftp_config->FtpIpaddr) - 1] = '\0';
        
        ftp_config->FtpPort = sqlite3_column_int(stmt, 2);
        
        const char* temp_id = (const char*)sqlite3_column_text(stmt, 3);
        strncpy(ftp_config->FtpId, temp_id ? temp_id : "", sizeof(ftp_config->FtpId) - 1);
        ftp_config->FtpId[sizeof(ftp_config->FtpId) - 1] = '\0';
        
        const char* temp_user = (const char*)sqlite3_column_text(stmt, 4);
        strncpy(ftp_config->FtpUser, temp_user ? temp_user : "", sizeof(ftp_config->FtpUser) - 1);
        ftp_config->FtpUser[sizeof(ftp_config->FtpUser) - 1] = '\0';
        
        const char* temp_pass = (const char*)sqlite3_column_text(stmt, 5);
        strncpy(ftp_config->FtpPasswd, temp_pass ? temp_pass : "", sizeof(ftp_config->FtpPasswd) - 1);
        ftp_config->FtpPasswd[sizeof(ftp_config->FtpPasswd) - 1] = '\0';
        
        sqlite3_finalize(stmt);
        return 0; // 成功找到并填充数据
    } else if (rc == SQLITE_DONE) {
        // 没有找到匹配的记录
        sqlite3_finalize(stmt);
        return 1; // 未找到记录
    } else {
        // 查询出错
        LOG_ERROR("Failed to query FtpConfig: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }
}

// 查询所有SZ651配置到结构体
int query_all_sz651_configs(SzConfigList* sz651_list) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT Id, SzIpaddr, SzPort, SzAddr, SzUser, SzPasswd FROM Sz651Config;";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to fetch data: %s", sqlite3_errmsg(db));
        return rc;
    }
    
    // 初始化SzConfigList
    sz651_list->count = 0;
    sz651_list->capacity = 4; // 初始容量
    sz651_list->configs = malloc(sizeof(SzConfigInfo) * sz651_list->capacity);
    
    if (!sz651_list->configs) {
        LOG_ERROR("Failed to allocate memory for sz651 list");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 遍历查询结果
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // 如果需要，扩展容量
        if (sz651_list->count >= sz651_list->capacity) {
            sz651_list->capacity *= 2;
            SzConfigInfo* temp = realloc(sz651_list->configs, sizeof(SzConfigInfo) * sz651_list->capacity);
            if (!temp) {
                LOG_ERROR("Failed to allocate memory for sz651 list");
                free(sz651_list->configs);
                sqlite3_finalize(stmt);
                return -1;
            }
            sz651_list->configs = temp;
        }
        
        // 填充当前SzConfigInfo结构体
        SzConfigInfo* config = &sz651_list->configs[sz651_list->count];
        config->Id = sqlite3_column_int(stmt, 0);
        
        const char* temp_str = (const char*)sqlite3_column_text(stmt, 1);
        strncpy(config->SzIpaddr, temp_str ? temp_str : "", sizeof(config->SzIpaddr) - 1);
        config->SzIpaddr[sizeof(config->SzIpaddr) - 1] = '\0';
        
        config->SzPort = sqlite3_column_int(stmt, 2);
        
        temp_str = (const char*)sqlite3_column_text(stmt, 3);
        strncpy(config->SzAddr, temp_str ? temp_str : "", sizeof(config->SzAddr) - 1);
        config->SzAddr[sizeof(config->SzAddr) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 4);
        strncpy(config->SzUser, temp_str ? temp_str : "", sizeof(config->SzUser) - 1);
        config->SzUser[sizeof(config->SzUser) - 1] = '\0';
        
        temp_str = (const char*)sqlite3_column_text(stmt, 5);
        strncpy(config->SzPasswd, temp_str ? temp_str : "", sizeof(config->SzPasswd) - 1);
        config->SzPasswd[sizeof(config->SzPasswd) - 1] = '\0';
        
        sz651_list->count++;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

// 添加SZ651配置
int add_sz651_config(const SzConfigInfo* sz651_info) {
    char sql[1024];
    snprintf(sql, sizeof(sql), 
             "INSERT INTO Sz651Config (SzIpaddr, SzPort, SzAddr, SzUser, SzPasswd) "
             "VALUES ('%s', %d, '%s', '%s', '%s');",
             sz651_info->SzIpaddr[0] ? sz651_info->SzIpaddr : "",
             sz651_info->SzPort,
             sz651_info->SzAddr[0] ? sz651_info->SzAddr : "",
             sz651_info->SzUser[0] ? sz651_info->SzUser : "",
             sz651_info->SzPasswd[0] ? sz651_info->SzPasswd : "");
    
    return execute_sql(sql);
}

// 根据ID删除SZ651配置
int delete_sz651_config_by_id(int id) {
    char sql[512];
    snprintf(sql, sizeof(sql), 
             "DELETE FROM Sz651Config WHERE Id = %d;",
             id);
    
    return execute_sql(sql);
}

// 根据ID修改SZ651配置
int modify_sz651_config_by_id(const SzConfigInfo* sz651_info) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "UPDATE Sz651Config SET SzIpaddr = '%s', SzPort = %d, SzAddr = '%s', "
             "SzUser = '%s', SzPasswd = '%s' "
             "WHERE Id = %d;",
             sz651_info->SzIpaddr[0] ? sz651_info->SzIpaddr : "",
             sz651_info->SzPort,
             sz651_info->SzAddr[0] ? sz651_info->SzAddr : "",
             sz651_info->SzUser[0] ? sz651_info->SzUser : "",
             sz651_info->SzPasswd[0] ? sz651_info->SzPasswd : "",
             sz651_info->Id);
    
    return execute_sql(sql);
}

// 释放SzConfigList内存
void free_sz651_config_list(SzConfigList* sz651_list) {
    if (sz651_list && sz651_list->configs) {
        free(sz651_list->configs);
        sz651_list->configs = NULL;
        sz651_list->count = 0;
        sz651_list->capacity = 0;
    }
}

