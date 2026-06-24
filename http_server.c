#include "model_router.h"
/* http_client.h 合并到 model_router.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <time.h>

#include "admin_html.h"
#define MAX_EVENTS 1024
#define BUFSIZE 65536
#define MAX_CLIENTS 256

/* 客户端连接状态 */
typedef struct {
  int fd;
  char buf[BUFSIZE];
  size_t len;
  int is_streaming;
  int keep_alive;
} client_t;

static router_config_t g_config;
static int g_epoll_fd;
static client_t g_clients[MAX_CLIENTS];
static int g_port = 8000;

/* 设置非阻塞 */
static int set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* HTTP 响应头部 */
static const char *http_headers(int code, const char *status, const char *content_type,
                                size_t content_len, int stream) {
  static char hdr[1024];
  if (stream) {
    snprintf(hdr, sizeof(hdr),
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-cache\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: keep-alive\r\n"
      "\r\n", code, status);
  } else {
    snprintf(hdr, sizeof(hdr),
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %zu\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: close\r\n"
      "\r\n", code, status, content_type, content_len);
  }
  return hdr;
}

/* 回复客户端 */
static int send_response(client_t *c, const char *data, size_t len) {
  if (send(c->fd, data, len, MSG_NOSIGNAL) < 0) return -1;
  return 0;
}

/* 健康检查 */
static void handle_health(client_t *c) {
  const char *body = "{\"status\":\"ok\",\"version\":\"3.0.0-c\"}";
  size_t blen = strlen(body);
  const char *hdr = http_headers(200, "OK", "application/json", blen, 0);
  send_response(c, hdr, strlen(hdr));
  send_response(c, body, blen);
}

/* 模型列表 */
static void handle_models(client_t *c) {
  const char *body =
    "{\"object\":\"list\",\"data\":["
    "{\"id\":\"router-v1\",\"object\":\"model\",\"owned_by\":\"model-router-c\"}"
    "]}";
  size_t blen = strlen(body);
  const char *hdr = http_headers(200, "OK", "application/json", blen, 0);
  send_response(c, hdr, strlen(hdr));
  send_response(c, body, blen);
}


/* =========================== */
/* 配置管理 API                 */
/* =========================== */

extern int config_save(const router_config_t *cfg, const char *path);
extern const char *task_names[];

/* JSON 转义辅助 */
static void escape_json_str(const char *in, char *out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_sz - 6; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= out_sz) break;
            out[j++] = '\\';
            out[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= out_sz) break;
            out[j++] = '\\'; out[j++] = 'n';
        } else if (c == '\r') {
            if (j + 2 >= out_sz) break;
            out[j++] = '\\'; out[j++] = 'r';
        } else if (c == '\t') {
            if (j + 2 >= out_sz) break;
            out[j++] = '\\'; out[j++] = 't';
        } else if (c < 0x20) {
            if (j + 6 >= out_sz) break;
            int written = snprintf(out + j, out_sz - j, "\\u%04x", c);
            if (written < 0) break;
            j += (size_t)written;
        } else {
            if (j + 1 >= out_sz) break;
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

/* 序列化 model_config_t 到输出缓冲区 */
static void write_model_json(model_config_t *mc, char *buf, size_t sz) {
    char p[256], m[256], b[512];
    escape_json_str(mc->provider, p, sizeof(p));
    escape_json_str(mc->model, m, sizeof(m));
    escape_json_str(mc->base_url, b, sizeof(b));
    snprintf(buf, sz,
        "{\"provider\":\"%s\",\"model\":\"%s\",\"base_url\":\"%s\",\"api_key\":\"***\"}",
        p, m, b);
}

/* 序列化完整配置 */
static char *serialize_config(const router_config_t *cfg, size_t *out_len) {
    char buf[8192];
    char mb[1024];
    size_t pos = 0;
    int n;
    
    n = snprintf(buf + pos, sizeof(buf) - pos, "{\"router_model\":");
    pos += (n > 0 ? (size_t)n : 0);
    
    write_model_json((model_config_t*)&cfg->router_model, mb, sizeof(mb));
    n = snprintf(buf + pos, sizeof(buf) - pos, "%s,", mb);
    pos += (n > 0 ? (size_t)n : 0);
    
    n = snprintf(buf + pos, sizeof(buf) - pos, "\"routing_rules\":{");
    pos += (n > 0 ? (size_t)n : 0);
    
    for (int t = 0; t < TASK_UNKNOWN; t++) {
        if (t > 0) {
            n = snprintf(buf + pos, sizeof(buf) - pos, ",");
            pos += (n > 0 ? (size_t)n : 0);
        }
        n = snprintf(buf + pos, sizeof(buf) - pos, "\"%s\":", task_names[t]);
        pos += (n > 0 ? (size_t)n : 0);
        write_model_json((model_config_t*)&cfg->rules[t], mb, sizeof(mb));
        n = snprintf(buf + pos, sizeof(buf) - pos, "%s", mb);
        pos += (n > 0 ? (size_t)n : 0);
    }
    
    n = snprintf(buf + pos, sizeof(buf) - pos, "}}");
    pos += (n > 0 ? (size_t)n : 0);
    
    char *result = malloc(pos + 1);
    if (result) {
        memcpy(result, buf, pos);
        result[pos] = '\0';
    }
    *out_len = pos;
    return result;
}

/* GET /api/config */
static void handle_get_config(client_t *c, const router_config_t *cfg) {
    size_t json_len;
    char *json = serialize_config(cfg, &json_len);
    if (!json) {
        const char *err = "{\"error\":\"serialization failed\"}";
        const char *hdr = http_headers(500, "Internal Server Error", "application/json", strlen(err), 0);
        send_response(c, hdr, strlen(hdr));
        send_response(c, err, strlen(err));
        return;
    }
    const char *hdr = http_headers(200, "OK", "application/json", json_len, 0);
    send_response(c, hdr, strlen(hdr));
    send_response(c, json, json_len);
    free(json);
}

/* 从 JSON 中提取字符串值 */
static int extract_json_str(const char *json, const char *key, char *out, size_t out_sz) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *kp = strstr(json, search);
    if (!kp) return -1;
    const char *colon = strchr(kp, ':');
    if (!colon) return -1;
    colon++;
    while (*colon == ' ') colon++;
    if (*colon != '"') return -1;
    colon++;
    const char *end = strchr(colon, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - colon);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, colon, len);
    out[len] = '\0';
    return 0;
}

