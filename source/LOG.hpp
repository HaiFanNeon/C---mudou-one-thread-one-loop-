#pragma once

#define INF 0
#define DBG 1
#define ERR 2
#define LOG_LEVEL INF
#define LOG(level, format, ...) \
    do { \
        if (level < LOG_LEVEL) break; \
        time_t t = time(nullptr); \
        struct tm *tm = localtime(&t); \
        char tmp[32] = {0}; \
        strftime(tmp, 32 ,"%H:%M:%S", tm); \
        fprintf(stdout, "[%s %s:%d] " format "\n", tmp, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__);
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__);
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__);