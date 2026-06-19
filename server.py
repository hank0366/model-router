#!/usr/bin/env python3
"""
智能模型路由服务器
提供 OpenAI 兼容接口 + Web 配置管理面板
"""
import os
import json
import time
import asyncio
from pathlib import Path
from typing import List, Dict, Any, Optional, AsyncIterator
from enum import Enum

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import StreamingResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
import httpx

# ============================================================================
# 配置持久化
# ============================================================================
CONFIG_FILE = Path(__file__).parent / "config.json"
OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL", "http://192.168.100.4:11434/v1")
DEFAULT_CONFIG = {
    "router_model": {
        "provider": "ollama",
        "model": "qwen3:8b-chat",
        "base_url": OLLAMA_BASE_URL,
        "api_key": "***"
    },
    "routing_rules": {
        "vision": {
            "provider": "ollama",
            "model": "minicpm-v:8b",
            "base_url": OLLAMA_BASE_URL,
            "api_key": "***"
        },
        "code": {
            "provider": "ollama",
            "model": "qwen3:8b-chat",
            "base_url": OLLAMA_BASE_URL,
            "api_key": "***"
        },
        "reasoning": {
            "provider": "ollama",
            "model": "qwen3:8b",
            "base_url": OLLAMA_BASE_URL,
            "api_key": "***"
        },
        "translation": {
            "provider": "ollama",
            "model": "qwen3:8b-chat",
            "base_url": OLLAMA_BASE_URL,
            "api_key": "***"
        },
        "chat": {
            "provider": "ollama",
            "model": "qwen3:8b-chat",
            "base_url": OLLAMA_BASE_URL,
            "api_key": "***"
        },
        "audio": {
            "provider": "ollama",
            "model": "qwen3:8b-chat",
            "base_url": OLLAMA_BASE_URL,
            "api_key": "***"
        },
        "video": {
            "provider": "ollama",
            "model": "qwen3:8b-chat",
            "base_url": OLLAMA_BASE_URL,
            "api_key": "***"
        },
    }
}

def load_config():
    if CONFIG_FILE.exists():
        with open(CONFIG_FILE) as f:
            saved = json.load(f)
        merged = DEFAULT_CONFIG.copy()
        merged.update(saved)
        if "routing_rules" in saved:
            merged["routing_rules"] = {**DEFAULT_CONFIG["routing_rules"], **saved["routing_rules"]}
        return merged
    return DEFAULT_CONFIG

def save_config(cfg: dict):
    with open(CONFIG_FILE, "w") as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)

CONFIG = load_config()
ROUTER_MODEL = CONFIG["router_model"]
ROUTING_RULES = CONFIG["routing_rules"]

# ============================================================================
# 数据模型
# ============================================================================

class TaskType(str, Enum):
    VISION = "vision"
    CODE = "code"
    REASONING = "reasoning"
    TRANSLATION = "translation"
    CHAT = "chat"
    AUDIO = "audio"
    VIDEO = "video"

TASK_LABELS = {
    "vision": "图像理解",
    "code": "代码生成",
    "reasoning": "复杂推理",
    "translation": "翻译任务",
    "chat": "普通对话",
    "audio": "音频处理",
    "video": "视频分析",
}

class ProviderConfig(BaseModel):
    provider: str
    model: str
    base_url: str
    api_key: str = "***"

class Message(BaseModel):
    role: str
    content: str | List[Dict[str, Any]]

class ChatCompletionRequest(BaseModel):
    model: str
    messages: List[Message]
    temperature: Optional[float] = 0.7
    max_tokens: Optional[int] = None
    stream: Optional[bool] = False
    top_p: Optional[float] = 1.0
    frequency_penalty: Optional[float] = 0
    presence_penalty: Optional[float] = 0

class Usage(BaseModel):
    prompt_tokens: int
    completion_tokens: int
    total_tokens: int

class Choice(BaseModel):
    index: int
    message: Message
    finish_reason: Optional[str] = None