/* POST /api/config */
static void handle_post_config(client_t *c, const char *body, size_t blen, router_config_t *cfg) {
    (void)blen;
    const char *rm = strstr(body, "\"router_model\"");
    if (rm) {
        const char *rm_val = strchr(rm, ':');
        if (rm_val) {
            rm_val++;
            while (*rm_val == ' ') rm_val++;
            extract_json_str(rm_val, "provider", cfg->router_model.provider, sizeof(cfg->router_model.provider));
            extract_json_str(rm_val, "model", cfg->router_model.model, sizeof(cfg->router_model.model));
            extract_json_str(rm_val, "base_url", cfg->router_model.base_url, sizeof(cfg->router_model.base_url));
        }
    }
    const char *rr = strstr(body, "\"routing_rules\"");
    if (rr) {
        const char *rr_val = strchr(rr, ':');
        if (rr_val) {
            rr_val++;
            while (*rr_val == ' ') rr_val++;
            for (int t = 0; t < TASK_UNKNOWN; t++) {
                char sk[32];
                snprintf(sk, sizeof(sk), "\"%s\"", task_names[t]);
                const char *tk = strstr(rr_val, sk);
                if (!tk) continue;
                const char *tv = strchr(tk, ':');
                if (!tv) continue;
                tv++;
                while (*tv == ' ') tv++;
                extract_json_str(tv, "provider", cfg->rules[t].provider, sizeof(cfg->rules[t].provider));
                extract_json_str(tv, "model", cfg->rules[t].model, sizeof(cfg->rules[t].model));
                extract_json_str(tv, "base_url", cfg->rules[t].base_url, sizeof(cfg->rules[t].base_url));
            }
        }
    }
    config_save(cfg, CONFIG_FILE);
    fprintf(stderr, "[server] 配置已更新\n");
    const char *ok = "{\"status\":\"ok\"}";
    const char *hdr = http_headers(200, "OK", "application/json", strlen(ok), 0);
    send_response(c, hdr, strlen(hdr));
    send_response(c, ok, strlen(ok));
}

/* 管理页面 HTML */
/* 管理页面 HTML - 来自 admin_html.h (via xxd) */

/* /admin 页面 */
static void handle_admin(client_t *c) {
  const char *hdr = http_headers(200, "OK", "text/html; charset=utf-8", admin_html_len, 0);
  send_response(c, hdr, strlen(hdr));
  send_response(c, (const char *)admin_html, admin_html_len);
}

