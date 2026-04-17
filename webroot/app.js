/**
 * HyperPredict WebUI - 完整适配版
 * 与 C++ 后端所有接口完全对接
 */

'use strict';

// ============================================================
// 工具函数
// ============================================================
const Utils = {
    clamp: (v, min, max) => Math.max(min, Math.min(max, v)),
    
    normalize: {
        cpu_util: v => v / 1024,
        run_queue: v => v / 32,
        wakeups: v => v / 100,
        frame_interval: v => v / 20000,
        touch_rate: v => v / 20,
        thermal_margin: v => (v + 30) / 60,
        battery: v => v / 100,
        is_gaming: v => v ? 1 : 0
    },
    
    toFeatureVector: (status) => [
        Utils.normalize.cpu_util(status.cpu_util || 0),
        Utils.normalize.run_queue(status.run_queue_len || 1),
        Utils.normalize.wakeups(status.wakeups_100ms || 0),
        Utils.normalize.frame_interval(status.frame_interval_us || 16667),
        Utils.normalize.touch_rate(status.touch_rate_100ms || 0),
        Utils.normalize.thermal_margin(status.thermal_margin || 20),
        Utils.normalize.battery(status.battery_level || 100),
        Utils.normalize.is_gaming(status.is_gaming || false)
    ],
    
    calcFps: (frame_interval_us) => {
        if (!frame_interval_us || frame_interval_us === 0) return 60;
        return 1000000 / frame_interval_us;
    },
    
    calcFrameInterval: (fps) => {
        if (!fps || fps === 0) return 16667;
        return 1000000 / fps;
    }
};

// ============================================================
// 线性回归预测器
// ============================================================
class LinearPredictor {
    constructor() {
        this.weights = {
            w_util: 0.3,
            w_rq: -0.1,
            w_wakeups: 0.05,
            w_frame: 0.2,
            w_touch: 0.02,
            w_thermal: 0.1,
            w_battery: 0.01,
            bias: 55.0
        };
        this.ema_error = 2.5;
        this.last_util = 0;
        this.learning_rate = 0.05;
        this.history = [];
        this.maxHistory = 100;
    }
    
    train(features, actual_fps) {
        const normalized = Utils.toFeatureVector(features);
        const predicted = this.predict(features);
        const error = actual_fps - predicted;
        
        const lr = this.learning_rate;
        this.weights.w_util += lr * error * normalized[0];
        this.weights.w_rq += lr * error * normalized[1];
        this.weights.w_wakeups += lr * error * normalized[2];
        this.weights.w_frame += lr * error * normalized[3];
        this.weights.w_touch += lr * error * normalized[4];
        this.weights.w_thermal += lr * error * normalized[5];
        this.weights.w_battery += lr * error * normalized[6];
        this.weights.bias += lr * error;
        
        Object.keys(this.weights).forEach(k => {
            this.weights[k] = Utils.clamp(this.weights[k], k === 'bias' ? -50 : -2, k === 'bias' ? 50 : 2);
        });
        
        this.ema_error = this.ema_error * 0.9 + error * 0.1;
        this.last_util = normalized[0];
        
        this._addHistory({ actual: actual_fps, predicted, error });
        return predicted;
    }
    
    predict(features) {
        const normalized = Utils.toFeatureVector(features);
        let pred = this.weights.w_util * normalized[0] +
                   this.weights.w_rq * normalized[1] +
                   this.weights.w_wakeups * normalized[2] +
                   this.weights.w_frame * normalized[3] +
                   this.weights.w_touch * normalized[4] +
                   this.weights.w_thermal * normalized[5] +
                   this.weights.w_battery * normalized[6] +
                   this.weights.bias;
        
        const trend = (normalized[0] - this.last_util) * 10;
        pred += trend * 0.5;
        this.last_util = normalized[0];
        
        return Utils.clamp(pred, 0, 144);
    }
    
    _addHistory(entry) {
        entry.timestamp = Date.now();
        this.history.push(entry);
        if (this.history.length > this.maxHistory) this.history.shift();
    }
    
    export() {
        return {
            w_util: this.weights.w_util,
            w_rq: this.weights.w_rq,
            w_wakeups: this.weights.w_wakeups,
            w_frame: this.weights.w_frame,
            w_touch: this.weights.w_touch,
            w_thermal: this.weights.w_thermal,
            w_battery: this.weights.w_battery,
            bias: this.weights.bias,
            ema_error: this.ema_error
        };
    }
    
    import(data) {
        if (!data) return;
        Object.assign(this.weights, {
            w_util: data.w_util ?? 0.3,
            w_rq: data.w_rq ?? -0.1,
            w_wakeups: data.w_wakeups ?? 0.05,
            w_frame: data.w_frame ?? 0.2,
            w_touch: data.w_touch ?? 0.02,
            w_thermal: data.w_thermal ?? 0.1,
            w_battery: data.w_battery ?? 0.01,
            bias: data.bias ?? 55.0
        });
        this.ema_error = data.ema_error ?? 2.5;
    }
    
