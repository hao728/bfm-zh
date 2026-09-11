#!/bin/bash
# 一键删除 GitHub Actions 所有历史运行记录
# 用法: GITHUB_TOKEN=ghp_xxx ./cleanup-actions.sh hao728/bfm-zh
# 需要 Personal Access Token (repo 权限)

REPO="${1:-hao728/bfm-zh}"
TOKEN="${GITHUB_TOKEN:?请设置 GITHUB_TOKEN 环境变量}"

echo "清理仓库: $REPO"
echo "正在获取所有 workflow runs..."

PAGE=1
DELETED=0
while :; do
  RESP=$(curl -s -H "Authorization: token $TOKEN" \
    "https://api.github.com/repos/$REPO/actions/runs?per_page=100&page=$PAGE")
  IDS=$(echo "$RESP" | python3 -c "
import sys,json
try:
    data=json.load(sys.stdin)
    for r in data.get('workflow_runs',[]):
        print(r['id'])
except: pass
")
  if [ -z "$IDS" ]; then
    echo "第 $PAGE 页无数据，完成。"
    break
  fi
  COUNT=$(echo "$IDS" | wc -l)
  echo "第 $PAGE 页: $COUNT 条，正在删除..."
  for ID in $IDS; do
    curl -s -X DELETE -H "Authorization: token $TOKEN" \
      "https://api.github.com/repos/$REPO/actions/runs/$ID"
    DELETED=$((DELETED+1))
  done
  PAGE=$((PAGE+1))
  if [ $PAGE -gt 50 ]; then
    echo "超过50页，停止。"
    break
  fi
done

echo "共删除 $DELETED 条 workflow runs。"
echo ""
echo "提示: 删除 .github/workflows/ 下不需要的 yml 文件可阻止新运行产生。"
