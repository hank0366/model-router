#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "model_router.h"
#include <time.h>

/* 日志文件路径 */
#define DEBUG_LOG_DIR  "/home/lvzheyu/apps/model-router-c/logs"
#define DEBUG_LOG_FMT  "/home/lvzheyu/apps/model-router-c/logs/router_debug_%s.jsonl"

/* 单次路由记录 */
typedef struct {
  time_t timestamp;
  char used_model[128];
  char used_provider[64];
  char matched_keyword[128];  /* 匹配的关键词，空表示 fallback */
  char user_text_preview[256];  /* 用户消息前 250 字符 */
  int  fallback;               /* 1=fallback 到 chat, 0=规则命中 */
  int  has_image;              /* 是否含图片 */
  int  task_type;              /* 分类后的任务类型 */
} debug_log_entry_t;

/* 初始化日志目录 */
int debug_log_init(void);

/* 写入一条路由日志 */
void debug_log_write(debug_log_entry_t *entry);

/* 清理超过 N 天的日志文件 */
int debug_log_cleanup_days(unsigned int days);

#endif /* DEBUG_LOG_H */