    reset() {
        this.weights = { w_util: 0.3, w_rq: -0.1, w_wakeups: 0.05, w_frame: 0.2, w_touch: 0.02, w_thermal: 0.1, w_battery: 0.01, bias: 55.0 };
        this.ema_error = 2.5;
        this.last_util = 0;
        this.history = [];
    }
    
    getAccuracy() {
        if (this.history.length < 10) return 0;
        const recent = this.history.slice(-20);
        return (recent.filter(h => Math.abs(h.actual - h.predicted) <= 5).length / recent.length) * 100;
    }
    
    getMAE() {
        if (this.history.length < 5) return 0;
        const recent = this.history.slice(-20);
        return recent.reduce((sum, h) => sum + Math.abs(h.error), 0) / recent.length;
    }
}

// ============================================================
// 轻量神经网络预测器 (8→4→1)
// ============================================================
class SimpleNeuralPredictor {
    constructor() {
        this.inputSize = 8;
        this.hiddenSize = 4;
        this.outputSize = 1;
        this.learningRate = 0.01;
        
        this.weights = [];
        this.biases = [];
        this.activations = [];
        this.history = [];
        this.maxHistory = 100;
        
        this._initWeights();
    }
    
    _initWeights() {
        const xavier1 = Math.sqrt(2.0 / (this.inputSize + this.hiddenSize));
        const xavier2 = Math.sqrt(2.0 / (this.hiddenSize + this.outputSize));
        
        this.weights[0] = [];
        for (let i = 0; i < this.hiddenSize * this.inputSize; i++) {
            this.weights[0][i] = (Math.random() - 0.5) * 2 * xavier1;
        }
        this.biases[0] = new Array(this.hiddenSize).fill(0);
        
        this.weights[1] = [];
        for (let i = 0; i < this.outputSize * this.hiddenSize; i++) {
            this.weights[1][i] = (Math.random() - 0.5) * 2 * xavier2;
        }
        this.biases[1] = [0];
    }
    
    _relu(x) { return Math.max(0, x); }
    _reluDerivative(x) { return x > 0 ? 1 : 0; }
    
    forward(input) {
        this.activations[0] = [];
        for (let h = 0; h < this.hiddenSize; h++) {
            let sum = this.biases[0][h];
            for (let i = 0; i < this.inputSize; i++) {
                sum += this.weights[0][h * this.inputSize + i] * input[i];
            }
            this.activations[0][h] = this._relu(sum);
        }
        
        let sum = this.biases[1][0];
        for (let h = 0; h < this.hiddenSize; h++) {
            sum += this.weights[1][h] * this.activations[0][h];
        }
        this.activations[1] = [sum];
        
        return this.activations[1][0];
    }
    
    backward(input, target, predicted) {
        const lr = this.learningRate;
        const outputError = predicted - target;
        const outputGrad = outputError;
        
        const hiddenGrad = [];
        for (let h = 0; h < this.hiddenSize; h++) {
            hiddenGrad[h] = this.weights[1][h] * outputGrad * this._reluDerivative(this.activations[0][h]);
        }
        
        for (let h = 0; h < this.hiddenSize; h++) {
            this.weights[1][h] -= lr * outputGrad * this.activations[0][h];
        }
        this.biases[1][0] -= lr * outputGrad;
        
        for (let h = 0; h < this.hiddenSize; h++) {
            for (let i = 0; i < this.inputSize; i++) {
                this.weights[0][h * this.inputSize + i] -= lr * hiddenGrad[h] * input[i];
            }
            this.biases[0][h] -= lr * hiddenGrad[h];
        }
    }
    
    train(features, actual_fps) {
        const input = Utils.toFeatureVector(features);
        const predicted = Utils.clamp(this.forward(input), 0, 144);
        
        this.backward(input, actual_fps, predicted);
        
        this._addHistory({ actual: actual_fps, predicted, error: actual_fps - predicted });
        return predicted;
    }
    
    predict(features) {
        const input = Utils.toFeatureVector(features);
        return Utils.clamp(this.forward(input), 0, 144);
    }
    
    _addHistory(entry) {
        entry.timestamp = Date.now();
        this.history.push(entry);
        if (this.history.length > this.maxHistory) this.history.shift();
    }
    
    export() {
        return {
            has_nn: true,
            nn_weights: [
                Array.from({length: this.hiddenSize}, (_, h) => 
                    Array.from({length: this.inputSize}, (_, i) => this.weights[0][h * this.inputSize + i])
                ),
                [Array.from(this.weights[1])]
            ],
            nn_biases: [
                Array.from(this.biases[0]),
                Array.from(this.biases[1])
            ]
        };
    }
    
    import(data) {
        if (!data || !data.nn_weights) return;
        const wh = data.nn_weights[0];
        const wo = data.nn_weights[1][0];
        for (let h = 0; h < this.hiddenSize; h++) {
            for (let i = 0; i < this.inputSize; i++) {
                this.weights[0][h * this.inputSize + i] = wh[h][i];
            }
        }
        for (let h = 0; h < this.hiddenSize; h++) {
            this.weights[1][h] = wo[h];
        }
        if (data.nn_biases) {
            this.biases[0] = Array.from(data.nn_biases[0]);
            this.biases[1] = Array.from(data.nn_biases[1]);
        }
    }
    
