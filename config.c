#include "model_router.h"
#include "jsmn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

const char *task_names[] = {
  "chat", "code", "vision", "reasoning", "translation",
  "audio", "video"
};

/*
 * JSON 工具: 从 jsmn token 获取字符串值
 * 返回长度, 或 -1 出错
 */
/* 从 jsmn token 中提取字符串值，去掉首尾引号 */
static int json_string_value(const char *js, const jsmntok_t *t, char *out, size_t out_sz) {
  size_t raw_len = (size_t)(t->end - t->start);
  /* 跳过首尾引号 */
  int start = t->start;
  size_t len = raw_len;
  if (len >= 2 && js[start] == '"') { start++; len -= 2; }
  if (len >= out_sz) len = out_sz - 1;
  memcpy(out, js + start, len);
  out[len] = '\0';
  return (int)len;
}

/* 找 key 对应的 value token */
static int json_find_key(const char *js, const jsmntok_t *tokens, int ntokens,
                         const char *key, jsmntok_t **out) {
  for (int i = 0; i < ntokens; i++) {
    if (tokens[i].type == JSMN_STRING) {
      size_t klen = (size_t)(tokens[i].end - tokens[i].start - 2); /* strip quotes */
      if (klen == strlen(key) && strncmp(js + tokens[i].start + 1, key, klen) == 0) {
        if (i + 1 < ntokens) {
          *out = &tokens[i + 1];
          return 0;
        }
        return -1;
      }
    }
  }
  return -1;
}

/*
 * 解析模型配置 — 直接从展平的 jsmn token 数组中顺序解析 key-value
 * obj_start_idx: obj token 在数组中的索引
 * 由于 jsmn 展平了所有 token，配置文件是扁平的 key-value 结构
 */
static int parse_model_config(const char *js, const jsmntok_t *all_tokens,
                               int obj_start_idx, int ntokens,
                               model_config_t *mc) {
  /* 从 obj_start_idx + 1 开始遍历，直到遇到不属于当前 obj 的 token */
  const jsmntok_t *obj = &all_tokens[obj_start_idx];
  for (int i = obj_start_idx + 1; i < ntokens; i++) {
    const jsmntok_t *k = &all_tokens[i];
    if (k->type != JSMN_STRING) continue;
    int klen = k->end - k->start;
    if (klen < 2) continue;
    i++; if (i >= ntokens) break;
    const jsmntok_t *v = &all_tokens[i];
    if (klen == 10 && strncmp(js + k->start + 1, "provider", 8) == 0)
      json_string_value(js, v, mc->provider, sizeof(mc->provider));
    else if (klen == 7 && strncmp(js + k->start + 1, "model", 5) == 0)
      json_string_value(js, v, mc->model, sizeof(mc->model));
    else if (klen == 10 && strncmp(js + k->start + 1, "base_url", 8) == 0)
      json_string_value(js, v, mc->base_url, sizeof(mc->base_url));
    else if (klen == 9 && strncmp(js + k->start + 1, "api_key", 7) == 0)
      json_string_value(js, v, mc->api_key, sizeof(mc->api_key));
  }
  return 0;
}

