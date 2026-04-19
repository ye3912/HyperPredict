# HyperPredict KSU WebUI

HyperPredict 的 KSU (KernelSU) WebUI 支持，允许通过 KernelSU 的 Web 界面配置和管理 HyperPredict。

## 功能特性

- 🎨 **深色主题** - 符合 KSU 风格的深色卡片式设计
- 📊 **实时监控** - FPS、温度、CPU 负载等实时数据展示
- 🧠 **预测模型** - 神经网络和线性回归模型对比
- ⚡ **调度策略** - 调度模式、uclamp、温控预设配置
- 📱 **响应式设计** - 支持移动端和桌面端
- 🔌 **实时通信** - WebSocket + HTTP REST API

## 安装

### 作为 KSU 模块安装

1. 将 HyperPredict 模块安装到 KernelSU
2. 在 KernelSU WebUI 中打开 HyperPredict
3. 开始配置和使用

### 手动安装

```bash
# 复制 webroot 目录到模块目录
cp -r webroot /data/adb/modules/hyperpredict/

# 重启 KernelSU WebUI
```

## 使用

### 实时监控

- 查看 FPS、温度、电池等实时数据
- 监控 CPU 负载和频率
- 查看系统信息

### 预测模型

- 切换神经网络/线性回归/混合模型
- 查看预测准确率和 MAE
- 查看当前场景识别

### 调度策略

- 切换均衡/游戏/性能模式
- 调整 uclamp.min 和 uclamp.max
- 配置温控预设（激进/均衡/静音）

## API 接口

### HTTP REST API

#### 获取状态

```http
GET /api/status
```

响应：
```json
{
  "timestamp": 1234567890,
  "fps": 60,
  "cpu_util": 512,
  "temperature": 42,
  "battery_level": 85,
  "mode": "game",
  "uclamp_min": 50,
  "uclamp_max": 100
}
```

#### 获取模型权重

```http
GET /api/model
```

响应：
```json
{
  "linear": {
    "w_util": 0.3,
    "w_rq": -0.1,
    "bias": 55.0
  },
  "has_nn": true,
  "nn_weights": [...],
  "nn_biases": [...]
}
```

#### 发送命令

```http
POST /api/command
Content-Type: application/json

{
  "cmd": "set_mode",
  "mode": "game"
}
```

### WebSocket

连接：
```
ws://localhost:8081/
```

消息类型：
- `type: 1` - 状态更新
- `type: 2` - 模型权重
- `type: 3` - 命令确认
- `type: 4` - 错误

## 配置

### module.prop

```
id=hyperpredict
name=HyperPredict
version=v4.2.0
versionCode=420
author=ye3912
description=AI CPU Scheduler with KSU WebUI Support
webui=webroot
```

### 环境变量

- `HP_API_BASE` - API 基础 URL（默认：`http://localhost:8081/api`）
- `HP_WS_URL` - WebSocket URL（默认：`ws://localhost:8081/`）

## 开发

### 本地开发

```bash
# 启动 HTTP 服务器
python3 -m http.server 8080

# 访问
open http://localhost:8080
```

### 构建生产版本

```bash
# 压缩 JS 和 CSS
npm install -g terser clean-css-cli

terser app.js -c -m -o app.min.js
cleancss styles.css -o styles.min.css
```

## 故障排除

### 无法连接到守护进程

1. 检查 `hyperpredictd` 是否运行：
   ```bash
   ps aux | grep hyperpredictd
   ```

2. 检查端口是否被占用：
   ```bash
   netstat -tlnp | grep 8081
   ```

3. 查看日志：
   ```bash
   cat /data/adb/modules/hyperpredict/logs/hp.log
   ```

### WebUI 显示异常

1. 清除浏览器缓存
2. 检查浏览器控制台错误
3. 确认 JavaScript 和 CSS 文件已正确加载

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 Pull Request！