    reset() {
        this._initWeights();
        this.history = [];
    }
    
    getAccuracy() {
        if (this.history.length < 10) return 0;
        const recent = this.history.slice(-20);
        return (recent.filter(h => Math.abs(h.actual - h.predicted) <= 5).length / recent.length) * 100;
    }
    
    getMAE() {
        if (this.history.length < 5) return 0;
        const recent = this.history.slice(-20);
        return recent.reduce((sum, h) => sum + Math.abs(h.error), 0) / recent.length;
    }
}

// ============================================================
// Daemon 通信层 (完整适配)
// ============================================================
class DaemonConnector {
    constructor(options = {}) {
        this.apiBase = options.apiBase || 'http://localhost:8081/api';
        this.wsUrl = options.wsUrl || 'ws://localhost:8081/ws';
        this.pollInterval = options.pollInterval || 2000;
        
        this.connected = false;
        this.websocket = null;
        this.pollTimer = null;
        this.callbacks = {};
        
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
    }
    
    on(event, callback) {
        this.callbacks[event] = callback;
        return this;
    }
    
    _emit(event, ...args) {
        if (this.callbacks[event]) this.callbacks[event].apply(this, args);
    }
    
    connectWS() {
        try {
            this.websocket = new WebSocket(this.wsUrl);
            
            this.websocket.onopen = () => {
                this.connected = true;
                this.reconnectAttempts = 0;
                this._emit('onConnect');
                this._emit('onStatus', 'connected', 'WebSocket');
            };
            
            this.websocket.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this._handleMessage(data);
                } catch (e) {
                    console.error('Parse WS message failed:', e);
                }
            };
            
            this.websocket.onclose = () => {
                this.connected = false;
                this._emit('onDisconnect');
                this._scheduleReconnect();
            };
            
            this.websocket.onerror = (error) => {
                this._emit('onError', error);
            };
        } catch (e) {
            console.error('WebSocket init failed:', e);
            this._connectFallback();
        }
    }
    
    _connectFallback() {
        console.log('Fallback to HTTP polling');
        this.pollTimer = setInterval(() => this._pollStatus(), this.pollInterval);
        this._pollStatus();
        this.connected = true;
        this._emit('onConnect');
        this._emit('onStatus', 'connected', 'HTTP Poll');
    }
    
    _pollStatus() {
        this.httpGet('/status')
            .then(data => this._handleMessage(data))
            .catch(() => {});
    }
    
    _handleMessage(data) {
        if (!data) return;
        
        // WebSocket 消息类型
        if (data.type !== undefined) {
            switch (data.type) {
                case 1: // STATUS_UPDATE
                    this._emit('onData', data.data || data);
                    break;
                case 2: // MODEL_WEIGHTS
                    this._emit('onModelWeights', data.data || data);
                    break;
                default:
                    this._emit('onData', data);
            }
        } else if (data.timestamp !== undefined || data.fps !== undefined || data.cpu_util !== undefined) {
            // 直接是状态数据
            this._emit('onData', data);
        } else if (data.linear !== undefined || data.nn_weights !== undefined) {
            // 模型权重数据
            this._emit('onModelWeights', data);
        }
    }
    
    _scheduleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            this._emit('onStatus', 'failed', 'Max reconnect attempts');
            return;
        }
        this.reconnectAttempts++;
        const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 30000);
        this._emit('onStatus', 'reconnecting', `Attempt ${this.reconnectAttempts}`);
        setTimeout(() => this.connectWS(), delay);
    }
    
    async httpGet(endpoint) {
        const resp = await fetch(this.apiBase + endpoint, {
            method: 'GET',
            headers: { 'Accept': 'application/json' },
            signal: AbortSignal.timeout(5000)
        });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        return resp.json();
    }
    
    async httpPost(endpoint, data) {
        const resp = await fetch(this.apiBase + endpoint, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data),
            signal: AbortSignal.timeout(5000)
        });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        return resp.json();
    }
    
    // 获取状态
    async getStatus() {
        return this.httpGet('/status');
    }
    
    // 获取模型权重
    async getModelWeights() {
        return this.httpGet('/model');
    }
    
    // 设置模型 (线性/神经网络)
    async setModel(model) {
        return this.httpPost('/command', { cmd: 'set_model', model });
    }
    
    // 设置调度模式
    async setMode(mode) {
        return this.httpPost('/command', { cmd: 'set_mode', mode });
    }
    
    // 设置 uclamp
    async setUclamp(min, max) {
        return this.httpPost('/command', { cmd: 'set_uclamp', min, max });
    }
    
    // 设置温控预设
    async setThermalPreset(preset) {
        return this.httpPost('/command', { cmd: 'set_thermal', preset });
    }
    
    disconnect() {
        if (this.websocket) {
            this.websocket.close();
            this.websocket = null;
        }
        if (this.pollTimer) {
            clearInterval(this.pollTimer);
            this.pollTimer = null;
        }
        this.connected = false;
    }
}

// ============================================================
// 模型管理器
// ============================================================
class ModelManager {
    constructor() {
        this.storageKey = 'hp_models_v2';
        this.linear = new LinearPredictor();
        this.neural = new SimpleNeuralPredictor();
        this.currentModel = 'neural';
        
        this._load();
    }
    
