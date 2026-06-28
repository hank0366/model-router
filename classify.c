#include "model_router.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/*
 * 关键词规则匹配引擎
 * 使用预编译的关键词表 + 循环 Aho-Corasick 风格匹配
 * O(N*K) 最坏 N=消息长度, K=总关键词长度
 */

typedef struct {
  const char *keyword;
  task_type_t type;
} keyword_entry_t;

/* 所有关键词表 (小写) */
static const keyword_entry_t keyword_table[] = {
  /* code */
  {"def ",       TASK_CODE},
  {"class ",     TASK_CODE},
  {"function",   TASK_CODE},
  {"代码",       TASK_CODE},
  {"算法",       TASK_CODE},
  {"函数",       TASK_CODE},
  {"编写",       TASK_CODE},
  {"实现",       TASK_CODE},
  {"怎么写",     TASK_CODE},
  {"如何实现",   TASK_CODE},
  {"debug",      TASK_CODE},
  {"bug",        TASK_CODE},
  {"import ",    TASK_CODE},
  {"package",    TASK_CODE},
  {"python",     TASK_CODE},
  {"javascript", TASK_CODE},
  {"java ",      TASK_CODE},
  {"c++",        TASK_CODE},
  {"rust",       TASK_CODE},
  {"golang",     TASK_CODE},

  /* translation */
  {"翻译",       TASK_TRANSLATION},
  {"translate",  TASK_TRANSLATION},
  {" 英文",      TASK_TRANSLATION},
  {" 中文",      TASK_TRANSLATION},
  {" 日语",      TASK_TRANSLATION},
  {" 法语",      TASK_TRANSLATION},
  {"成英文",     TASK_TRANSLATION},
  {"成中文",     TASK_TRANSLATION},
  {"用英语",     TASK_TRANSLATION},
  {"用中文",     TASK_TRANSLATION},
  {"英语翻译",   TASK_TRANSLATION},
  {"中文翻译",   TASK_TRANSLATION},

  /* reasoning */
  {"证明",       TASK_REASONING},
  {"数学",       TASK_REASONING},
  {"计算",       TASK_REASONING},
  {"逻辑",       TASK_REASONING},
  {"推理",       TASK_REASONING},
  {"概率",       TASK_REASONING},
  {"统计",       TASK_REASONING},
  {"微积分",     TASK_REASONING},
  {"代数",       TASK_REASONING},
  {"多少次",     TASK_REASONING},
  {"等于",       TASK_REASONING},
  {"一共",       TASK_REASONING},
  {"多少个",     TASK_REASONING},
  {"proof",      TASK_REASONING},
  {"math",       TASK_REASONING},

  /* audio */
  {"音频",       TASK_AUDIO},
  {"audio",      TASK_AUDIO},
  {"声音",       TASK_AUDIO},
  {"语音",       TASK_AUDIO},

  /* video */
  {"视频",       TASK_VIDEO},
  {"video",      TASK_VIDEO},
  {"影片",       TASK_VIDEO},

  /* sentinel */
  {NULL, TASK_CHAT}
};

#define KEYWORD_COUNT (int)(sizeof(keyword_table) / sizeof(keyword_table[0]))

/* 小写化并复制 */
static void to_lower_copy(const char *src, char *dst, size_t sz) {
  size_t i;
  for (i = 0; i < sz - 1 && src[i]; i++)
    dst[i] = (char)tolower((unsigned char)src[i]);
  dst[i] = '\0';
}

/* 在字符串中找关键词 */
static int str_contains_lower(const char *lower_text, const char *keyword) {
  return strstr(lower_text, keyword) != NULL;
}

/* 规则匹配入口 */
task_type_t classify_by_rules(const char *text) {
  if (!text || !*text) return TASK_CHAT;

  /* 小写化 text */
  size_t tlen = strlen(text);
  char *lower = malloc(tlen + 1);
  if (!lower) return TASK_CHAT;
  to_lower_copy(text, lower, tlen + 1);

  task_type_t result = TASK_CHAT;

  for (int i = 0; keyword_table[i].keyword; i++) {
    if (str_contains_lower(lower, keyword_table[i].keyword)) {
      result = keyword_table[i].type;
      break;
    }
  }

  free(lower);
  return result;
}

/* 规则匹配入口（带关键词追踪） */
task_type_t classify_by_rules_with_keyword(const char *text, char *matched_kw, size_t kw_sz) {
  if (!text || !*text) {
    if (matched_kw && kw_sz > 0) matched_kw[0] = '\0';
    return TASK_CHAT;
  }

  size_t tlen = strlen(text);
  char *lower = malloc(tlen + 1);
  if (!lower) { if (matched_kw && kw_sz > 0) matched_kw[0] = '\0'; return TASK_CHAT; }
  to_lower_copy(text, lower, tlen + 1);

  task_type_t result = TASK_CHAT;

  for (int i = 0; keyword_table[i].keyword; i++) {
    if (str_contains_lower(lower, keyword_table[i].keyword)) {
      result = keyword_table[i].type;
      if (matched_kw && kw_sz > 0) {
        strncpy(matched_kw, keyword_table[i].keyword, kw_sz - 1);
        matched_kw[kw_sz - 1] = '\0';
      }
      break;
    }
  }

  free(lower);
  return result;
}

/* 用 LLM 分类 */
task_type_t classify_with_llm(const char *text, const model_config_t *router) {
  (void)router;
  (void)text;
  /* TODO: 通过 HTTP POST 调用 router_model 做意图判断 */
  return TASK_CHAT;
}

/* 返回关键词数量（用于日志） */
int keyword_count(void) {
  return KEYWORD_COUNT - 1; /* -1 for sentinel */
}
