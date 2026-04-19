# HyperPredict WebUI 快速开始指南

## 🎉 新 WebUI 已完成！

基于 **React + Material Design** 的现代化 WebUI 已经创建完成，包含高级图表和配置管理功能。

## ✨ 主要特性

### 1. Material Design 设计
- 采用 Material Design 3 设计语言
- 深色主题，护眼舒适
- 响应式布局，支持移动端

### 2. 实时监控面板
- FPS、温度、电池、CPU 负载实时显示
- **高级图表**：FPS 趋势图、温度趋势图、CPU 负载趋势图
- 集群信息可视化（LITTLE、MID、BIG）

### 3. 预测模型面板
- 神经网络/线性回归模型切换
- 模型性能指标（MAE、准确率）
- 预测结果对比图表
- 预测准确性分析

### 4. 调度策略面板
- 调度模式切换（均衡/游戏/性能）
- uclamp 参数调整
- 温控预设（激进/均衡/静音）
- 实时状态显示

### 5. 配置管理面板（新增）
- 配置预设管理
- 预设保存/加载
- 配置导入/导出
- LocalStorage 持久化

## 🚀 快速开始

### 1. 安装依赖

```bash
cd /storage/emulated/0/HyperPredict/webui-react
./install.sh
```

或手动安装：

```bash
npm install
```

### 2. 启动开发服务器

```bash
npm run dev
```

访问 http://localhost:3000

### 3. 构建生产版本

```bash
./build.sh
```

或手动构建：

```bash
npm run build
```

构建产物将输出到 `../webroot/` 目录。

## 📁 项目结构

```
webui-react/
├── src/
│   ├── api/
│   │   └── index.ts              # API 调用封装
│   ├── components/
│   │   ├── Dashboard.tsx         # 监控面板组件
│   │   ├── Predictor.tsx         # 预测模型组件
│   │   ├── Scheduler.tsx         # 调度策略组件
│   │   ├── Config.tsx            # 配置管理组件
│   │   └── Layout.tsx            # 布局组件
│   ├── hooks/
│   │   └── useWebSocket.ts       # WebSocket Hook
│   ├── types/
│   │   └── index.ts              # TypeScript 类型定义
│   ├── App.tsx                   # 主应用组件
│   └── main.tsx                  # 入口文件
├── index.html                    # HTML 模板
├── package.json                  # 项目配置
├── tsconfig.json                 # TypeScript 配置
├── vite.config.ts                # Vite 配置
├── install.sh                    # 安装脚本
├── build.sh                      # 构建脚本
└── README.md                     # 项目文档
```

## 🔧 技术栈

- **React 18** - 现代化 UI 框架
- **Material UI v5** - Material Design 组件库
- **Recharts** - 高级图表库
- **Vite** - 快速构建工具
- **TypeScript** - 类型安全

## 📊 功能对比

### 旧 WebUI vs 新 WebUI

| 功能 | 旧 WebUI | 新 WebUI |
|------|---------|---------|
| 设计风格 | iOS 风格 | Material Design |
| 技术栈 | 纯原生 | React + TypeScript |
| 图表库 | Canvas | Recharts |
| 配置管理 | ❌ | ✅ |
| 高级图表 | ❌ | ✅ |
| 响应式布局 | ❌ | ✅ |
| 类型安全 | ❌ | ✅ |

## 🎯 使用说明

### 监控面板
- 查看实时 FPS、温度、电池、CPU 负载
- 查看历史趋势图表
- 查看集群信息

### 预测面板
- 切换预测模型（神经网络/线性回归）
- 查看模型性能指标
- 查看预测结果对比

### 调度面板
- 切换调度模式（均衡/游戏/性能）
- 调整 uclamp 参数
- 设置温控预设

### 配置面板
- 添加/编辑/删除配置预设
- 导出配置到本地文件
- 从本地文件导入配置

## 🔌 API 接口

### HTTP API
- `GET /api/status` - 获取系统状态
- `GET /api/model` - 获取模型权重
- `POST /api/command` - 发送命令

### WebSocket
- `ws://localhost:8081/ws` - WebSocket 连接

## 📝 注意事项

1. 确保 HyperPredict 守护进程正在运行
2. 确保 WebSocket 端口 8081 可访问
3. 构建前请先安装依赖
4. 构建产物将覆盖 `../webroot/` 目录

## 🐛 故障排除

### 依赖安装失败
```bash
# 清除缓存重新安装
rm -rf node_modules package-lock.json
npm install
```

### 构建失败
```bash
# 检查 TypeScript 配置
npm run build
```

### WebSocket 连接失败
- 检查守护进程是否运行
- 检查端口 8081 是否开放
- 检查防火墙设置

## 📚 更多文档

- [README.md](./README.md) - 项目文档
- [PROJECT_STRUCTURE.md](./PROJECT_STRUCTURE.md) - 项目结构说明

## 🎉 开始使用

现在你可以开始使用新的 WebUI 了！

```bash
cd /storage/emulated/0/HyperPredict/webui-react
./install.sh
npm run dev
```

访问 http://localhost:3000 查看新界面！