class ChatCompletionResponse(BaseModel):
    id: str
    object: str = "chat.completion"
    created: int
    model: str
    choices: List[Choice]
    usage: Optional[Usage] = None

# ============================================================================
# 核心路由逻辑
# ============================================================================

class ModelRouter:
    def __init__(self):
        self.client = httpx.AsyncClient(timeout=300.0)

    async def classify_intent(self, messages: List[Message]) -> TaskType:
        last_user_msg = None
        for msg in reversed(messages):
            if msg.role == "user":
                last_user_msg = msg
                break
        if not last_user_msg:
            return TaskType.CHAT

        if isinstance(last_user_msg.content, list):
            for item in last_user_msg.content:
                if isinstance(item, dict) and item.get("type") == "image_url":
                    return TaskType.VISION

        text_content = ""
        if isinstance(last_user_msg.content, str):
            text_content = last_user_msg.content
        elif isinstance(last_user_msg.content, list):
            text_content = " ".join([
                item.get("text", "") for item in last_user_msg.content
                if isinstance(item, dict) and item.get("type") == "text"
            ])

        rule_based_type = self._classify_by_rules(text_content)
        if rule_based_type:
            print(f"📋 规则匹配: {rule_based_type.value}")
            return rule_based_type

        try:
            return await self._classify_with_model(text_content)
        except Exception as e:
            print(f"意图分类失败: {e}, 使用默认类型")
            return TaskType.CHAT

    def _classify_by_rules(self, text: str) -> Optional[TaskType]:
        text_lower = text.lower()

        code_keywords = [
            "代码", "函数", "class", "def", "function", "algorithm", "算法",
            "bug", "调试", "debug", "import", "package", "库", "框架",
            "写一个", "实现", "编写", "怎么写", "如何实现",
            "python", "javascript", "java", "c++", "rust", "golang",
        ]
        if any(kw in text_lower for kw in code_keywords):
            return TaskType.CODE

        translation_keywords = [
            "翻译", "translate", "英文", "中文", "日语", "法语",
            "成英文", "成中文", "用英语", "用中文",
        ]
        if any(kw in text_lower for kw in translation_keywords):
            return TaskType.TRANSLATION

        reasoning_keywords = [
            "推理", "证明", "数学", "计算", "逻辑", "proof", "math",
            "如果", "假设", "那么", "因此", "所以",
            "概率", "统计", "微积分", "代数",
            "多少次", "多少种", "握手", "组合", "排列", "等于", "总共", "一共",
        ]
        if any(kw in text_lower for kw in reasoning_keywords):
            return TaskType.REASONING

        if any(kw in text_lower for kw in ["音频", "视频", "audio", "video", "声音"]):
            return TaskType.VIDEO if ("视频" in text_lower or "video" in text_lower) else TaskType.AUDIO

        return None

    async def _classify_with_model(self, text_content: str) -> TaskType:
        classification_prompt = f"""分析以下用户请求，判断任务类型。只返回一个词，从以下选项中选择：
- vision: 图像理解、图片分析
- code: 代码编写、代码调试、代码审查
- reasoning: 复杂推理、数学问题、逻辑分析
- translation: 翻译任务
- chat: 普通对话、闲聊
- audio: 音频处理
- video: 视频分析

用户请求: {text_content}

任务类型:"""
        response = await self._call_model(
            provider_config=ROUTER_MODEL,
            messages=[{"role": "user", "content": classification_prompt}],
            temperature=0.1, max_tokens=10,
        )
        task_type_str = response.strip().lower()
        for task_type in TaskType:
            if task_type.value in task_type_str:
                print(f"🤖 模型分类: {task_type.value}")
                return task_type
        return TaskType.CHAT

    async def _call_model(
        self,
        provider_config: Dict[str, str],
        messages: List[Dict[str, Any]],
        temperature: float = 0.7,
        max_tokens: Optional[int] = None,
        stream: bool = False,
    ):
        headers = {"Content-Type": "application/json"}
        api_key = provider_config.get("api_key", "")
        if api_key and api_key != "***":
            headers["Authorization"] = f"Bearer {api_key}"

        payload = {
            "model": provider_config["model"],
            "messages": messages,
            "temperature": temperature,
            "stream": stream,
        }
        if max_tokens:
            payload["max_tokens"] = max_tokens
        else:
            payload["max_tokens"] = 4096

        url = f"{provider_config['base_url']}/chat/completions"

        if stream:
            return self._stream_response(url, headers, payload)
        else:
            response = await self.client.post(url, headers=headers, json=payload)
            response.raise_for_status()
            data = response.json()
            content = data["choices"][0]["message"].get("content", "")
            if not content:
                reasoning = data["choices"][0]["message"].get("reasoning", "")
                if reasoning:
                    content = reasoning
            return content

    async def _stream_response(self, url: str, headers: Dict, payload: Dict):
        async with self.client.stream("POST", url, headers=headers, json=payload) as response:
            response.raise_for_status()
            async for line in response.aiter_lines():
                if line.startswith("data: "):
                    chunk = line[6:]
                    if chunk.strip() == "[DONE]":
                        break
                    yield chunk + "\n"

    async def route_and_call(self, messages, temperature=0.7, max_tokens=None, stream=False):
        task_type = await self.classify_intent(messages)
        print(f"🎯 检测到任务类型: {task_type.value}")

        provider_config = ROUTING_RULES.get(task_type.value, ROUTING_RULES["chat"])
        print(f"📡 路由到模型: {provider_config['model']}")

        formatted_messages = [{"role": msg.role, "content": msg.content} for msg in messages]

        result = await self._call_model(
            provider_config=provider_config,
            messages=formatted_messages,
            temperature=temperature, max_tokens=max_tokens, stream=stream,
        )
        return result, provider_config["model"]