    get current() {
        return this.currentModel === 'neural' ? this.neural : this.linear;
    }
    
    switchModel(name) {
        if (name !== 'linear' && name !== 'neural') return false;
        this.currentModel = name;
        this._save();
        return true;
    }
    
    train(status, actual_fps) {
        if (!status.frame_interval_us && actual_fps) {
            status.frame_interval_us = Utils.calcFrameInterval(actual_fps);
        }
        
        const linearPred = this.linear.train(status, actual_fps);
        const neuralPred = this.neural.train(status, actual_fps);
        
        return {
            linear: linearPred,
            neural: neuralPred,
            actual: actual_fps,
            linearError: Math.abs(actual_fps - linearPred),
            neuralError: Math.abs(actual_fps - neuralPred)
        };
    }
    
    predict(status) {
        return {
            value: this.current.predict(status),
            model: this.currentModel
        };
    }
    
    getStats() {
        return {
            linear: { accuracy: this.linear.getAccuracy(), mae: this.linear.getMAE() },
            neural: { accuracy: this.neural.getAccuracy(), mae: this.neural.getMAE() },
            current: this.currentModel
        };
    }
    
    getWeights() {
        return {
            ...this.linear.export(),
            ...this.neural.export()
        };
    }
    
    _save() {
        try {
            localStorage.setItem(this.storageKey, JSON.stringify({
                linear: this.linear.export(),
                neural: this.neural.export(),
                current: this.currentModel
            }));
        } catch (e) {}
    }
    
    _load() {
        try {
            const data = JSON.parse(localStorage.getItem(this.storageKey));
            if (data) {
                this.linear.import(data.linear);
                if (data.neural && data.neural.nn_weights) this.neural.import(data.neural);
                this.currentModel = data.current || 'neural';
            }
        } catch (e) {}
    }
    
    reset() {
        this.linear.reset();
        this.neural.reset();
        this._save();
    }
}

// ============================================================
// 主应用 - 完整适配所有接口
// ============================================================
class HyperPredictApp {
    constructor() {
        this.models = new ModelManager();
        this.daemon = new DaemonConnector({
            apiBase: 'http://localhost:8081/api',
            wsUrl: 'ws://localhost:8081/ws'
        });
        
        // 状态 (与 C++ StatusUpdate 对齐)
        this.state = {
            connected: false,
            mode: 'daily',
            targetFps: 60,
            
            // 实时数据
            fps: 60,
            cpu_util: 512,
            run_queue_len: 1,
            wakeups_100ms: 0,
            frame_interval_us: 16667,
            touch_rate_100ms: 0,
            thermal_margin: 20,
            battery_level: 100,
            is_gaming: false,
            temperature: 42,
            uclamp_min: 50,
            uclamp_max: 100,
            
            // 集群数据
            clusters: {
                little: { freq: 400, usage: 30 },
                mid: { freq: 800, usage: 25 },
                big: { freq: 1800, usage: 15 }
            },
            
            // 双电芯
            dualCell: false,
            
            // 平滑显示值
            display: { fps: 60, temp: 42, battery: 100 }
        };
        
        // 图表数据
        this.thermalData = new Array(30).fill(42);
        this.predictorData = [];
        this.maxPredictorData = 50;
        
        // Canvas
        this.thermalCtx = null;
        this.predCtx = null;
        this.thermalW = 300;
        this.thermalH = 120;
        this.predW = 300;
        this.predH = 150;
        
        this.init();
    }
    
    init() {
        this._setupCallbacks();
        this._setupEvents();
        this._initCanvases();
        this._connectDaemon();
        this._startSimulation();
        this._startRender();
        
        this._log('HyperPredict 初始化完成', 'info');
    }
    
    // ============================================================
    // 回调设置
    // ============================================================
    _setupCallbacks() {
        this.daemon
            .on('onConnect', () => {
                this.state.connected = true;
                this._log('守护进程已连接', 'success');
                this._syncModelFromDaemon();
            })
            .on('onDisconnect', () => {
                this.state.connected = false;
                this._log('守护进程断开，切换到模拟模式', 'warning');
            })
            .on('onStatus', (status, detail) => this._updateConnectionUI(status, detail))
            .on('onData', (data) => this._processStatus(data))
            .on('onModelWeights', (data) => this._syncModelFromDaemon(data))
            .on('onError', (e) => this._log(`错误: ${e}`, 'error'));
    }
    
