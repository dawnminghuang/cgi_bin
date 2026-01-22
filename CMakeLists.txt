cmake_minimum_required(VERSION 3.5)

# Cross-compilation settings
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Set the target sysroot
set(CMAKE_SYSROOT ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
# Search for programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

project(cgi_bin C)  # 指定项目使用C语言

find_package(PkgConfig REQUIRED)

# 查找pthread库
find_package(Threads REQUIRED)

# 查找sqlite3库
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# 添加cJSON源文件到SOURCES变量
list(APPEND SOURCES cJSON.c)
list(APPEND SOURCES api.c)
list(APPEND SOURCES sql.c)
list(APPEND SOURCES log.c)
list(APPEND SOURCES public.c)

# 将目标文件名改为cgi_bin，并使用main.c作为源文件
add_executable(cgi_bin main.c ${SOURCES})

target_include_directories(cgi_bin PUBLIC 
    ${PROJECT_SOURCE_DIR}/rknn
    ${SQLITE3_INCLUDE_DIRS}
)

# 链接pthread库、C语言标准库、sqlite3库
target_link_libraries(cgi_bin 
    pthread
    m
    c
    ${SQLITE3_LIBRARIES}
)

# 保持原有的静态链接设置
set_target_properties(cgi_bin PROPERTIES LINK_SEARCH_START_STATIC 1)
set_target_properties(cgi_bin PROPERTIES LINK_SEARCH_END_STATIC 1)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")