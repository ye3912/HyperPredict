#!/bin/bash

echo "安装 HyperPredict WebUI 依赖..."

cd "$(dirname "$0")"

# 检查 Node.js
if ! command -v node &> /dev/null; then
    echo "错误: 未找到 Node.js"
    echo "请先安装 Node.js: https://nodejs.org/"
    exit 1
fi

# 检查 npm
if ! command -v npm &> /dev/null; then
    echo "错误: 未找到 npm"
    exit 1
fi

echo "Node.js 版本: $(node --version)"
echo "npm 版本: $(npm --version)"

# 安装依赖
npm install

if [ $? -eq 0 ]; then
    echo "✓ 依赖安装成功"
    echo ""
    echo "使用方法:"
    echo "  开发模式: npm run dev"
    echo "  构建生产版本: npm run build"
else
    echo "✗ 依赖安装失败"
    exit 1
fi