/* Chat Completions */
static void handle_chat_completions(client_t *c, const char *body, size_t blen) {
  int stream = (strstr(body, "\"stream\": true") != NULL);

  if (stream) {
    /* SSE 模式 — 流式返回 */
    const char *hdr = http_headers(200, "OK", "text/event-stream", 0, 1);
    send_response(c, hdr, strlen(hdr));
    /* TODO: 实际 SSE 流 — 目前返回一个简单的 dummy */
    c->is_streaming = 1;
  }

  http_response_t result;
  http_response_init(&result);
  char used_model[128] = {0};

  route_and_call(body, blen, &g_config, &result, used_model, sizeof(used_model));

  if (!stream) {
    /* 非流式: 直接返回完整响应 */
    const char *hdr = http_headers(200, "OK", "application/json", result.len, 0);
    send_response(c, hdr, strlen(hdr));
    send_response(c, result.data, result.len);
  } else {
    /* 流式: 完整结果 + DONE */
    /* 发送完整响应内容作为 SSE event */
    if (result.data && result.len > 0) {
      send_response(c, result.data, result.len);
    }
    send_response(c, "data: [DONE]\n\n", 15);
    c->is_streaming = 0;
  }

  http_response_free(&result);
}

/* 处理 HTTP 请求 */
static void handle_request(client_t *c) {
  char *buf = c->buf;
  size_t len = c->len;

  /* 解析请求行 */
  /* 调试: 打印最后 30 个字符 */
  const char *tail = buf + (len > 30 ? len - 30 : 0);
  fprintf(stderr, "[server] tail[%zu]: ", len);
  for (size_t i = 0; tail[i]; i++) {
    fprintf(stderr, "%02x ", (unsigned char)tail[i]);
  }
  fprintf(stderr, "\n");

  /* 先找 body 分隔符 (必须在截断 line 之前!) */
  char *body_start = strstr(buf, "\r\n\r\n");
  size_t header_len = body_start ? (size_t)(body_start - buf + 4) : len;
  size_t body_len = len - header_len;
  char *body = body_start ? body_start + 4 : NULL;

  /* 解析请求行 */
  char *line_end = strstr(buf, "\r\n");
  if (!line_end) return;
  *line_end = '\0';
  char method[16], path[512];
  if (sscanf(buf, "%15s %511s", method, path) < 2) return;

  fprintf(stderr, "[server] %s %s (%zu bytes body)\n", method, path, body_len);
  fprintf(stderr, "[server] buflen=%zu header_len=%zu body_len=%zu\n", len, header_len, body_len);
  fprintf(stderr, "[server] body starts: %.60s\n", body ? body : "NULL");

  /* 路由 */
  if (strcmp(path, "/health") == 0) {
    handle_health(c);
  } else if (strcmp(path, "/v1/models") == 0 || strcmp(path, "/api/tags") == 0) {
    handle_models(c);
  } else if (strcmp(path, "/v1/chat/completions") == 0 && strcmp(method, "POST") == 0) {
    if (body && body_len > 0) {
      handle_chat_completions(c, body, body_len);
    } else {
      const char *err = "{\"error\":\"empty body\"}";
      const char *hdr = http_headers(400, "Bad Request", "application/json", strlen(err), 0);
      send_response(c, hdr, strlen(hdr));
      send_response(c, err, strlen(err));
    }
  } else if (strcmp(path, "/admin") == 0) {
    handle_admin(c);
  } else if (strcmp(path, "/api/config") == 0) {
    if (strcmp(method, "GET") == 0) {
      handle_get_config(c, &g_config);
    } else if (strcmp(method, "POST") == 0) {
      handle_post_config(c, body, body_len, &g_config);
    } else {
      const char *err = "{\"error\":\"method not allowed\"}";
      const char *hdr = http_headers(405, "Method Not Allowed", "application/json", strlen(err), 0);
      send_response(c, hdr, strlen(hdr));
      send_response(c, err, strlen(err));
    }
  } else {
    const char *err = "{\"error\":\"not found\"}";
    const char *hdr = http_headers(404, "Not Found", "application/json", strlen(err), 0);
    send_response(c, hdr, strlen(hdr));
    send_response(c, err, strlen(err));
  }
}

/* 读取客户端数据 — 循环读取直到获取完整请求 */
static void read_client(client_t *c) {
  /* 如果已有部分数据，先清空重读 */
  c->len = 0;
  memset(c->buf, 0, BUFSIZE);

  /* 边缘触发(ET)模式需要循环读 */
  while (1) {
    ssize_t n = recv(c->fd, c->buf + c->len, BUFSIZE - c->len - 1, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      /* 错误 */
      close(c->fd);
      c->fd = -1;
      return;
    }
    if (n == 0) {
      /* 连接关闭 */
      close(c->fd);
      c->fd = -1;
      return;
    }
    c->len += (size_t)n;
    c->buf[c->len] = '\0';
  }

  if (c->len == 0) {
    close(c->fd);
    c->fd = -1;
    return;
  }

  handle_request(c);
}

/* 接受新连接 */
static void accept_conn(int listen_fd) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  int conn = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);
  if (conn < 0) return;

  set_nonblock(conn);

  /* 找个空闲 client 槽位 */
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_clients[i].fd < 0) {
      g_clients[i].fd = conn;
      g_clients[i].len = 0;
      g_clients[i].is_streaming = 0;
      g_clients[i].keep_alive = 1;

      struct epoll_event ev;
      ev.events = EPOLLIN | EPOLLET;
      ev.data.ptr = &g_clients[i];
      epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, conn, &ev);
      fprintf(stderr, "[server] 新连接 #%d\n", i);
      return;
    }
  }
  close(conn);
}