# ============================================================================
# FastAPI 应用
# ============================================================================
app = FastAPI(title="智能模型路由服务", version="2.0.0")
model_router = ModelRouter()

# OpenAI 兼容接口
@app.post("/v1/chat/completions")
async def chat_completions(request: ChatCompletionRequest):
    try:
        if request.stream:
            async def generate():
                stream, actual_model = await model_router.route_and_call(
                    messages=request.messages, temperature=request.temperature,
                    max_tokens=request.max_tokens, stream=True,
                )
                async for chunk in stream:
                    yield f"data: {chunk}"
                yield "data: [DONE]\n"
            return StreamingResponse(generate(), media_type="text/event-stream")
        else:
            content, actual_model = await model_router.route_and_call(
                messages=request.messages, temperature=request.temperature,
                max_tokens=request.max_tokens, stream=False,
            )
            return ChatCompletionResponse(
                id=f"chatcmpl-{int(time.time())}", created=int(time.time()),
                model=actual_model,
                choices=[Choice(index=0, message=Message(role="assistant", content=content), finish_reason="stop")],
                usage=Usage(prompt_tokens=0, completion_tokens=0, total_tokens=0),
            )
    except httpx.HTTPStatusError as e:
        raise HTTPException(status_code=e.response.status_code, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"路由失败: {str(e)}")

@app.get("/v1/models")
async def list_models():
    return {
        "object": "list",
        "data": [{"id": "router-v1", "object": "model", "created": int(time.time()), "owned_by": "model-router"}],
    }

@app.get("/health")
async def health_check():
    return {"status": "ok", "version": "2.0.0"}

# ============================================================================
# 配置管理 API
# ============================================================================

@app.get("/api/config")
async def api_get_config():
    """获取完整配置"""
    return load_config()

