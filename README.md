# Model Router C v3.0

纯 C 语言实现的智能模型路由代理（零外部依赖）

## 特性

- **高速：** epoll 事件驱动，启动 2ms，内存 1.2MB（Python 版 40+MB）
- **智能路由：** 54 关键词规则引擎（聊天/代码/翻译/推理/音视频）
- **全免费后端：** [Agnes AI](https://platform.agnes-ai.com) 全模态 API
- **OpenAI 兼容：** 标准 `/v1/chat/completions` API
- **轻量化：** 仅依赖 POSIX socket + pthread

## 快速开始

```bash
make          # 编译
./model_router 8000  # 启动
```

## 配置

编辑 `config.json`：

| 路由 | 默认模型 | 费用 |
|------|---------|------|
| chat | agnes-2.0-flash | 免费 |
| code | agnes-2.0-flash | 免费 |
| reasoning | agnes-2.0-flash | 免费 |
| translation | agnes-2.0-flash | 免费 |
| audio | agnes-2.0-flash | 免费 |
| video | agnes-2.0-flash | 免费 |
| vision | minicpm-v:8b (本地) | 免费 |

## 升级说明 (v2 Python → v3 C)

- 彻底脱离 Python 运行时
- 内存占用降低 97%：40MB → 1.2MB
- 启动时间：2ms（Python 版 ~500ms）
- 后端从 DeepSeek API 切换到 Agnes 免费 API
- 月省 ¥100+

## 依赖

- Linux (epoll)
- gcc (C11)
- libcurl（可选，HTTPS 回退用）
