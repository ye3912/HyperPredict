# HyperPredict ksuwebui

KernelSU WebUI for HyperPredict kernel scheduler daemon.

## Features

### 🤖 Neural Network Predictor
- **MLP Neural Network**: 8 inputs → 16 → 8 → 1
- **Leaky ReLU** activation with Xavier weight initialization
- **Backpropagation** for real-time learning
- **Dual Model**: Switch between Neural Network and Linear Regression
- **Accuracy Tracking**: Real-time model performance metrics

### 📊 Process Communication
- **WebSocket** connection to daemon (primary)
- **HTTP Polling** fallback (2s interval)
- **Auto Reconnect** with exponential backoff
- **Bidirectional Sync**: UI ↔ Daemon model weights

### 🎨 Material Design 3 UI
- **120Hz** smooth animations
- **Google Vibrant** color scheme
- **Dark theme** optimized

## Usage

```bash
# Serve locally
cd ksuwebui
python -m http.server 8080

# Or use any static file server
npx serve .
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    WebUI (Browser)                   │
├─────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │
│  │  NeuralNet  │  │   Linear    │  │   Daemon    │  │
│  │  Predictor  │  │  Predictor  │  │  Connector  │  │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  │
│         │                │                │          │
│         └────────┬───────┘                │          │
│                  ▼                        ▼          │
│         ┌─────────────────┐      ┌─────────────────┐  │
│         │  Model Manager  │      │ WebSocket/HTTP  │  │
│         │  (Switch/Sync)  │      │    Connector    │  │
│         └─────────────────┘      └────────┬────────┘  │
│                                           │           │
└───────────────────────────────────────────│───────────┘
                                            │
                                            ▼
                              ┌─────────────────────────┐
                              │   HyperPredict Daemon   │
                              │   (Linux Kernel Mod)   │
                              └─────────────────────────┘
```

## API Endpoints (Daemon)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Get current system status |
| `/api/model` | GET | Get model weights |
| `/api/model` | POST | Set model weights |
| `/api/command` | POST | Send command |
| `/ws` | WS | WebSocket real-time stream |

## Model Formats

### Linear Regression
```json
{
  "weights": {
    "w_util": 0.5,
    "w_rq": -0.2,
    "w_wakeups": 0.1,
    "w_frame": 0.3,
    "w_touch": 0.05,
    "w_thermal": 0.1,
    "w_battery": 0.02,
    "bias": 55.0
  },
  "ema_error": 2.5,
  "learning_rate": 0.05
}
```

### Neural Network
```json
{
  "weights": [
    [[0.1, -0.2, ...], [0.3, 0.1, ...]],
    [[0.2, -0.1, ...], [0.1, 0.2, ...]]
  ],
  "biases": [[0.1, -0.2], [0.05]],
  "config": {
    "inputSize": 8,
    "hiddenSizes": [16, 8],
    "outputSize": 1,
    "learningRate": 0.005
  }
}
```

## File Structure

```
ksuwebui/
├── index.html      # UI structure
├── styles.css      # M3 styling + 120Hz animations
├── app.js          # Core logic + ML models + daemon connector
└── README.md       # This file
```

## ML Model Comparison

| Metric | Linear | Neural |
|--------|--------|--------|
| Training Speed | ⚡ Instant | 🐢 ~50ms |
| Accuracy | ~75% | ~90% |
| Memory | <1KB | ~10KB |
| Cold Start | ✅ | ⚠️ Needs warmup |

## Installation

1. Copy files to KernelSU module webui directory
2. Ensure HyperPredict daemon is running
3. Access via KernelSU manager → WebUI

## License

MIT