/* 加载默认配置 */
static void load_default_config(router_config_t *cfg) {
  memset(cfg, 0, sizeof(*cfg));

  /* router model */
  strcpy(cfg->router_model.provider, "ollama");
  strcpy(cfg->router_model.model, "qwen3:8b-chat");
  strcpy(cfg->router_model.base_url, "http://192.168.100.4:11434/v1");

  /* chat */
  strcpy(cfg->rules[TASK_CHAT].provider, "ollama");
  strcpy(cfg->rules[TASK_CHAT].model, "qwen3:8b-chat");
  strcpy(cfg->rules[TASK_CHAT].base_url, "http://192.168.100.4:11434/v1");

  /* code */
  strcpy(cfg->rules[TASK_CODE].provider, "deepseek");
  strcpy(cfg->rules[TASK_CODE].model, "deepseek-v4-pro");
  strcpy(cfg->rules[TASK_CODE].base_url, "https://api.deepseek.com/v1");

  /* vision */
  strcpy(cfg->rules[TASK_VISION].provider, "ollama");
  strcpy(cfg->rules[TASK_VISION].model, "minicpm-v:8b");
  strcpy(cfg->rules[TASK_VISION].base_url, "http://192.168.100.4:11434/v1");

  /* reasoning */
  strcpy(cfg->rules[TASK_REASONING].provider, "deepseek");
  strcpy(cfg->rules[TASK_REASONING].model, "deepseek-v4-pro");
  strcpy(cfg->rules[TASK_REASONING].base_url, "https://api.deepseek.com/v1");

  /* translation */
  strcpy(cfg->rules[TASK_TRANSLATION].provider, "ollama");
  strcpy(cfg->rules[TASK_TRANSLATION].model, "qwen3:8b-chat");
  strcpy(cfg->rules[TASK_TRANSLATION].base_url, "http://192.168.100.4:11434/v1");

  /* audio */
  strcpy(cfg->rules[TASK_AUDIO].provider, "ollama");
  strcpy(cfg->rules[TASK_AUDIO].model, "qwen3:8b-chat");
  strcpy(cfg->rules[TASK_AUDIO].base_url, "http://192.168.100.4:11434/v1");

  /* video */
  strcpy(cfg->rules[TASK_VIDEO].provider, "ollama");
  strcpy(cfg->rules[TASK_VIDEO].model, "qwen3:8b-chat");
  strcpy(cfg->rules[TASK_VIDEO].base_url, "http://192.168.100.4:11434/v1");
}

int main(int argc, char **argv) {
  if (argc > 1) g_port = atoi(argv[1]);

  /* 初始化配置 */
  load_default_config(&g_config);

  const char *config_path = CONFIG_FILE;
  if (config_load(&g_config, config_path) == 0) {
    fprintf(stderr, "[config] 已加载 %s\n", config_path);
  } else {
    fprintf(stderr, "[config] 使用默认配置 (未找到 %s)\n", config_path);
  }

  /* 初始化客户端数组 */
  for (int i = 0; i < MAX_CLIENTS; i++) g_clients[i].fd = -1;

  /* 创建监听 socket */
  int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listen_fd < 0) { perror("socket"); return 1; }

  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)g_port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind"); close(listen_fd); return 1;
  }
  listen(listen_fd, 128);

  /* 创建 epoll */
  g_epoll_fd = epoll_create1(0);
  if (g_epoll_fd < 0) { perror("epoll_create"); return 1; }

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = listen_fd;
  epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

  fprintf(stderr, "🚀 Model Router C v3.0\n");
  fprintf(stderr, "📍 listening on 0.0.0.0:%d\n", g_port);
  fprintf(stderr, "⚙️  规则引擎: %d keywords\n", keyword_count());
  fprintf(stderr, "💓 API: POST /v1/chat/completions\n");

  /* 事件循环 */
  struct epoll_event events[MAX_EVENTS];
  while (1) {
    int nfds = epoll_wait(g_epoll_fd, events, MAX_EVENTS, -1);
    if (nfds < 0) {
      if (errno == EINTR) continue;
      perror("epoll_wait"); break;
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == listen_fd) {
        accept_conn(listen_fd);
      } else {
        /* 直接用 ptr 找到 client */
        client_t *c = (client_t *)events[i].data.ptr;
        if (c && c->fd > 0) {
          read_client(c);
        }
      }
    }
  }

  close(listen_fd);
  close(g_epoll_fd);
  return 0;
}