    // ============================================================
    // 事件绑定
    // ============================================================
    _setupEvents() {
        // 导航切换
        document.querySelectorAll('.nav-item').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.nav-item').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                const panel = btn.dataset.panel;
                document.querySelectorAll('.card').forEach(c => c.classList.remove('hidden'));
            });
        });
        
        // 模式切换 (均衡/游戏/性能)
        document.querySelectorAll('.segment-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.segment-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                this.state.mode = btn.dataset.mode;
                this.state.targetFps = { daily: 60, game: 90, turbo: 120 }[this.state.mode] || 60;
                this._log(`调度模式: ${this.state.mode}`, 'info');
                if (this.state.connected) {
                    this.daemon.setMode(this.state.mode);
                }
            });
        });
        
        // 模型切换 (神经网络/线性回归)
        document.querySelectorAll('.model-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                const model = btn.dataset.model;
                if (this.models.switchModel(model)) {
                    document.querySelectorAll('.model-btn').forEach(b => b.classList.remove('active'));
                    btn.classList.add('active');
                    const name = model === 'neural' ? '神经网络' : '线性回归';
                    this._log(`预测模型: ${name}`, 'info');
                    if (this.state.connected) {
                        this.daemon.setModel(model);
                    }
                }
            });
        });
        
        // 双电芯开关
        const dualCellToggle = document.getElementById('dual-cell-toggle');
        if (dualCellToggle) {
            dualCellToggle.addEventListener('change', (e) => {
                this.state.dualCell = e.target.checked;
                this._log(`双电芯模式: ${this.state.dualCell ? '启用' : '禁用'}`, 'info');
            });
        }
        
        // uclamp 滑块
        const uclampMinSlider = document.getElementById('uclamp-min-slider');
        const uclampMaxSlider = document.getElementById('uclamp-max-slider');
        const uclampMinValue = document.getElementById('uclamp-min-value');
        const uclampMaxValue = document.getElementById('uclamp-max-value');
        
        if (uclampMinSlider) {
            uclampMinSlider.addEventListener('input', () => {
                if (uclampMinValue) uclampMinValue.textContent = uclampMinSlider.value + '%';
            });
            uclampMinSlider.addEventListener('change', async () => {
                const min = parseInt(uclampMinSlider.value);
                const max = parseInt(uclampMaxSlider?.value || 100);
                this.state.uclamp_min = min;
                try {
                    await this.daemon.setUclamp(min, max);
                    this._log(`uclamp.min: ${min}%`, 'success');
                } catch (e) {
                    this._log('uclamp.min 设置失败', 'error');
                }
            });
        }
        
        if (uclampMaxSlider) {
            uclampMaxSlider.addEventListener('input', () => {
                if (uclampMaxValue) uclampMaxValue.textContent = uclampMaxSlider.value + '%';
            });
            uclampMaxSlider.addEventListener('change', async () => {
                const min = parseInt(uclampMinSlider?.value || 50);
                const max = parseInt(uclampMaxSlider.value);
                this.state.uclamp_max = max;
                try {
                    await this.daemon.setUclamp(min, max);
                    this._log(`uclamp.max: ${max}%`, 'success');
                } catch (e) {
                    this._log('uclamp.max 设置失败', 'error');
                }
            });
        }
        
        // 温控预设
        document.querySelectorAll('.preset-btn').forEach(btn => {
            btn.addEventListener('click', async () => {
                document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                const preset = btn.dataset.preset;
                this._log(`温控预设: ${preset}`, 'info');
                if (this.state.connected) {
                    try {
                        await this.daemon.setThermalPreset(preset);
                    } catch (e) {}
                }
            });
        });
        
        // 清空日志
        const clearLogBtn = document.getElementById('clear-log');
        if (clearLogBtn) {
            clearLogBtn.addEventListener('click', () => {
                const container = document.getElementById('log-container');
                if (container) container.innerHTML = '';
                this._log('日志已清空', 'info');
            });
        }
        
        // 重置模型
        const resetBtn = document.getElementById('reset-models');
        if (resetBtn) {
            resetBtn.addEventListener('click', () => {
                this.models.reset();
                this._updateModelStats();
                this._log('模型已重置', 'info');
            });
        }
        
        // 导出模型
        const exportBtn = document.getElementById('export-model');
        if (exportBtn) {
            exportBtn.addEventListener('click', () => this._exportModels());
        }
        
        // 导入模型
        const importBtn = document.getElementById('import-model');
        const importFile = document.getElementById('import-file');
        if (importBtn && importFile) {
            importFile.addEventListener('change', (e) => this._importModels(e));
        }
    }
    
    _initCanvases() {
        const initCanvas = (id, prefix) => {
            const el = document.getElementById(id);
            if (el) {
                const ctx = el.getContext('2d');
                const rect = el.getBoundingClientRect();
                el.width = rect.width * 2;
                el.height = rect.height * 2;
                ctx.scale(2, 2);
                if (prefix === 'thermal') {
                    this.thermalCtx = ctx;
                    this.thermalW = rect.width;
                    this.thermalH = rect.height;
                } else {
                    this.predCtx = ctx;
                    this.predW = rect.width;
                    this.predH = rect.height;
                }
            }
        };
        
        initCanvas('thermal-canvas', 'thermal');
        initCanvas('predictor-canvas', 'pred');
    }
    
    _connectDaemon() {
        this.daemon.connectWS();
    }
    
    // ============================================================
    // 数据模拟 (无后端时使用)
    // ============================================================
    _startSimulation() {
        this.simTimer = setInterval(() => this._simulate(), 2000);
        this._simulate();
    }
    
    _simulate() {
        const baseFps = this.state.targetFps;
        const variance = baseFps * 0.15;
        
        this.state.fps = Utils.clamp(Math.round(baseFps + (Math.random() - 0.5) * variance), 24, 144);
        
        const loadFactor = this.state.fps / 144;
        this.state.cpu_util = Utils.clamp(Math.round(150 + loadFactor * 700 + Math.random() * 150), 0, 1024);
        this.state.run_queue_len = Utils.clamp(Math.round(1 + (1 - loadFactor) * 4 + Math.random() * 2), 1, 32);
        this.state.wakeups_100ms = Math.round(5 + loadFactor * 50 + Math.random() * 20);
        this.state.frame_interval_us = Utils.calcFrameInterval(this.state.fps);
        this.state.touch_rate_100ms = Math.random() > 0.9 ? Math.round(Math.random() * 15) : 0;
        this.state.thermal_margin = Math.round(10 + (1 - this.state.cpu_util / 1024) * 30 + Math.random() * 10);
        
        let temp = 35 + (this.state.cpu_util / 1024) * 25 + Math.random() * 5;
        if (this.state.dualCell) temp = Math.max(30, temp - 5);
        this.state.temperature = Math.round(temp);
        
        this.state.battery_level = Math.max(0, this.state.battery_level - 0.01);
        this.state.is_gaming = this.state.mode !== 'daily' || this.state.touch_rate_100ms > 5;
        
        // 模拟集群数据
        ['little', 'mid', 'big'].forEach(cluster => {
            const baseFreq = cluster === 'big' ? 1800 : cluster === 'mid' ? 1000 : 500;
            this.state.clusters[cluster].freq = Math.round(baseFreq + loadFactor * 500 + Math.random() * 300);
            this.state.clusters[cluster].usage = Math.round(15 + loadFactor * 70 + Math.random() * 15);
        });
        
        this._trainModels();
        this._updateModelStats();
    }
    
    // ============================================================
    // 后端数据处理
    // ============================================================
    _processStatus(data) {
        // 与 C++ StatusUpdate 完全对齐
        const status = {
            fps: data.fps || data.target_fps || 60,
            cpu_util: data.cpu_util || 512,
            run_queue_len: data.run_queue_len || 1,
            wakeups_100ms: data.wakeups_100ms || 0,
            frame_interval_us: data.frame_interval_us || 16667,
            touch_rate_100ms: data.touch_rate_100ms || 0,
            thermal_margin: data.thermal_margin || 20,
            battery_level: data.battery_level || 100,
            is_gaming: data.is_gaming || false,
            temperature: data.temperature || 42,
            uclamp_min: data.uclamp_min || 50,
            uclamp_max: data.uclamp_max || 100,
            mode: data.mode || 'daily',
            clusters: data.clusters || this.state.clusters
        };
        
        Object.assign(this.state, status);
        
        // 更新模式按钮状态
        const modeMap = { daily: 0, game: 1, turbo: 2, '0': 0, '1': 1, '2': 2 };
        this.state.targetFps = modeMap[status.mode] === 0 ? 60 : modeMap[status.mode] === 1 ? 90 : 120;
        
        // 更新 UI 控件状态
        this._updateUIControls();
        
        this._trainModels();
        this._updateModelStats();
    }
    
    _updateUIControls() {
        // 更新模式按钮
        document.querySelectorAll('.segment-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.mode === this.state.mode);
        });
        
        // 更新 uclamp 滑块
        const uclampMinSlider = document.getElementById('uclamp-min-slider');
        const uclampMaxSlider = document.getElementById('uclamp-max-slider');
        const uclampMinValue = document.getElementById('uclamp-min-value');
        const uclampMaxValue = document.getElementById('uclamp-max-value');
        
        if (uclampMinSlider) uclampMinSlider.value = this.state.uclamp_min;
        if (uclampMaxSlider) uclampMaxSlider.value = this.state.uclamp_max;
        if (uclampMinValue) uclampMinValue.textContent = this.state.uclamp_min + '%';
        if (uclampMaxValue) uclampMaxValue.textContent = this.state.uclamp_max + '%';
    }
    
    _trainModels() {
        const result = this.models.train(this.state, this.state.fps);
        
        // 记录图表数据
        this.predictorData.push({
            actual: result.actual,
            linear: result.linear,
            neural: result.neural,
            timestamp: Date.now()
        });
        if (this.predictorData.length > this.maxPredictorData) this.predictorData.shift();
        
        // 记录温度数据
        this.thermalData.push(this.state.temperature);
        if (this.thermalData.length > 30) this.thermalData.shift();
    }
    
    // ============================================================
    // 模型同步
    // ============================================================
    _syncModelFromDaemon(data) {
        if (!data) {
            this.daemon.getModelWeights()
                .then(d => this._syncModelFromDaemon(d))
                .catch(() => {});
            return;
        }
        
        // 解析后端返回的模型权重
        if (data.linear) {
            this.models.linear.import(data.linear);
        }
        if (data.nn_weights) {
            this.models.neural.import(data);
        }
        if (data.has_nn !== undefined) {
            this.models.switchModel(data.has_nn ? 'neural' : 'linear');
        }
        
        this._updateModelStats();
    }
    
    _updateModelStats() {
        const stats = this.models.getStats();
        
        const setText = (id, val) => {
            const el = document.getElementById(id);
            if (el) el.textContent = val;
        };
        
        setText('linear-accuracy', stats.linear.accuracy.toFixed(1) + '%');
        setText('neural-accuracy', stats.neural.accuracy.toFixed(1) + '%');
        setText('linear-mae', stats.linear.mae.toFixed(2));
        setText('neural-mae', stats.neural.mae.toFixed(2));
        setText('current-model', stats.current === 'neural' ? '神经网络' : '线性回归');
        
        // 更新模型按钮状态
        document.querySelectorAll('.model-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.model === stats.current);
        });
    }
    
    _updateConnectionUI(status, detail) {
        const statusDot = document.getElementById('conn-status');
        const statusText = document.getElementById('conn-text');
        const connType = document.getElementById('conn-type');
        
        if (statusDot) {
            statusDot.className = 'status-dot';
            if (status === 'connected') statusDot.classList.add('connected');
            else if (status === 'error' || status === 'failed') statusDot.classList.add('error');
            else if (status === 'reconnecting') statusDot.classList.add('reconnecting');
        }
        
        const labels = {
            'connected': '已连接',
            'disconnected': '未连接',
            'error': '连接错误',
            'reconnecting': '重连中',
            'failed': '连接失败'
        };
        if (statusText) statusText.textContent = labels[status] || status;
        if (connType) connType.textContent = detail || '';
    }
    
    // ============================================================
    // 渲染循环
    // ============================================================
    _startRender() {
        const loop = () => {
            this._renderUI();
            this._drawCharts();
            requestAnimationFrame(loop);
        };
        requestAnimationFrame(loop);
    }
    
    _renderUI() {
        const lerp = (c, t, f) => c + (t - c) * f;
        const sf = 0.12;
        
        // 平滑显示值
        this.state.display.fps = lerp(this.state.display.fps, this.state.fps, sf);
        this.state.display.temp = lerp(this.state.display.temp, this.state.temperature, sf);
        this.state.display.battery = lerp(this.state.display.battery, this.state.battery_level, sf * 0.5);
        
        // FPS 监控
        const fpsEl = document.getElementById('fps-value');
        if (fpsEl) fpsEl.textContent = Math.round(this.state.display.fps);
        
        const fpsStatus = document.getElementById('fps-status');
        if (fpsStatus) {
            const fps = this.state.display.fps;
            fpsStatus.textContent = fps >= 90 ? '流畅' : fps >= 60 ? '良好' : fps >= 30 ? '一般' : '卡顿';
            fpsStatus.style.color = fps >= 90 ? '#34A853' : fps >= 60 ? '#4285F4' : fps >= 30 ? '#FBBC04' : '#EA4335';
        }
        
        // 温度
        const tempEl = document.getElementById('temp-value');
        if (tempEl) {
            const temp = this.state.display.temp;
            tempEl.textContent = Math.round(temp);
            tempEl.style.color = temp < 38 ? '#34A853' : temp < 45 ? '#4285F4' : temp < 50 ? '#FBBC04' : '#EA4335';
        }
        
        // 电池
        const batteryEl = document.getElementById('battery-value');
        if (batteryEl) batteryEl.textContent = Math.round(this.state.display.battery);
        
        // CPU 信息
        const cpuUtilEl = document.getElementById('cpu-util');
        const rqLenEl = document.getElementById('rq-length');
        if (cpuUtilEl) cpuUtilEl.textContent = Math.round(this.state.cpu_util);
        if (rqLenEl) rqLenEl.textContent = this.state.run_queue_len;
        
        // 集群数据
        ['little', 'mid', 'big'].forEach(cluster => {
            const freqEl = document.getElementById(`freq-${cluster}`);
            const usageEl = document.getElementById(`usage-${cluster}`);
            const freqBarEl = document.getElementById(`freq-bar-${cluster}`);
            
            if (freqEl) freqEl.textContent = Math.round(this.state.clusters[cluster].freq);
            if (usageEl) usageEl.style.width = `${Math.min(100, this.state.clusters[cluster].usage)}%`;
            if (freqBarEl) freqBarEl.style.width = `${(this.state.clusters[cluster].freq / 2800) * 100}%`;
        });
        
        // 温控信息
        const thermalMarginEl = document.getElementById('thermal-margin');
        if (thermalMarginEl) thermalMarginEl.textContent = this.state.thermal_margin + '°C';
        
        // 预测结果
        const pred = this.models.predict(this.state);
        const setText = (id, val) => {
            const el = document.getElementById(id);
            if (el) el.textContent = val;
        };
        setText('predicted-fps', Math.round(pred.value));
        setText('actual-fps', Math.round(this.state.display.fps));
        setText('confidence', this.models.current.getAccuracy().toFixed(1) + '%');
    }
    
    // ============================================================
    // 图表绘制
    // ============================================================
    _drawCharts() {
        this._drawThermalChart();
        this._drawPredictorChart();
    }
    
    _drawThermalChart() {
        if (!this.thermalCtx || this.thermalData.length < 2) return;
        
        const ctx = this.thermalCtx;
        const w = this.thermalW, h = this.thermalH;
        
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = 'rgba(66, 133, 244, 0.05)';
        ctx.fillRect(0, 0, w, h);
        
        const step = w / (this.thermalData.length - 1);
        const maxTemp = 70;
        
        // 填充区域
        ctx.beginPath();
        ctx.moveTo(0, h);
        this.thermalData.forEach((temp, i) => ctx.lineTo(i * step, h - (temp / maxTemp) * h));
        ctx.lineTo(w, h);
        ctx.closePath();
        
        const gradient = ctx.createLinearGradient(0, 0, 0, h);
        gradient.addColorStop(0, 'rgba(234, 67, 53, 0.4)');
        gradient.addColorStop(0.5, 'rgba(251, 188, 4, 0.3)');
        gradient.addColorStop(1, 'rgba(52, 168, 83, 0.2)');
        ctx.fillStyle = gradient;
        ctx.fill();
        
        // 线条
        ctx.beginPath();
        ctx.strokeStyle = '#EA4335';
        ctx.lineWidth = 2;
        this.thermalData.forEach((temp, i) => {
            const x = i * step, y = h - (temp / maxTemp) * h;
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });
        ctx.stroke();
        
        // 标签
        ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
        ctx.font = '10px sans-serif';
        ctx.fillText('70°', 2, 12);
        ctx.fillText('35°', 2, h - 4);
    }
    
    _drawPredictorChart() {
        if (!this.predCtx || this.predictorData.length < 2) return;
        
        const ctx = this.predCtx;
        const w = this.predW, h = this.predH;
        const data = this.predictorData;
        const maxFps = 144;
        
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = 'rgba(66, 133, 244, 0.03)';
        ctx.fillRect(0, 0, w, h);
        
        // 网格
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
        ctx.lineWidth = 0.5;
        for (let i = 0; i <= 4; i++) {
            const y = (h / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            ctx.stroke();
        }
        
        // 目标线
        const targetY = h - (this.state.targetFps / maxFps) * h;
        ctx.beginPath();
        ctx.strokeStyle = 'rgba(251, 188, 4, 0.5)';
        ctx.setLineDash([4, 4]);
        ctx.moveTo(0, targetY);
        ctx.lineTo(w, targetY);
        ctx.stroke();
        ctx.setLineDash([]);
        
        const step = w / (data.length - 1);
        
        // 实际 FPS
        ctx.beginPath();
        ctx.strokeStyle = '#34A853';
        ctx.lineWidth = 2;
        data.forEach((item, i) => {
            const x = i * step, y = h - (item.actual / maxFps) * h;
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });
        ctx.stroke();
        
        // 神经网络
        ctx.beginPath();
        ctx.strokeStyle = '#4285F4';
        ctx.setLineDash([6, 3]);
        data.forEach((item, i) => {
            const x = i * step, y = h - (item.neural / maxFps) * h;
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });
        ctx.stroke();
        ctx.setLineDash([]);
        
        // 线性
        ctx.beginPath();
        ctx.strokeStyle = '#9C27B0';
        ctx.setLineDash([2, 2]);
        data.forEach((item, i) => {
            const x = i * step, y = h - (item.linear / maxFps) * h;
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });
        ctx.stroke();
        ctx.setLineDash([]);
        
        // 图例
        ctx.font = '10px sans-serif';
        ctx.fillStyle = '#34A853';
        ctx.fillText('● 实际', 4, 12);
        ctx.fillStyle = '#4285F4';
        ctx.fillText('● 神经网络', 50, 12);
        ctx.fillStyle = '#9C27B0';
        ctx.fillText('● 线性', 115, 12);
    }
    
    // ============================================================
    // 日志
    // ============================================================
    _log(msg, type = 'info') {
        const container = document.getElementById('log-container');
        if (!container) return;
        
        const now = new Date();
        const time = now.toTimeString().slice(0, 8);
        
        const div = document.createElement('div');
        div.className = `log-item log-${type}`;
        div.innerHTML = `<span class="log-time">[${time}]</span><span class="log-msg">${msg}</span>`;
        container.insertBefore(div, container.firstChild);
        
        while (container.children.length > 20) container.removeChild(container.lastChild);
    }
    
    // ============================================================
    // 导入/导出
    // ============================================================
    _exportModels() {
        const data = {
            linear: this.models.linear.export(),
            neural: this.models.neural.export(),
            current: this.models.currentModel,
            exportTime: new Date().toISOString()
        };
        
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `hp_model_${Date.now()}.json`;
        a.click();
        URL.revokeObjectURL(url);
        
        this._log('模型已导出', 'success');
    }
    
    _importModels(event) {
        const file = event.target.files?.[0];
        if (!file) return;
        
        const reader = new FileReader();
        reader.onload = (e) => {
            try {
                const data = JSON.parse(e.target.result);
                this.models.linear.import(data.linear);
                if (data.neural && data.neural.nn_weights) this.models.neural.import(data.neural);
                if (data.current) this.models.switchModel(data.current);
                this._updateModelStats();
                this._log('模型已导入', 'success');
            } catch (err) {
                this._log('导入失败: 无效格式', 'error');
            }
        };
        reader.readAsText(file);
        event.target.value = '';
    }
}

// ============================================================
// 启动
// ============================================================
window.addEventListener('DOMContentLoaded', () => {
    window.hpApp = new HyperPredictApp();
});