@app.post("/api/config/main-model")
async def api_update_main_model(body: ProviderConfig):
    """更新主模型（路由模型）"""
    global ROUTER_MODEL
    cfg = load_config()
    cfg["router_model"] = body.model_dump()
    save_config(cfg)
    CONFIG.update(cfg)
    ROUTER_MODEL = cfg["router_model"]
    return {"ok": True, "router_model": ROUTER_MODEL}

@app.post("/api/config/task/{task_type}")
async def api_update_task_model(task_type: str, body: ProviderConfig):
    """更新某个任务类型的后端模型"""
    global ROUTING_RULES
    if task_type not in TASK_LABELS:
        raise HTTPException(404, f"Unknown task type: {task_type}")
    cfg = load_config()
    cfg["routing_rules"][task_type] = body.model_dump()
    save_config(cfg)
    CONFIG.update(cfg)
    ROUTING_RULES = cfg["routing_rules"]
    return {"ok": True, "task_type": task_type, "config": ROUTING_RULES[task_type]}

@app.post("/api/config/reset")
async def api_reset_config():
    """重置为默认配置"""
    global ROUTING_RULES, ROUTER_MODEL
    if CONFIG_FILE.exists():
        CONFIG_FILE.unlink()
    save_config(DEFAULT_CONFIG)
    CONFIG.clear(); CONFIG.update(DEFAULT_CONFIG)
    ROUTER_MODEL = CONFIG["router_model"]
    ROUTING_RULES = CONFIG["routing_rules"]
    return {"ok": True}

# ============================================================================
# Web 管理面板
# ============================================================================

