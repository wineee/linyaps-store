/* SPDX-License-Identifier: MIT
 *
 * linyaps_log.h — 轻量日志宏
 *
 * 用法：
 *   #include "linyaps_log.h"
 *   LOG_INFO("search", "keyword=%s", kw);
 *   LOG_ERR("dbus", "sd_bus_call failed: %d", r);
 *
 * 级别（编译期，默认 DEBUG）：
 *   LINYAPS_LOG_LEVEL_NONE  = 0
 *   LINYAPS_LOG_LEVEL_ERR   = 1
 *   LINYAPS_LOG_LEVEL_WARN  = 2
 *   LINYAPS_LOG_LEVEL_INFO  = 3
 *   LINYAPS_LOG_LEVEL_DEBUG = 4   ← 默认
 *
 * 覆盖：cmake -DLINYAPS_LOG_LEVEL=3  （只输出 INFO 及以上）
 */

#pragma once

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#ifndef LINYAPS_LOG_LEVEL
#define LINYAPS_LOG_LEVEL 4 /* DEBUG */
#endif

/* ---- 时间戳辅助（精确到毫秒）---- */
static inline void _log_ts(char *buf, int n) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm tm;
  localtime_r(&tv.tv_sec, &tm);
  int ms = (int)(tv.tv_usec / 1000);
  snprintf(buf, (size_t)n, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min,
           tm.tm_sec, ms);
}

#define _LOG(level, tag, fmt, ...)                                             \
  do {                                                                         \
    char _ts[16];                                                              \
    _log_ts(_ts, (int)sizeof(_ts));                                            \
    fprintf(stderr, "[%s] %-5s [%-10s] " fmt "\n", _ts, level, tag,            \
            ##__VA_ARGS__);                                                    \
  } while (0)

#if LINYAPS_LOG_LEVEL >= 1
#define LOG_ERR(tag, fmt, ...) _LOG("ERR  ", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_ERR(tag, fmt, ...)                                                 \
  do {                                                                         \
  } while (0)
#endif

#if LINYAPS_LOG_LEVEL >= 2
#define LOG_WARN(tag, fmt, ...) _LOG("WARN ", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(tag, fmt, ...)                                                \
  do {                                                                         \
  } while (0)
#endif

#if LINYAPS_LOG_LEVEL >= 3
#define LOG_INFO(tag, fmt, ...) _LOG("INFO ", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(tag, fmt, ...)                                                \
  do {                                                                         \
  } while (0)
#endif

#if LINYAPS_LOG_LEVEL >= 4
#define LOG_DEBUG(tag, fmt, ...) _LOG("DEBUG", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(tag, fmt, ...)                                               \
  do {                                                                         \
  } while (0)
#endif
