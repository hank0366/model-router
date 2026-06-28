#!/bin/bash
# model_router 日志分析脚本
# 每晚执行，分析当天日志，输出优化建议

LOG_DIR="/home/lvzheyu/apps/model-router-c/logs"
SUMMARY_DIR="/home/lvzheyu/apps/model-router-c/logs"
DATE=$(date +%Y-%m-%d)
LOG_FILE="$LOG_DIR/router_debug_${DATE}.jsonl"
SUMMARY_FILE="$SUMMARY_DIR/summary_${DATE}.md"

if [ ! -f "$LOG_FILE" ]; then
    echo "[$DATE] 今天没有日志文件"
    exit 0
fi

TOTAL=$(wc -l < "$LOG_FILE")
if [ "$TOTAL" -eq 0 ]; then
    echo "[$DATE] 日志文件为空"
    exit 0
fi

echo "=== Model Router 日报 $DATE ==="
echo ""

# 统计各任务类型分布
echo "**任务类型分布：**"
grep -o '"task_type":[0-9]*' "$LOG_FILE" | sort | uniq -c | sort -rn | while read count type; do
    case "$type" in
        *"0"*) echo "- chat: $count";;
        *"1"*) echo "- code: $count";;
        *"2"*) echo "- vision: $count";;
        *"3"*) echo "- reasoning: $count";;
        *"4"*) echo "- translation: $count";;
        *"5"*) echo "- audio: $count";;
        *"6"*) echo "- video: $count";;
    esac
done
echo ""

# 统计 fallback 比例
FALLBACK=$(grep '"fallback":1' "$LOG_FILE" | wc -l)
FALLBACK_PCT=$(echo "scale=1; $FALLBACK * 100 / $TOTAL" | bc)
echo "**Fallback 比例:** $FALLBACK/$TOTAL ($FALLBACK_PCT%)"
echo ""

# 统计关键词命中情况
echo "**关键词命中 TOP 10：**"
grep -o '"keyword":"[^"]*"' "$LOG_FILE" | sed 's/"keyword":"//;s/"$//' | sort | uniq -c | sort -rn | head -10
echo ""

# 统计使用的模型
echo "**模型使用分布：**"
grep -o '"model":"[^"]*"' "$LOG_FILE" | sed 's/"model":"//;s/"$//' | sort | uniq -c | sort -rn
echo ""

# 分析未匹配请求的常见模式
echo "**未匹配请求分析（前 20 条）：**"
grep '"fallback":1' "$LOG_FILE" | head -20 | grep -o '"preview":"[^"]*"' | sed 's/"preview":"//;s/"$//' | head -5
echo ""

# 生成优化建议
echo "**优化建议：**"
if [ $(echo "$FALLBACK_PCT > 20" | bc) -eq 1 ]; then
    echo "- ⚠️ Fallback 比例较高（>$FALLBACK_PCT%），建议增加更多关键词规则"
fi

# 检查是否有高频未匹配模式
UNMATCHED_PATTERNS=$(grep '"fallback":1' "$LOG_FILE" | grep -o '"preview":"[^"]*"' | sed 's/"preview":"//;s/"$//' | awk '{print tolower($0)}' | head -10)
if [ -n "$UNMATCHED_PATTERNS" ]; then
    echo "- 🔍 未匹配请求示例："
    echo "$UNMATCHED_PATTERNS" | while read line; do
        echo "  - $line"
    done
fi

# 清理旧日志（保留 7 天）
echo ""
echo "**清理旧日志：**"
cd "$LOG_DIR"
DELETED=$(find . -name "router_debug_*.jsonl" -mtime +7 -exec rm -v {} \; 2>&1 | wc -l)
echo "- 已清理 $DELETED 个超过 7 天的日志文件"

# 保存摘要
cat > "$SUMMARY_FILE" << EOF
# Model Router 日报 - $DATE

## 统计数据
- 总请求数: $TOTAL
- Fallback 数: $FALLBACK ($FALLBACK_PCT%)

## 任务分布
$(grep -o '"task_type":[0-9]*' "$LOG_FILE" | sort | uniq -c | sort -rn)

## 关键词命中 TOP 10
$(grep -o '"keyword":"[^"]*"' "$LOG_FILE" | sed 's/"keyword":"//;s/"$//' | sort | uniq -c | sort -rn | head -10)

## 模型使用分布
$(grep -o '"model":"[^"]*"' "$LOG_FILE" | sed 's/"model":"//;s/"$//' | sort | uniq -c | sort -rn)

## 未匹配请求示例
$(grep '"fallback":1' "$LOG_FILE" | grep -o '"preview":"[^"]*"' | sed 's/"preview":"//;s/"$//' | head -5)

## 优化建议
$(if [ $(echo "$FALLBACK_PCT > 20" | bc) -eq 1 ]; then echo "- ⚠️ Fallback 比例较高，建议增加关键词规则"; fi)
EOF

echo "✅ 摘要已保存到: $SUMMARY_FILE"
