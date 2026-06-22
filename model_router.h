#ifndef MODEL_ROUTER_H
#define MODEL_ROUTER_H

#include <stddef.h>

/* 配置文件路径 */
#define CONFIG_FILE "/home/lvzheyu/apps/model-router-c/config.json"

/* 任务类型 */
typedef enum {
  TASK_CHAT = 0,
  TASK_CODE,
  TASK_VISION,
  TASK_REASONING,
  TASK_TRANSLATION,
  TASK_AUDIO,
  TASK_VIDEO,
  TASK_UNKNOWN
} task_type_t;

extern const char *task_names[];

/* 模型配置 */
typedef struct {
  char provider[64];
  char model[128];
  char base_url[256];
  char api_key[128];
} model_config_t;

/* 路由规则 */
typedef struct {
  model_config_t router_model;     /* 意图分类用 */
  model_config_t rules[TASK_UNKNOWN]; /* 各任务类型后端 */
} router_config_t;

/* 加载/保存配置 */
int config_load(router_config_t *cfg, const char *path);
int config_save(const router_config_t *cfg, const char *path);

/* 意图分类 */
task_type_t classify_by_rules(const char *text);
task_type_t classify_with_llm(const char *text, const model_config_t *router);

/* HTTP 请求工具 */
typedef struct {
  char *data;
  size_t len;
  size_t cap;
} http_response_t;

void http_response_init(http_response_t *r);
void http_response_free(http_response_t *r);
int http_response_append(http_response_t *r, const char *data, size_t len);

int http_post(const char *url, const char *body, size_t body_len,
              const char *api_key, int stream, http_response_t *resp);

/* SSE 透传 */
typedef void (*sse_callback_t)(const char *chunk, void *userdata);
int http_post_sse(const char *url, const char *body, size_t body_len,
                  const char *api_key, sse_callback_t cb, void *userdata);

/* 路由并调用 */
int route_and_call(const char *request_body, size_t body_len,
                   const router_config_t *cfg,
                   http_response_t *resp, char *used_model, size_t model_sz);

/* 内置规则关键词匹配 */
int keyword_count(void);

#endif /* MODEL_ROUTER_H */
