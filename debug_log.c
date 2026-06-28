#include "debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>

static FILE *log_fp = NULL;
static char log_filename[512] = {0};
static char last_date[16] = {0};  /* YYYY-MM-DD */

static void make_log_dir(void) {
  mkdir(DEBUG_LOG_DIR, 0755);
}

static void build_filename(time_t t) {
  struct tm *tm_info = localtime(&t);
  strftime(last_date, sizeof(last_date), "%Y-%m-%d", tm_info);
  snprintf(log_filename, sizeof(log_filename), DEBUG_LOG_FMT, last_date);
}

int debug_log_init(void) {
  make_log_dir();
  time_t now = time(NULL);
  build_filename(now);
  log_fp = fopen(log_filename, "a");
  if (!log_fp) {
    fprintf(stderr, "[debug_log] 无法打开日志文件: %s\n", log_filename);
    return -1;
  }
  fprintf(stderr, "[debug_log] 日志文件: %s\n", log_filename);
  return 0;
}

void debug_log_write(debug_log_entry_t *entry) {
  if (!log_fp) {
    /* 如果没初始化过，尝试自动初始化 */
    debug_log_init();
  }
  if (!log_fp) return;
  
  /* 检查是否跨天 */
  char cur_date[16];
  struct tm *tm_info = localtime(&entry->timestamp);
  strftime(cur_date, sizeof(cur_date), "%Y-%m-%d", tm_info);
  
  if (strcmp(cur_date, last_date) != 0) {
    fclose(log_fp);
    build_filename(entry->timestamp);
    log_fp = fopen(log_filename, "a");
    if (!log_fp) return;
  }
  
  struct timeval tv;
  gettimeofday(&tv, NULL);
  long long ms = (long long)entry->timestamp * 1000 + tv.tv_usec / 1000;
  
  /* 截断预览 */
  char preview[256] = {0};
  strncpy(preview, entry->user_text_preview, sizeof(preview) - 1);
  /* 把换行符替换掉 */
  for (int i = 0; i < 256; i++) {
    if (preview[i] == '\n' || preview[i] == '\r') preview[i] = ' ';
  }
  
  fprintf(log_fp,
    "{\"ts\":%lld,\"model\":\"%s\",\"provider\":\"%s\",\"keyword\":\"%s\","
    "\"preview\":\"%s\",\"fallback\":%d,\"has_image\":%d,\"task_type\":%d}\n",
    ms,
    entry->used_model,
    entry->used_provider,
    entry->matched_keyword,
    preview,
    entry->fallback,
    entry->has_image,
    entry->task_type
  );
  fflush(log_fp);
}

/* 清理超过 N 天的日志文件 */
int debug_log_cleanup_days(unsigned int days) {
  DIR *dir = opendir(DEBUG_LOG_DIR);
  if (!dir) return -1;
  
  struct dirent *ent;
  time_t now = time(NULL);
  int deleted = 0;
  
  while ((ent = readdir(dir)) != NULL) {
    /* 只处理 router_debug_*.jsonl 文件 */
    if (strncmp(ent->d_name, "router_debug_", 13) != 0) continue;
    if (strcmp(ent->d_name + strlen(ent->d_name) - 6, ".jsonl") != 0) continue;
    
    /* 提取日期 YYYY-MM-DD */
    char date_str[16];
    strncpy(date_str, ent->d_name + 13, 10);
    date_str[10] = '\0';
    
    /* 解析日期 */
    struct tm tm_time = {0};
    strptime(date_str, "%Y-%m-%d", &tm_time);
    time_t file_time = mktime(&tm_time);
    
    if (difftime(now, file_time) > (double)(days * 86400)) {
      char filepath[512];
      snprintf(filepath, sizeof(filepath), "%s/%s", DEBUG_LOG_DIR, ent->d_name);
      unlink(filepath);
      fprintf(stderr, "[debug_log] 已删除旧日志: %s\n", filepath);
      deleted++;
    }
  }
  
  closedir(dir);
  return deleted;
}