int config_load(router_config_t *cfg, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "[config] 无法打开 %s: %s\n", path, strerror(errno));
    return -1;
  }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (fsize <= 0) { fclose(f); return -1; }

  char *js = malloc((size_t)fsize + 1);
  fread(js, 1, (size_t)fsize, f);
  fclose(f);
  js[fsize] = '\0';

  /* 这个 jsmn 版本不支持 NULL tokens 计数模式，需要先给个大数组 */
  jsmn_parser p;
  int max_tokens = 256;
  jsmntok_t *tokens = malloc((size_t)max_tokens * sizeof(jsmntok_t));
  if (!tokens) { free(js); return -1; }
  jsmn_init(&p);
  int ntokens = jsmn_parse(&p, js, (size_t)fsize, tokens, (unsigned int)max_tokens);
  if (ntokens <= 0) { free(tokens); free(js); return -1; }

  /* 解析 router_model — 找 key 后+1 是 value (object) */
  int rm_key_idx = -1;
  for (int k = 0; k < ntokens; k++) {
    if (tokens[k].type == JSMN_STRING) {
      int tlen = tokens[k].end - tokens[k].start;
      if (tlen == 14 && strncmp(js + tokens[k].start + 1, "router_model", 12) == 0) {
        rm_key_idx = k;
        break;
      }
    }
  }
  if (rm_key_idx >= 0 && rm_key_idx + 1 < ntokens) {
    parse_model_config(js, tokens, rm_key_idx + 1, ntokens, &cfg->router_model);
    fprintf(stderr, "[config] router_model: %s / %s\n", cfg->router_model.provider, cfg->router_model.model);
  }

  /* 解析 routing_rules */
  int rr_key_idx = -1;
  for (int k = 0; k < ntokens; k++) {
    if (tokens[k].type == JSMN_STRING) {
      int tlen = tokens[k].end - tokens[k].start;
      if (tlen == 15 && strncmp(js + tokens[k].start + 1, "routing_rules", 13) == 0) {
        rr_key_idx = k;
        break;
      }
    }
  }
  int completed = 0;
  if (rr_key_idx >= 0) {
    /* rr_key_idx + 1 是 routing_rules 的 value (OBJ) */
    int rr_obj_idx = rr_key_idx + 1;
    if (rr_obj_idx >= ntokens) rr_obj_idx = 0;  /* 安全 */
    int idx = 1;
    while (rr_obj_idx + idx < ntokens && completed < TASK_UNKNOWN) {
      const jsmntok_t *child = &tokens[rr_obj_idx + idx];
      if (child->type == JSMN_STRING && child != &tokens[0]) {
        size_t klen = (size_t)(child->end - child->start - 2);
        int matched = 0;
        for (int t = 0; t < TASK_UNKNOWN; t++) {
          if (strlen(task_names[t]) == klen &&
              strncmp(js + child->start + 1, task_names[t], klen) == 0) {
            int val_idx = rr_obj_idx + idx + 1;
            if (val_idx < ntokens) {
              parse_model_config(js, tokens, val_idx, ntokens, &cfg->rules[t]);
              idx += 2;
              matched = 1;
              completed++;
            }
            break;
          }
        }
        if (!matched) idx++;
      } else {
        idx++;
      }
    }
  }
  for (int t = 0; t < TASK_UNKNOWN; t++) {
    fprintf(stderr, "[config] rules[%d](%s): %s\n", t, task_names[t], cfg->rules[t].model);
  }

  free(tokens);
  free(js);
  return 0;
}

static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (c < 0x20) fprintf(f, "\\u%04x", c);
                else fputc(c, f);
                break;
        }
    }
    fputc('"', f);
}

static void write_model_config(FILE *f, const model_config_t *mc, int indent) {
    const char *pad = indent == 4 ? "    " : "      ";
    fprintf(f, "%s{", pad + 2);  // same indent level for the whole block
    fprintf(f, "\n%s\"provider\": ", pad);
    write_json_string(f, mc->provider);
    fprintf(f, ",\n%s\"model\": ", pad);
    write_json_string(f, mc->model);
    fprintf(f, ",\n%s\"base_url\": ", pad);
    write_json_string(f, mc->base_url);
    fprintf(f, ",\n%s\"api_key\": \"***\"", pad);
    fprintf(f, "\n%s}", pad + (indent == 4 ? 2 : 0));
}

int config_save(const router_config_t *cfg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[config] 无法写入 %s\n", path);
        return -1;
    }

    fprintf(f, "{\n");

    // router_model
    fprintf(f, "  \"router_model\": ");
    write_model_config(f, &cfg->router_model, 2);
    fprintf(f, ",\n");

    // routing_rules
    fprintf(f, "  \"routing_rules\": {\n");
    for (int t = 0; t < TASK_UNKNOWN; t++) {
        if (t > 0) fprintf(f, ",\n");
        fprintf(f, "    \"%s\": ", task_names[t]);
        write_model_config(f, &cfg->rules[t], 4);
    }
    fprintf(f, "\n  }\n");
    fprintf(f, "}\n");

    fclose(f);
    fprintf(stderr, "[config] 已保存到 %s\n", path);
    return 0;
}
