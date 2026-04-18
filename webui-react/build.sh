#!/bin/bash

echo "构建 HyperPredict WebUI..."

cd "$(dirname "$0")"

# 检查依赖
if [ ! -d "node_modules" ]; then
    echo "错误: 未找到依赖，请先运行 ./install.sh"
    exit 1
fi

# 构建生产版本
npm run build

if [ $? -eq 0 ]; then
    echo "✓ 构建成功"
    echo ""
    echo "构建产物已输出到: ../webroot/"
    echo ""
    echo "请将 webroot/ 目录部署到 Web 服务器"
else
    echo "✗ 构建失败"
    exit 1
fi
