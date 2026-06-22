#include "model_router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void http_response_init(http_response_t *r) {
  r->data = NULL;
  r->len = 0;
  r->cap = 0;
}

/*
 * 从 JSON 请求体中提取用户消息
 */
static int extract_user_text(const char *json, size_t len,
                             char *out, size_t out_sz) {
  /* 简单地在 JSON 中找 "content": "..." */
  const char *content_key = "\"content\"";
  const char *p = json;
  size_t remaining = len;
  int found = 0;

  while ((p = strstr(p, content_key)) && (size_t)(p - json) < len) {
    p += strlen(content_key);
    remaining = len - (size_t)(p - json);

    /* 跳过 : \s*" */
    while (remaining && (*p == ':' || *p == ' ' || *p == '\t')) { p++; remaining--; }
    if (!remaining || *p != '"') continue;

    p++; /* 跳过开头引号 */
    size_t text_len = 0;
    const char *start = p;

    while (remaining && *p) {
      if (*p == '"' && (p == start || *(p-1) != '\\')) break;
      p++; remaining--; text_len++;
    }

    if (text_len > 0 && text_len < out_sz) {
      memcpy(out, start, text_len);
      out[text_len] = '\0';
      found = 1;
      break;
    }
  }

  /* 检查是否包含图片 */
  if (strstr(json, "\"image_url\"") || strstr(json, "\"images\"")) {
    return TASK_VISION; /* 用返回值类型的方式 — 实际上负数 */
  }

  return found ? 0 : -1;
}

/*
 * 构建 LLM 分类 prompt 并获取结果
 * 这里简单实现 — 实际应该 HTTP POST
 */
static task_type_t classify_text(const char *text, const router_config_t *cfg) {
  /* 先试规则匹配 */
  task_type_t t = classify_by_rules(text);
  if (t != TASK_CHAT) return t;

  /* 检测图片 */
  if (strstr(text, "\"image_url\"") || strstr(text, "\"images\""))
    return TASK_VISION;

  /* LLM 分类 — 暂时 fallback 到 chat */
  (void)cfg;
  fprintf(stderr, "[router] 规则未匹配到，fallback to chat\n");
  return TASK_CHAT;
}

/*
 * 核心函数: 路由并调用后端模型
 * request_body: 完整的 OpenAI 兼容请求 JSON
 */
int route_and_call(const char *request_body, size_t body_len,
                   const router_config_t *cfg,
                   http_response_t *resp, char *used_model, size_t model_sz) {
  /* 1. 提取用户消息文本 */
  char user_text[4096] = {0};
  int has_image = 0;

  if (strstr(request_body, "\"image_url\"") || strstr(request_body, "\"images\"")) {
    has_image = 1;
  }

  if (!has_image) {
    extract_user_text(request_body, body_len, user_text, sizeof(user_text));
  }

  /* 2. 意图分类 */
  task_type_t task;
  if (has_image) {
    task = TASK_VISION;
    fprintf(stderr, "[router] 📷 检测到图片 → vision\n");
  } else {
    task = classify_text(user_text, cfg);
    fprintf(stderr, "[router] 🎯 任务类型: %s\n", task_names[task]);
  }

  /* 3. 选择后端模型 */
  const model_config_t *backend;
  if (task >= 0 && task < TASK_UNKNOWN) {
    backend = &cfg->rules[task];
  } else {
    backend = &cfg->rules[TASK_CHAT];
  }

  if (used_model && model_sz > 0) {
    snprintf(used_model, model_sz, "%s", backend->model);
  }

  fprintf(stderr, "[router] 📡 路由到: %s (%s)\n", backend->model, backend->provider);

  /* 4. 构建后端 URL */
  char url[512];
  char base[256];
  strncpy(base, backend->base_url, sizeof(base)-1);
  /* 去掉尾部 /v1 或 / 以保证路径拼接正确 */
  size_t blen = strlen(base);
  while (blen > 0 && base[blen-1] == '/') base[--blen] = '\0';
  if (blen >= 3 && strcmp(base + blen - 3, "/v1") == 0) base[blen-3] = '\0';

  snprintf(url, sizeof(url), "%s/v1/chat/completions", base);

  /* 5. 替换 body 中的 model 名 (router-v1 → 实际模型名) */
  char modified_body[65536];
  size_t mb_len = body_len;
  if (body_len < sizeof(modified_body) - 1) {
    memcpy(modified_body, request_body, body_len);
    modified_body[body_len] = '\0';

    /* 替换 "model":"router-v1" → "model":"实际模型名" */
    char *model_pos = strstr(modified_body, "\"model\"");
    if (model_pos) {
      /* 找到冒号和引号 */
      char *val_start = strchr(model_pos, ':');
      if (val_start) {
        val_start++;
        while (*val_start == ' ') val_start++;
        if (*val_start == '\"') {
          char *val_end = strchr(val_start + 1, '\"');
          if (val_end) {
            size_t old_model_len = (size_t)(val_end - val_start + 1);
            size_t new_model_len = strlen(backend->model) + 2; /* +2 for quotes */
            char new_val[256];
            snprintf(new_val, sizeof(new_val), "\"%s\"", backend->model);
            size_t new_val_len = strlen(new_val);

            ptrdiff_t diff = (ptrdiff_t)(new_val_len - old_model_len);
            if (diff != 0) {
              memmove(val_start + new_val_len, val_start + old_model_len,
                      body_len - (size_t)(val_start - modified_body) - old_model_len + 1);
              mb_len = (size_t)((ptrdiff_t)mb_len + diff);
            }
            memcpy(val_start, new_val, new_val_len);
          }
        }
      }
    }
    fprintf(stderr, "[router] 📤 发送到 %s (model: %s)\n", url, backend->model);
  }

  /* 6. 调用后端 */
  int stream = (strstr(request_body, "\"stream\": true") != NULL);

  http_response_t backend_resp;
  http_response_init(&backend_resp);

  int ret = http_post(url, modified_body, mb_len,
                      backend->api_key, stream, &backend_resp);

  if (ret < 0) {
    fprintf(stderr, "[router] ❌ 后端调用失败\n");
    char errmsg[] = "{\"error\":\"backend call failed\"}";
    http_response_append(resp, errmsg, strlen(errmsg));
    return -1;
  }

  /* 复制结果 */
  http_response_append(resp, backend_resp.data, backend_resp.len);
  http_response_free(&backend_resp);

  fprintf(stderr, "[router] ✅ 完成 (%zu bytes)\n", resp->len);
  return 0;
}
