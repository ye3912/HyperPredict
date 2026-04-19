#!/bin/bash
# 版本号自动更新脚本
# 用于 CI/CD 自动递增版本号

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE_PROP="${PROJECT_ROOT}/module.prop"
VERSION_FILE="${PROJECT_ROOT}/.version"

# 检查 module.prop 是否存在
if [ ! -f "$MODULE_PROP" ]; then
    echo -e "${RED}错误: module.prop 文件不存在${NC}"
    exit 1
fi

# 读取当前版本号
CURRENT_VERSION=$(grep "^version=" "$MODULE_PROP" | cut -d'=' -f2)
CURRENT_VERSION_CODE=$(grep "^versionCode=" "$MODULE_PROP" | cut -d'=' -f2)

if [ -z "$CURRENT_VERSION" ]; then
    echo -e "${RED}错误: 无法读取当前版本号${NC}"
    exit 1
fi

# 解析版本号 (格式: v4.2.0)
VERSION_MAJOR=$(echo "$CURRENT_VERSION" | sed 's/v//' | cut -d'.' -f1)
VERSION_MINOR=$(echo "$CURRENT_VERSION" | sed 's/v//' | cut -d'.' -f2)
VERSION_PATCH=$(echo "$CURRENT_VERSION" | sed 's/v//' | cut -d'.' -f3)

# 递增版本号
NEW_VERSION_PATCH=$((VERSION_PATCH + 1))
NEW_VERSION="v${VERSION_MAJOR}.${VERSION_MINOR}.${NEW_VERSION_PATCH}"
NEW_VERSION_CODE=$((CURRENT_VERSION_CODE + 1))

# 更新 module.prop
sed -i "s/^version=.*/version=${NEW_VERSION}/" "$MODULE_PROP"
sed -i "s/^versionCode=.*/versionCode=${NEW_VERSION_CODE}/" "$MODULE_PROP"

# 保存版本号到文件
echo "${NEW_VERSION}" > "$VERSION_FILE"
echo "${NEW_VERSION_CODE}" >> "$VERSION_FILE"

# 获取 Git 信息
GIT_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

# 输出结果
echo -e "${GREEN}版本号已更新${NC}"
echo "旧版本: ${CURRENT_VERSION} (code: ${CURRENT_VERSION_CODE})"
echo "新版本: ${NEW_VERSION} (code: ${NEW_VERSION_CODE})"
echo "Git 提交: ${GIT_COMMIT}"
echo "Git 分支: ${GIT_BRANCH}"
echo "构建时间: ${BUILD_DATE}"

# 导出环境变量供 CI/CD 使用
export HP_VERSION="${NEW_VERSION}"
export HP_VERSION_CODE="${NEW_VERSION_CODE}"
export HP_GIT_COMMIT="${GIT_COMMIT}"
export HP_GIT_BRANCH="${GIT_BRANCH}"
export HP_BUILD_DATE="${BUILD_DATE}"

# 如果在 CI 环境中，设置 GitHub Actions 输出
if [ -n "$GITHUB_OUTPUT" ]; then
    echo "version=${NEW_VERSION}" >> "$GITHUB_OUTPUT"
    echo "versionCode=${NEW_VERSION_CODE}" >> "$GITHUB_OUTPUT"
    echo "gitCommit=${GIT_COMMIT}" >> "$GITHUB_OUTPUT"
    echo "gitBranch=${GIT_BRANCH}" >> "$GITHUB_OUTPUT"
    echo "buildDate=${BUILD_DATE}" >> "$GITHUB_OUTPUT"
fi

exit 0
