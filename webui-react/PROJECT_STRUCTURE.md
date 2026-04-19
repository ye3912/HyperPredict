# HyperPredict WebUI 项目结构

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
│   ├── utils/
│   │   └── (待添加)              # 工具函数
│   ├── App.tsx                   # 主应用组件
│   └── main.tsx                  # 入口文件
├── index.html                    # HTML 模板
├── package.json                  # 项目配置
├── tsconfig.json                 # TypeScript 配置
├── tsconfig.node.json            # Node TypeScript 配置
├── vite.config.ts                # Vite 配置
├── .gitignore                    # Git 忽略文件
├── README.md                     # 项目文档
├── PROJECT_STRUCTURE.md         # 项目结构说明
├── install.sh                    # 安装脚本
└── build.sh                      # 构建脚本
```

## 组件说明

### Dashboard
实时监控面板，显示：
- 核心指标（FPS、温度、电池、CPU 负载）
- FPS 趋势图
- 温度趋势图
- CPU 负载趋势图
- 集群信息（LITTLE、MID、BIG）

### Predictor
预测模型面板，显示：
- 模型选择（神经网络/线性回归）
- 模型性能指标（MAE、准确率）
- 预测结果对比图
- 预测准确性分析

### Scheduler
调度策略面板，显示：
- 调度模式切换（均衡/游戏/性能）
- uclamp 参数调整
- 温控预设（激进/均衡/静音）
- 当前调度状态

### Config
配置管理面板，显示：
- 配置预设列表
- 添加/编辑/删除预设
- 配置导入/导出
- 系统信息

## API 接口

### HTTP API
- `GET /api/status` - 获取系统状态
- `GET /api/model` - 获取模型权重
- `POST /api/command` - 发送命令

### WebSocket
- `ws://localhost:8081/ws` - WebSocket 连接

## 开发流程

1. 安装依赖：`./install.sh`
2. 启动开发服务器：`npm run dev`
3. 访问 http://localhost:3000
4. 构建生产版本：`./build.sh`

## 构建产物

构建后的文件将输出到 `../webroot/` 目录，可以直接部署到 Web 服务器。
