# 智能模型路由服务 v2.0

[![Version](https://img.shields.io/badge/version-2.0.0-blue)](https://github.com)
[![Python](https://img.shields.io/badge/python-3.10%2B-green)](https://python.org)
[![License](https://img.shields.io/badge/license-MIT-orange)](https://opensource.org/licenses/MIT)

一个 OpenAI 兼容的智能路由代理，根据请求内容动态调用不同的后端模型。附带 Web 配置管理面板。

## ✨ 新特性 (v2.0)

- 🎛️ **Web 配置面板** — 可视化编辑一切，无需改代码
- 💾 **配置持久化** — 修改即时保存到 `config.json`
- 🔄 **热更新** — 修改配置无需重启服务
- 🏷️ **GitHub 风格 UI** — 深色主题，仿 OpenClaw 配置体验

## 🚀 快速开始

```bash
cd model-router
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# 编辑 .env 配置 Ollama 地址
source .env

# 启动
python server.py
```

## 📡 端口说明

| 地址 | 用途 |
|------|------|
| `http://localhost:8000/v1/chat/completions` | OpenAI 兼容 API |
| `http://localhost:8000/admin` | Web 配置管理面板 |
| `http://localhost:8000/docs` | API 文档 (Swagger) |
| `http://localhost:8000/health` | 健康检查 |

## ⚙️ Web 配置面板

访问 `http://localhost:8000/admin` 打开管理面板，你可以：

### 配置主模型
分析请求意图的路由模型，建议用轻量快速的模型。

### 配置路由规则
为 7 种任务类型分别指定后端模型：

| 任务类型 | 默认模型 | 说明 |
|---------|---------|------|
| 🔍 主模型 | qwen3:8b-chat | 意图分类 |
| 👁️ 图像理解 | minicpm-v:8b | 多模态视觉 |
| 💻 代码生成 | qwen3:8b-chat | 代码编写/调试 |
| 🧠 复杂推理 | qwen3:8b | 数学/逻辑问题 |
| 🌐 翻译任务 | qwen3:8b-chat | 多语言翻译 |
| 💬 普通对话 | qwen3:8b-chat | 闲聊/问答 |
| 🎵 音频处理 | qwen3:8b-chat | 音频分析 |
| 🎬 视频分析 | qwen3:8b-chat | 视频理解 |

所有修改即时生效，自动持久化。

## 🔗 集成到 OpenClaw

```bash
openclaw gateway config.patch
```

```json
{
  "models": {
    "providers": {
      "model-router": {
        "driver": "openai",
        "apiKey": "***",
        "baseURL": "http://localhost:8000/v1"
      }
    },
    "entries": {
      "smart-router": {
        "name": "Smart Router",
        "provider": "model-router",
        "model": "router-v1"
      }
    }
  }
}
```

## 📁 文件结构

```
model-router/
├── server.py           # 主服务（路由 + API + 管理面板）
├── config.json         # 持久化配置文件（自动生成）
├── requirements.txt    # Python 依赖
├── .env                # 环境变量
├── .env.example        # 环境变量模板
├── test_router.py      # 测试脚本
└── README.md           # 本文档
```

## License

MIT
