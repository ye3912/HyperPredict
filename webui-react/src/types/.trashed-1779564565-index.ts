// 系统状态
export interface SystemStatus {
  timestamp: number;
  fps: number;
  target_fps: number;
  cpu_util: number;
  run_queue_len: number;
  wakeups_100ms: number;
  frame_interval_us: number;
  touch_rate_100ms: number;
  thermal_margin: number;
  temperature: number;
  battery_level: number;
  is_gaming: boolean;
  mode: 'daily' | 'game' | 'turbo';
  uclamp_min: number;
  uclamp_max: number;
  clusters: {
    little: { freq: number; usage: number };
    mid: { freq: number; usage: number };
    big: { freq: number; usage: number };
  };
}

// 模型权重
export interface ModelWeights {
  // 线性模型
  w_util: number;
  w_rq: number;
  w_wakeups: number;
  w_frame: number;
  w_touch: number;
  w_thermal: number;
  w_battery: number;
  bias: number;
  ema_error: number;

  // 神经网络
  has_nn: boolean;
  nn_weights?: number[][][];
  nn_biases?: number[][];
}

// 预测结果
export interface PredictionResult {
  actual: number;
  predicted: number;
  linear: number;
  neural: number;
  error: number;
  timestamp: number;
}

// 模型统计
export interface ModelStats {
  linear: {
    accuracy: number;
    mae: number;
  };
  neural: {
    accuracy: number;
    mae: number;
  };
  current: 'linear' | 'neural';
}

// 配置预设
export interface ConfigPreset {
  id: string;
  name: string;
  description: string;
  mode: 'daily' | 'game' | 'turbo';
  uclamp_min: number;
  uclamp_max: number;
  thermal_preset: 'aggressive' | 'balanced' | 'quiet';
  created_at: string;
}

// 日志条目
export interface LogEntry {
  timestamp: number;
  level: 'info' | 'warning' | 'error' | 'success';
  message: string;
}

// WebSocket 消息
export interface WSMessage {
  type: number;
  data?: any;
}