ADMIN_HTML = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>模型路由管理器</title>
<style>
  :root {
    --bg: #0d1117; --card: #161b22; --border: #30363d;
    --text: #c9d1d9; --text-dim: #8b949e; --accent: #58a6ff;
    --green: #3fb950; --orange: #d2991d; --red: #f85149;
    --input-bg: #0d1117; --btn-bg: #21262d; --btn-hover: #30363d;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: var(--bg); color: var(--text); line-height: 1.5; }
  .container { max-width: 1200px; margin: 0 auto; padding: 24px 16px; }
  header { border-bottom: 1px solid var(--border); padding-bottom: 16px; margin-bottom: 24px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 12px; }
  h1 { font-size: 24px; display: flex; align-items: center; gap: 8px; }
  h1 svg { width: 28px; height: 28px; }
  h2 { font-size: 18px; margin-bottom: 12px; display: flex; align-items: center; gap: 8px; }
  .badge { display: inline-block; padding: 2px 8px; border-radius: 12px; font-size: 11px; font-weight: 600; }
  .badge-green { background: rgba(63,185,80,0.15); color: var(--green); }
  .badge-blue { background: rgba(88,166,255,0.15); color: var(--accent); }
  .card { background: var(--card); border: 1px solid var(--border); border-radius: 8px; padding: 20px; margin-bottom: 20px; }
  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
  .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; }
  @media (max-width: 768px) { .grid-2, .grid-3 { grid-template-columns: 1fr; } }
  .form-group { margin-bottom: 14px; }
  .form-group label { display: block; font-size: 13px; color: var(--text-dim); margin-bottom: 4px; font-weight: 500; }
  .form-group input, .form-group select { width: 100%; padding: 8px 12px; background: var(--input-bg); border: 1px solid var(--border); border-radius: 6px; color: var(--text); font-size: 14px; }
  .form-group input:focus, .form-group select:focus { outline: none; border-color: var(--accent); box-shadow: 0 0 0 2px rgba(88,166,255,0.2); }
  .btn { display: inline-flex; align-items: center; gap: 6px; padding: 8px 16px; border: 1px solid var(--border); border-radius: 6px; background: var(--btn-bg); color: var(--text); font-size: 13px; cursor: pointer; transition: all 0.15s; }
  .btn:hover { background: var(--btn-hover); }
  .btn-primary { background: #238636; border-color: #238636; color: white; }
  .btn-primary:hover { background: #2ea043; }
  .btn-danger { background: transparent; border-color: var(--red); color: var(--red); }
  .btn-danger:hover { background: rgba(248,81,73,0.1); }
  .btn-sm { padding: 4px 10px; font-size: 12px; }
  .task-card { display: flex; align-items: center; justify-content: space-between; padding: 14px 16px; }
  .task-info { display: flex; align-items: center; gap: 12px; }
  .task-icon { width: 36px; height: 36px; border-radius: 8px; display: flex; align-items: center; justify-content: center; font-size: 18px; }
  .task-icon.code { background: rgba(88,166,255,0.15); }
  .task-icon.chat { background: rgba(63,185,80,0.15); }
  .task-icon.vision { background: rgba(210,153,29,0.15); }
  .task-icon.reasoning { background: rgba(188,140,255,0.15); }
  .task-icon.translation { background: rgba(255,123,114,0.15); }
  .task-icon.audio { background: rgba(121,192,255,0.15); }
  .task-icon.video { background: rgba(255,171,145,0.15); }
  .task-model { color: var(--accent); font-size: 13px; }
  .task-provider { color: var(--text-dim); font-size: 12px; }
  .modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.6); z-index: 100; align-items: center; justify-content: center; }
  .modal-overlay.active { display: flex; }
  .modal { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 24px; width: 90%; max-width: 520px; max-height: 90vh; overflow-y: auto; }
  .modal h3 { margin-bottom: 16px; font-size: 16px; }
  .modal-actions { display: flex; gap: 8px; justify-content: flex-end; margin-top: 20px; }
  .toast { position: fixed; top: 16px; right: 16px; padding: 12px 20px; border-radius: 8px; font-size: 14px; z-index: 200; animation: slideIn 0.25s; display: none; }
  .toast.success { background: #238636; color: white; }
  .toast.error { background: #da3633; color: white; }
  @keyframes slideIn { from { transform: translateX(100%); opacity: 0; } to { transform: translateX(0); opacity: 1; } }
  .status-dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block; }
  .status-dot.online { background: var(--green); }
  .status-dot.offline { background: var(--red); }
  .arrow { color: var(--text-dim); margin: 0 4px; }
  .empty-state { text-align: center; padding: 40px; color: var(--text-dim); }
</style>
</head>
<body>
<div class="container">
  <header>
    <div>
      <h1>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 7l-8-4-8 4m16 0l-8 4m8-4v10l-8 4m0-10L4 7m8 4v10M4 7v10l8 4"/></svg>
        模型路由管理器
        <span class="badge badge-green">v2.0</span>
      </h1>
      <p style="color: var(--text-dim); font-size: 14px; margin-top: 4px;">智能意图识别 & 动态模型路由</p>
    </div>
    <div style="display: flex; gap: 8px; align-items: center;">
      <span class="status-dot online" id="statusDot"></span>
      <span style="font-size: 13px; color: var(--text-dim);" id="statusText">运行中</span>
    </div>
  </header>

  <!-- 主模型配置 -->
  <div class="card" id="mainModelCard">
    <h2>
      <span style="color: var(--orange);">🔍</span> 主模型（意图分类）
      <span class="badge badge-blue">Router Model</span>
    </h2>
    <p style="color: var(--text-dim); font-size: 13px; margin-bottom: 16px;">
      用于分析用户请求意图的主模型，轻量快速即可。所有请求先经过它判断任务类型，再路由到对应的后端模型。
    </p>
    <div style="display: flex; gap: 12px; flex-wrap: wrap;">
      <div class="form-group" style="flex: 1; min-width: 120px;">
        <label>提供商</label>
        <input type="text" id="main-provider" placeholder="ollama" value="">
      </div>
      <div class="form-group" style="flex: 2; min-width: 180px;">
        <label>模型名称</label>
        <input type="text" id="main-model" placeholder="qwen3:8b-chat" value="">
      </div>
      <div class="form-group" style="flex: 2; min-width: 220px;">
        <label>API 地址</label>
        <input type="text" id="main-baseurl" placeholder="http://192.168.100.4:11434/v1" value="">
      </div>
      <div class="form-group" style="flex: 1; min-width: 120px;">
        <label>API Key</label>
        <input type="password" id="main-apikey" placeholder="***" value="">
      </div>
      <div style="display: flex; align-items: flex-end; padding-bottom: 2px;">
        <button class="btn btn-primary" onclick="saveMainModel()">💾 保存主模型</button>
      </div>
    </div>
  </div>

  <!-- 路由规则 -->
  <div class="card">
    <h2>
      <span style="color: var(--accent);">🔀</span> 路由规则配置
      <span class="badge badge-blue">7 个任务类型</span>
    </h2>
    <p style="color: var(--text-dim); font-size: 13px; margin-bottom: 16px;">
      为每种任务类型配置后端模型。根据请求意图自动路由到最合适的模型。
    </p>
    <div id="taskList" class="grid-2">
      <!-- 动态生成 -->
    </div>
  </div>

  <div style="display: flex; gap: 8px; justify-content: flex-end;">
    <button class="btn btn-danger" onclick="resetConfig()">🔄 重置为默认配置</button>
  </div>
</div>

<!-- 编辑弹窗 -->
<div class="modal-overlay" id="editModal">
  <div class="modal">
    <h3 id="modalTitle">编辑路由模型</h3>
    <div class="form-group">
      <label>任务类型</label>
      <input type="text" id="modalTaskType" disabled>
    </div>
    <div class="form-group">
      <label>提供商</label>
      <input type="text" id="modalProvider" placeholder="ollama">
    </div>
    <div class="form-group">
      <label>模型名称</label>
      <input type="text" id="modalModel" placeholder="qwen3:8b-chat">
    </div>
    <div class="form-group">
      <label>API 地址</label>
      <input type="text" id="modalBaseUrl" placeholder="http://192.168.100.4:11434/v1">
    </div>
    <div class="form-group">
      <label>API Key</label>
      <input type="password" id="modalApiKey" placeholder="***">
    </div>
    <div class="modal-actions">
      <button class="btn" onclick="closeModal()">取消</button>
      <button class="btn btn-primary" onclick="saveTaskModel()">💾 保存</button>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
const TASK_ICONS = { code:'💻', chat:'💬', vision:'👁️', reasoning:'🧠', translation:'🌐', audio:'🎵', video:'🎬' };
const TASK_LABELS = { code:'代码生成', chat:'普通对话', vision:'图像理解', reasoning:'复杂推理', translation:'翻译任务', audio:'音频处理', video:'视频分析' };
const TASK_COLORS = { code:'blue', chat:'green', vision:'orange', reasoning:'purple', audio:'cyan', video:'peach', translation:'red' };
let currentConfig = null;

async function fetchConfig() {
  const r = await fetch('/api/config');
  currentConfig = await r.json();
  renderAll();
}

function renderAll() {
  document.getElementById('main-provider').value = currentConfig.router_model.provider;
  document.getElementById('main-model').value = currentConfig.router_model.model;
  document.getElementById('main-baseurl').value = currentConfig.router_model.base_url;
  document.getElementById('main-apikey').value = currentConfig.router_model.api_key === '***' ? '' : currentConfig.router_model.api_key;
  renderTaskList();
}

function renderTaskList() {
  const container = document.getElementById('taskList');
  container.innerHTML = Object.entries(currentConfig.routing_rules).map(([key, cfg]) => `
    <div class="card task-card" style="margin:0;">
      <div class="task-info">
        <div class="task-icon ${TASK_COLORS[key]}">${TASK_ICONS[key]}</div>
        <div>
          <strong>${TASK_LABELS[key]}</strong>
          <div class="task-model">${cfg.model}</div>
          <div class="task-provider">${cfg.provider} <span class="arrow">→</span> ${cfg.base_url}</div>
        </div>
      </div>
      <button class="btn btn-sm" onclick="editTask('${key}')">✏️ 编辑</button>
    </div>
  `).join('');
}

function editTask(taskType) {
  const cfg = currentConfig.routing_rules[taskType];
  document.getElementById('modalTaskType').value = taskType;
  document.getElementById('modalTitle').textContent = `编辑路由模型 - ${TASK_LABELS[taskType]}`;
  document.getElementById('modalProvider').value = cfg.provider;
  document.getElementById('modalModel').value = cfg.model;
  document.getElementById('modalBaseUrl').value = cfg.base_url;
  document.getElementById('modalApiKey').value = cfg.api_key === '***' ? '' : cfg.api_key;
  document.getElementById('editModal').classList.add('active');
  document.getElementById('editModal').dataset.taskType = taskType;
}

function closeModal() {
  document.getElementById('editModal').classList.remove('active');
}

async function saveMainModel() {
  const body = {
    provider: document.getElementById('main-provider').value,
    model: document.getElementById('main-model').value,
    base_url: document.getElementById('main-baseurl').value,
    api_key: document.getElementById('main-apikey').value || '***',
  };
  try {
    const r = await fetch('/api/config/main-model', {
      method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)
    });
    if (r.ok) { showToast('主模型已保存 ✅', 'success'); await fetchConfig(); }
    else { showToast('保存失败 ❌', 'error'); }
  } catch(e) { showToast('请求失败: ' + e.message, 'error'); }
}

