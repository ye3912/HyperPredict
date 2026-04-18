# HyperPredict WebUI

基于 React + Material Design 的现代化 WebUI，用于监控和管理 HyperPredict 系统。

## 功能特性

### 🎨 Material Design
- 采用 Material Design 3 设计语言
- 深色主题，护眼舒适
- 响应式布局，支持移动端

### 📊 实时监控
- FPS、温度、电池、CPU 负载实时显示
- 高级图表展示（FPS 趋势、温度趋势、CPU 负载趋势）
- 集群信息可视化（LITTLE、MID、BIG）

### 🧠 预测模型
- 神经网络/线性回归模型切换
- 模型性能指标（MAE、准确率）
- 预测结果对比图表
- 预测准确性分析

### ⚡ 调度策略
- 调度模式切换（均衡/游戏/性能）
- uclamp 参数调整
- 温控预设（激进/均衡/静音）
- 实时状态显示

### ⚙️ 配置管理
- 配置预设管理
- 预设保存/加载
- 配置导入/导出
- LocalStorage 持久化

## 技术栈

- **React 18** - 现代化 UI 框架
- **Material UI v5** - Material Design 组件库
- **Recharts** - 高级图表库
- **Vite** - 快速构建工具
- **TypeScript** - 类型安全

## 开发

### 安装依赖

```bash
npm install
```

### 启动开发服务器

```bash
npm run dev
```

访问 http://localhost:3000

### 构建生产版本

```bash
npm run build
```

构建产物将输出到 `../webroot` 目录。

## API 接口

### HTTP API

- `GET /api/status` - 获取系统状态
- `GET /api/model` - 获取模型权重
- `POST /api/command` - 发送命令

### WebSocket

- `ws://localhost:8081/ws` - WebSocket 连接

## 配置

### Vite 配置

```typescript
// vite.config.ts
export default defineConfig({
  server: {
    port: 3000,
    proxy: {
      '/api': {
        target: 'http://localhost:8081',
        changeOrigin: true
      },
      '/ws': {
        target: 'ws://localhost:8081',
        ws: true
      }
    }
  },
  build: {
    outDir: '../webroot',
    emptyOutDir: true
  }
})
```

## 浏览器支持

- Chrome (推荐)
- Firefox
- Safari
- Edge

## 许可证

MIT License
