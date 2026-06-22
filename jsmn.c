/*
 * MIT License
 * Copyright (c) 2010 Serge Zaitsev
 */
#include "jsmn.h"

int jsmn_init(jsmn_parser *parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
  return 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js,
                                size_t len, jsmntok_t *tokens,
                                unsigned int num_tokens) {
  int start = (int)parser->pos;
  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    switch (js[parser->pos]) {
    case ':':
    case '\t':
    case '\r':
    case '\n':
    case ' ':
    case ',':
    case ']':
    case '}':
      goto found;
    default:
      break;
    }
  }
found:
  if (parser->toknext >= num_tokens) return -3;
  tokens[parser->toknext].type = JSMN_PRIMITIVE;
  tokens[parser->toknext].start = start;
  tokens[parser->toknext].end = parser->pos;
  parser->toknext++;
  parser->toksuper = -1;
  return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len,
                             jsmntok_t *tokens, unsigned int num_tokens) {
  int start = (int)parser->pos;
  parser->pos++;
  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    char c = js[parser->pos];
    if (c == '\"') {
      if (parser->toknext >= num_tokens) return -3;
      tokens[parser->toknext].type = JSMN_STRING;
      tokens[parser->toknext].start = start;
      tokens[parser->toknext].end = parser->pos + 1;
      parser->toknext++;
      parser->toksuper = -1;
      return 0;
    }
    if (c == '\\') parser->pos++;
  }
  return -2;
}

int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
               jsmntok_t *tokens, unsigned int num_tokens) {
  int r;
  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    char c = js[parser->pos];
    switch (c) {
    case '{':
    case '[':
      if (parser->toknext >= num_tokens) return -3;
      tokens[parser->toknext].type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
      tokens[parser->toknext].start = (int)parser->pos;
      tokens[parser->toknext].end = 0;
      tokens[parser->toknext].size = 0;
      parser->toksuper = (int)parser->toknext;
      parser->toknext++;
      break;
    case '}':
    case ']':
      if (parser->toksuper >= 0)
        tokens[parser->toksuper].end = (int)parser->pos + 1;
      for (int i = parser->toknext - 1; i >= 0; i--) {
        if (tokens[i].start == parser->toksuper &&
            (tokens[i].type == JSMN_OBJECT || tokens[i].type == JSMN_ARRAY) &&
            tokens[i].end == 0) {
          tokens[i].end = (int)parser->pos + 1;
          break;
        }
      }
      parser->toksuper = -1;
      break;
    case '\"':
      r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
      if (r < 0) return r;
      break;
    case '\t':
    case '\r':
    case '\n':
    case ' ':
    case ',':
    case ':':
      break;
    default:
      r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
      if (r < 0) return r;
      break;
    }
  }
  return (int)parser->toknext;
}