async function saveTaskModel() {
  const taskType = document.getElementById('editModal').dataset.taskType;
  const body = {
    provider: document.getElementById('modalProvider').value,
    model: document.getElementById('modalModel').value,
    base_url: document.getElementById('modalBaseUrl').value,
    api_key: document.getElementById('modalApiKey').value || '***',
  };
  try {
    const r = await fetch(`/api/config/task/${taskType}`, {
      method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)
    });
    if (r.ok) { showToast(`${TASK_LABELS[taskType]} 已更新 ✅`, 'success'); closeModal(); await fetchConfig(); }
    else { showToast('保存失败 ❌', 'error'); }
  } catch(e) { showToast('请求失败: ' + e.message, 'error'); }
}

async function resetConfig() {
  if (!confirm('确定要重置所有配置为默认值吗？此操作不可撤销。')) return;
  try {
    const r = await fetch('/api/config/reset', { method: 'POST' });
    if (r.ok) { showToast('已重置为默认配置 ✅', 'success'); await fetchConfig(); }
    else { showToast('重置失败 ❌', 'error'); }
  } catch(e) { showToast('请求失败: ' + e.message, 'error'); }
}

function showToast(msg, type) {
  const t = document.getElementById('toast');
  t.textContent = msg; t.className = `toast ${type}`; t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 2500);
}

async function checkHealth() {
  try {
    const r = await fetch('/health');
    if (r.ok) {
      document.getElementById('statusDot').className = 'status-dot online';
      document.getElementById('statusText').textContent = '运行中';
    }
  } catch(e) {
    document.getElementById('statusDot').className = 'status-dot offline';
    document.getElementById('statusText').textContent = '离线';
  }
}

document.getElementById('editModal').addEventListener('click', function(e) {
  if (e.target === this) closeModal();
});

fetchConfig();
setInterval(checkHealth, 30000);
</script>
</body>
</html>"""

@app.get("/admin", response_class=HTMLResponse)
async def admin_panel():
    return ADMIN_HTML

# ============================================================================
# 启动入口
# ============================================================================
if __name__ == "__main__":
    import uvicorn
    print("🚀 启动智能模型路由服务 v2.0...")
    print("📍 API 接口:   http://0.0.0.0:8000")
    print("⚙️  配置面板:  http://localhost:8000/admin")
    print("📖 API 文档:   http://localhost:8000/docs")
    print("💓 健康检查:   http://localhost:8000/health")
    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="info")
