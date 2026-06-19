#!/usr/bin/env python3
"""
测试脚本：验证模型路由功能
"""

import asyncio
import json
from server import ModelRouter, Message, TaskType


async def test_router():
    """测试路由器的各种场景"""
    router = ModelRouter()
    
    test_cases = [
        {
            "name": "代码生成任务",
            "messages": [
                Message(role="user", content="写一个 Python 快速排序函数")
            ],
            "expected_type": TaskType.CODE,
        },
        {
            "name": "普通对话",
            "messages": [
                Message(role="user", content="今天天气怎么样？")
            ],
            "expected_type": TaskType.CHAT,
        },
        {
            "name": "复杂推理",
            "messages": [
                Message(role="user", content="如果一个房间里有 10 个人，每个人都和其他人握手一次，总共握手多少次？")
            ],
            "expected_type": TaskType.REASONING,
        },
        {
            "name": "翻译任务",
            "messages": [
                Message(role="user", content="把这句话翻译成英文：你好世界")
            ],
            "expected_type": TaskType.TRANSLATION,
        },
        {
            "name": "图像理解（多模态）",
            "messages": [
                Message(role="user", content=[
                    {"type": "text", "text": "这张图片里有什么？"},
                    {"type": "image_url", "image_url": {"url": "https://example.com/image.jpg"}}
                ])
            ],
            "expected_type": TaskType.VISION,
        },
    ]
    
    print("🧪 开始测试模型路由器...\n")
    
    for i, test_case in enumerate(test_cases, 1):
        print(f"测试 {i}: {test_case['name']}")
        print("=" * 50)
        
        try:
            # 测试意图分类
            task_type = await router.classify_intent(test_case["messages"])
            
            is_correct = task_type == test_case["expected_type"]
            status = "✅ 通过" if is_correct else "❌ 失败"
            
            print(f"预期类型: {test_case['expected_type'].value}")
            print(f"识别类型: {task_type.value}")
            print(f"结果: {status}\n")
            
        except Exception as e:
            print(f"❌ 错误: {e}\n")
    
    await router.client.aclose()


async def test_full_request():
    """测试完整的路由和调用流程"""
    router = ModelRouter()
    
    print("\n🚀 测试完整请求流程...\n")
    
    # 测试一个简单的代码任务
    messages = [
        Message(role="user", content="写一个 Python 函数，计算斐波那契数列的第 n 项")
    ]
    
    try:
        print("📤 发送请求...")
        response, model = await router.route_and_call(
            messages=messages,
            temperature=0.7,
            stream=False,
        )
        
        print(f"✅ 使用模型: {model}")
        print(f"📥 响应内容:\n{response[:200]}...\n")
        
    except Exception as e:
        print(f"❌ 请求失败: {e}\n")
    
    await router.client.aclose()


if __name__ == "__main__":
    print("智能模型路由器测试\n")
    
    # 测试意图分类
    asyncio.run(test_router())
    
    # 测试完整请求（需要配置真实的 API keys）
    # asyncio.run(test_full_request())
