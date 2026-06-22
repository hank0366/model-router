#include "model_router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>

/* HTTPS curl fallback 前向声明 */
static int http_post_curl(const char *url, const char *body, size_t body_len,
                           const char *api_key, int stream, http_response_t *resp);

/*
 * HTTP 客户端
 * HTTP → POSIX sockets
 * HTTPS → 调用外部 curl (需要 curl 在 PATH 中)
 */

void http_response_free(http_response_t *r) {
  if (r->data) free(r->data);
  r->data = NULL;
  r->len = 0;
  r->cap = 0;
}

int http_response_append(http_response_t *r, const char *data, size_t len) {
  if (r->len + len >= r->cap) {
    size_t newcap = r->cap ? r->cap * 2 : 65536;
    while (r->len + len >= newcap) newcap *= 2;
    char *nd = realloc(r->data, newcap);
    if (!nd) return -1;
    r->data = nd;
    r->cap = newcap;
  }
  memcpy(r->data + r->len, data, len);
  r->len += len;
  r->data[r->len] = '\0';
  return 0;
}

static int is_https(const char *url) {
  return strncmp(url, "https://", 8) == 0;
}

static int connect_host(const char *host, int port) {
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  int gai = getaddrinfo(host, port_str, &hints, &res);
  if (gai) return -1;

  int fd = -1;
  for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

/* 解析 url, 提取 host/port/path */
static int parse_url(const char *url, char *host, size_t host_sz, int *port, char *path, size_t path_sz) {
  const char *p = url;
  if (strncmp(p, "http://", 7) == 0) p += 7;
  else if (strncmp(p, "https://", 8) == 0) p += 8;

  const char *colon = strchr(p, ':');
  const char *slash = strchr(p, '/');
  size_t host_len;
  if (colon && (!slash || colon < slash)) {
    host_len = (size_t)(colon - p);
    *port = atoi(colon + 1);
  } else if (slash) {
    host_len = (size_t)(slash - p);
    *port = 80;
  } else {
    host_len = strlen(p);
    *port = 80;
  }
  if (host_len >= host_sz) host_len = host_sz - 1;
  memcpy(host, p, host_len);
  host[host_len] = '\0';

  if (slash) {
    snprintf(path, path_sz, "%s", slash);
  } else {
    snprintf(path, path_sz, "/");
  }

  /* 去掉 URL 中的端口号(仅用于提取 host) — 已经处理过了 */
  return 0;
}

int http_post(const char *url, const char *body, size_t body_len,
              const char *api_key, int stream, http_response_t *resp) {
  char host[256], path[512];
  int port;
  if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0)
    return -1;

  int fd = connect_host(host, port);
  if (fd < 0) return -1;

  /* 对于 HTTPS, 用 curl */
  if (is_https(url)) {
    return http_post_curl(url, body, body_len, api_key, stream, resp);
  }

  /* 构建 HTTP 请求 */
  char req[8192];
  int reqlen = snprintf(req, sizeof(req),
    "POST %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %zu\r\n"
    "%s%s%s"
    "Connection: close\r\n"
    "\r\n",
    path, host, body_len,
    (api_key && api_key[0] && strcmp(api_key, "***") != 0) ? "Authorization: Bearer " : "",
    (api_key && api_key[0] && strcmp(api_key, "***") != 0) ? api_key : "",
    (api_key && api_key[0] && strcmp(api_key, "***") != 0) ? "\r\n" : ""
  );

  if (send(fd, req, (size_t)reqlen, 0) < 0) { close(fd); return -1; }
  if (send(fd, body, body_len, 0) < 0) { close(fd); return -1; }

  /* 读响应 */
  char buf[65536];
  http_response_init(resp);

  struct pollfd pfd = { .fd = fd, .events = POLLIN };
  int timeout = stream ? 300000 : 60000;

  while (1) {
    int r = poll(&pfd, 1, timeout);
    if (r <= 0) break;
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) break;

    if (stream) {
      /* SSE 透传 — 原样追加 */
      if (http_response_append(resp, buf, (size_t)n) < 0) break;
    } else {
      if (http_response_append(resp, buf, (size_t)n) < 0) break;
    }
  }

  close(fd);

  /* 解析 HTTP 响应头，提取 body */
  char *body_start = strstr(resp->data, "\r\n\r\n");
  if (body_start) {
    body_start += 4;
    size_t header_len = (size_t)(body_start - resp->data);
    size_t body_sz = resp->len - header_len;
    memmove(resp->data, body_start, body_sz);
    resp->len = body_sz;
    resp->data[body_sz] = '\0';
  }

  return 0;
}

/* HTTPS: 通过外部 curl 调用 */
static int http_post_curl(const char *url, const char *body, size_t body_len,
                           const char *api_key, int stream, http_response_t *resp) {
  /* 构建临时文件路径 */
  char tmpfile[] = "/tmp/mr_curl_XXXXXX";
  int fd = mkstemp(tmpfile);
  if (fd < 0) return -1;
  write(fd, body, body_len);
  close(fd);

  char cmd[8192];
  int n;
  if (api_key && api_key[0] && strcmp(api_key, "***") != 0) {
    n = snprintf(cmd, sizeof(cmd),
      "curl -s --max-time 10 -w '\\n%%{http_code}' -X POST '%s' "
      "-H 'Content-Type: application/json' "
      "-H 'Authorization: Bearer %s' "
      "-d @%s 2>/dev/null",
      url, api_key, tmpfile);
  } else {
    n = snprintf(cmd, sizeof(cmd),
      "curl -s --max-time 10 -w '\\n%%{http_code}' -X POST '%s' "
      "-H 'Content-Type: application/json' "
      "-d @%s 2>/dev/null",
      url, tmpfile);
  }
  (void)n;

  FILE *f = popen(cmd, "r");
  if (!f) { unlink(tmpfile); return -1; }

  char buf[4096];
  size_t total = 0;
  while (fgets(buf, sizeof(buf), f)) {
    total += strlen(buf);
    http_response_append(resp, buf, strlen(buf));
  }
  int status = pclose(f);
  unlink(tmpfile);

  if (status != 0) {
    http_response_free(resp);
    return -1;
  }

  /* 去掉 curl 的 http_code 尾行 (最后一行是 \n200 格式) */
  char *last_newline = strrchr(resp->data, '\n');
  if (last_newline && last_newline > resp->data) {
    char *prev_newline = last_newline;
    while (prev_newline > resp->data && *(prev_newline - 1) == '\n') prev_newline--;
    /* 去掉最后一个换行及其后的 http_code */
    /* 通常 resp 格式是: {json}\n200 — 去掉 \n200 */
    char *http_code_pos = resp->data;
    for (char *p = resp->data; p < resp->data + resp->len; p++) {
      if (*p < '0' || *p > '9') http_code_pos = p + 1;
      else break;
    }
    /* 简单版：去掉最后一行（换行后的数字） */
    *last_newline = '\0';
    resp->len = (size_t)(last_newline - resp->data);
  }

  return 0;
}
