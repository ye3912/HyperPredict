/**
 * HyperPredict WebUI - Neural Network Predictor + WebSocket Client
 * 
 * Features:
 * - MLP Neural Network (8→16→8→1) for FPS prediction
 * - WebSocket real-time communication
 * - Model weight management and persistence
 */

class NeuralNetworkPredictor {
    constructor(layerSizes = [8, 16, 8, 1]) {
        this.layerSizes = layerSizes;
        this.weights = [];
        this.biases = [];
        this.initializeWeights();
    }

    initializeWeights() {
        this.weights = [];
        this.biases = [];
        
        for (let i = 0; i < this.layerSizes.length - 1; i++) {
            const inputSize = this.layerSizes[i];
            const outputSize = this.layerSizes[i + 1];
            
            // Xavier initialization
            const scale = Math.sqrt(2.0 / (inputSize + outputSize));
            
            const w = [];
            for (let j = 0; j < outputSize; j++) {
                const row = [];
                for (let k = 0; k < inputSize; k++) {
                    row.push((Math.random() * 2 - 1) * scale);
                }
                w.push(row);
            }
            this.weights.push(w);
            this.biases.push(new Array(outputSize).fill(0));
        }
    }

    relu(x) {
        return Math.max(0, x);
    }

    reluDerivative(x) {
        return x > 0 ? 1 : 0;
    }

    forward(input) {
        let activation = input;
        const activations = [input];
        
        for (let i = 0; i < this.weights.length; i++) {
            const w = this.weights[i];
            const b = this.biases[i];
            const next = [];
            
            for (let j = 0; j < w.length; j++) {
                let sum = b[j];
                for (let k = 0; k < w[j].length; k++) {
                    sum += w[j][k] * activation[k];
                }
                // Output layer is linear, hidden layers use ReLU
                next.push(i < this.weights.length - 1 ? this.relu(sum) : sum);
            }
            
            activation = next;
            activations.push(activation);
        }
        
        return activation[0];
    }

    predict(features) {
        // features: [cpu_util, run_queue, wakeups, frame_interval, touch_rate, thermal_margin, battery, fps_history]
        return this.forward(features);
    }

    train(features, target, learningRate = 0.001) {
        // Simplified training using gradient descent
        const input = features;
        const activations = [input];
        let activation = input;
        
        // Forward pass
        const preActivations = [];
        for (let i = 0; i < this.weights.length; i++) {
            const w = this.weights[i];
            const b = this.biases[i];
            const preAct = [];
            const next = [];
            
            for (let j = 0; j < w.length; j++) {
                let sum = b[j];
                for (let k = 0; k < w[j].length; k++) {
                    sum += w[j][k] * activation[k];
                }
                preAct.push(sum);
                next.push(i < this.weights.length - 1 ? this.relu(sum) : sum);
            }
            
            preActivations.push(preAct);
            activation = next;
            activations.push(activation);
        }
        
        // Backward pass
        const output = activations[activations.length - 1];
        let delta = output.map((o, i) => (o - target) * (i === 0 ? 1 : this.reluDerivative(preActivations[preActivations.length - 1][i])));
        
        for (let i = this.weights.length - 1; i >= 0; i--) {
            const newDelta = [];
            
            // Update weights and biases
            for (let j = 0; j < this.weights[i].length; j++) {
                for (let k = 0; k < this.weights[i][j].length; k++) {
                    this.weights[i][j][k] -= learningRate * delta[j] * activations[i][k];
                }
                this.biases[i][j] -= learningRate * delta[j];
            }
            
            // Compute delta for previous layer
            if (i > 0) {
                for (let k = 0; k < this.weights[i][0].length; k++) {
                    let sum = 0;
                    for (let j = 0; j < this.weights[i].length; j++) {
                        sum += this.weights[i][j][k] * delta[j];
                    }
                    newDelta.push(sum * this.reluDerivative(preActivations[i - 1][k]));
                }
                delta = newDelta;
            }
        }
    }

    toJSON() {
        return {
            layerSizes: this.layerSizes,
            weights: this.weights,
            biases: this.biases
        };
    }

    fromJSON(json) {
        this.layerSizes = json.layerSizes;
        this.weights = json.weights;
        this.biases = json.biases;
    }
}


class DaemonConnector {
    constructor(url = null) {
        // Auto-detect WebSocket URL
        this.url = url || this.detectWsUrl();
        this.ws = null;
        this.connected = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 2000;
        this.pingInterval = null;
        this.messageHandlers = new Map();
    }

    detectWsUrl() {
        const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const host = window.location.hostname || 'localhost';
        const port = window.location.port || '8081';
        return `${proto}//${host}:${port}`;
    }

    connect() {
        return new Promise((resolve, reject) => {
            try {
                console.log(`[WS] Connecting to ${this.url}...`);
                this.ws = new WebSocket(this.url);
                
                this.ws.onopen = () => {
                    console.log('[WS] Connected!');
                    this.connected = true;
                    this.reconnectAttempts = 0;
                    this.startPing();
                    this.updateConnectionStatus('connected');
                    this.send({ type: 'subscribe', channels: ['status', 'model'] });
                    resolve();
                };

                this.ws.onmessage = (event) => {
                    try {
                        const msg = JSON.parse(event.data);
                        this.handleMessage(msg);
                    } catch (e) {
                        console.warn('[WS] Parse error:', e);
                    }
                };

                this.ws.onclose = () => {
                    console.log('[WS] Disconnected');
                    this.connected = false;
                    this.stopPing();
                    this.updateConnectionStatus('disconnected');
                    this.attemptReconnect();
                };

                this.ws.onerror = (error) => {
                    console.error('[WS] Error:', error);
                    this.updateConnectionStatus('error');
                    reject(error);
                };

                // Timeout
                setTimeout(() => {
                    if (!this.connected) {
                        reject(new Error('Connection timeout'));
                    }
                }, 5000);

            } catch (e) {
                reject(e);
            }
        });
    }

    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.stopPing();
    }

    startPing() {
        this.pingInterval = setInterval(() => {
            if (this.connected) {
                this.send({ type: 'ping', timestamp: Date.now() });
            }
        }, 15000);
    }

    stopPing() {
        if (this.pingInterval) {
            clearInterval(this.pingInterval);
            this.pingInterval = null;
        }
    }

    attemptReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.log('[WS] Max reconnect attempts reached');
            return;
        }

        this.reconnectAttempts++;
        const delay = this.reconnectDelay * this.reconnectAttempts;
        
        console.log(`[WS] Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts})...`);
        this.updateConnectionStatus('reconnecting');
        
        setTimeout(() => {
            this.connect().catch(() => {});
        }, delay);
    }

    send(data) {
        if (this.ws && this.connected) {
            this.ws.send(JSON.stringify(data));
        }
    }

    on(messageType, handler) {
        this.messageHandlers.set(messageType, handler);
    }

    handleMessage(msg) {
        const handler = this.messageHandlers.get(msg.type);
        if (handler) {
            handler(msg);
        }
    }

    updateConnectionStatus(status) {
        const el = document.getElementById('connectionStatus');
        if (!el) return;
        
        const dot = el.querySelector('.status-dot');
        const text = el.querySelector('.status-text');
        
        dot.className = 'status-dot';
        
        switch (status) {
            case 'connected':
                dot.classList.add('connected');
                text.textContent = '已连接';
                break;
            case 'disconnected':
                text.textContent = '断开连接';
                break;
            case 'reconnecting':
                text.textContent = '重新连接中...';
                break;
            case 'error':
                dot.classList.add('error');
                text.textContent = '连接错误';
                break;
        }
    }

    // HTTP fallback methods
    async httpGet(path) {
        try {
            const resp = await fetch(`${window.location.origin}${path}`);
            return await resp.json();
        } catch (e) {
            console.error('[HTTP] GET error:', e);
            return null;
        }
    }

    async httpPost(path, data) {
        try {
            const resp = await fetch(`${window.location.origin}${path}`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data)
            });
            return await resp.json();
        } catch (e) {
            console.error('[HTTP] POST error:', e);
            return null;
        }
    }
}


class ModelManager {
    constructor(predictor) {
        this.predictor = predictor;
        this.storageKey = 'hyperpredict_model';
        this.isDirty = false;
    }

    exportModel() {
        const data = this.predictor.toJSON();
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        
        const a = document.createElement('a');
        a.href = url;
        a.download = `hyperpredict_model_${Date.now()}.json`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        return data;
    }

    importModel(file) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onload = (e) => {
                try {
                    const data = JSON.parse(e.target.result);
                    this.predictor.fromJSON(data);
                    this.isDirty = true;
                    this.save();
                    resolve(data);
                } catch (err) {
                    reject(err);
                }
            };
            reader.onerror = reject;
            reader.readAsText(file);
        });
    }

    save() {
        try {
            localStorage.setItem(this.storageKey, JSON.stringify(this.predictor.toJSON()));
            this.isDirty = false;
        } catch (e) {
            console.error('[Model] Save error:', e);
        }
    }

    load() {
        try {
            const data = localStorage.getItem(this.storageKey);
            if (data) {
                this.predictor.fromJSON(JSON.parse(data));
                return true;
            }
        } catch (e) {
            console.error('[Model] Load error:', e);
        }
        return false;
    }

    reset() {
        this.predictor.initializeWeights();
        this.isDirty = true;
        this.save();
    }

    getWeights() {
        return this.predictor.toJSON();
    }

    setWeights(weights) {
        try {
            this.predictor.fromJSON(weights);
            this.isDirty = true;
            return true;
        } catch (e) {
            console.error('[Model] Set weights error:', e);
            return false;
        }
    }
}


// Application Controller
class HyperPredictApp {
    constructor() {
        this.daemon = new DaemonConnector();
        this.predictor = new NeuralNetworkPredictor([8, 16, 8, 1]);
        this.modelManager = new ModelManager(this.predictor);
        
        this.currentMode = 'game';
        this.uclampMin = 50;
        this.uclampMax = 100;
        this.thermalPreset = 'balanced';
        
        this.lastStatus = null;
    }

    async init() {
        console.log('[App] Initializing...');
        
        // Load saved model
        this.modelManager.load();
        
        // Setup UI handlers
        this.setupModeSelector();
        this.setupUclampSliders();
        this.setupThermalPresets();
        this.setupModelControls();
        this.setupLogControls();
        
        // Connect to daemon
        this.setupDaemonCallbacks();
        
        try {
            await this.daemon.connect();
        } catch (e) {
            this.log(`连接失败: ${e.message}`, 'error');
            this.log('请确保 HyperPredict 守护进程正在运行', 'warning');
        }
        
        console.log('[App] Ready');
    }

    setupModeSelector() {
        const buttons = document.querySelectorAll('.mode-btn');
        buttons.forEach(btn => {
            btn.addEventListener('click', () => {
                buttons.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                this.currentMode = btn.dataset.mode;
                this.sendCommand('mode', { mode: this.currentMode });
                this.log(`切换到 ${this.getModeName(this.currentMode)} 模式`, 'success');
            });
        });
    }

    setupUclampSliders() {
        const minSlider = document.getElementById('uclampMin');
        const maxSlider = document.getElementById('uclampMax');
        const minValue = document.getElementById('uclampMinValue');
        const maxValue = document.getElementById('uclampMaxValue');
        const applyBtn = document.getElementById('applyUclamp');
        
        minSlider.addEventListener('input', () => {
            this.uclampMin = parseInt(minSlider.value);
            minValue.textContent = `${this.uclampMin}%`;
        });
        
        maxSlider.addEventListener('input', () => {
            this.uclampMax = parseInt(maxSlider.value);
            maxValue.textContent = `${this.uclampMax}%`;
        });
        
        applyBtn.addEventListener('click', () => {
            this.sendCommand('uclamp', { min: this.uclampMin, max: this.uclampMax });
            this.log(`UCLAMP: min=${this.uclampMin}%, max=${this.uclampMax}%`, 'success');
            this.showToast('设置已应用', 'success');
        });
    }

    setupThermalPresets() {
        const buttons = document.querySelectorAll('.preset-btn');
        buttons.forEach(btn => {
            btn.addEventListener('click', () => {
                buttons.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                this.thermalPreset = btn.dataset.preset;
                this.sendCommand('thermal', { preset: this.thermalPreset });
                this.log(`温控预设: ${this.getPresetName(this.thermalPreset)}`, 'success');
            });
        });
    }

    setupModelControls() {
        document.getElementById('exportModel').addEventListener('click', () => {
            this.modelManager.exportModel();
            this.log('模型已导出', 'success');
        });
        
        document.getElementById('importModel').addEventListener('click', () => {
            document.getElementById('modelFile').click();
        });
        
        document.getElementById('modelFile').addEventListener('change', async (e) => {
            const file = e.target.files[0];
            if (file) {
                try {
                    await this.modelManager.importModel(file);
                    this.log('模型已导入', 'success');
                    this.showToast('模型导入成功', 'success');
                } catch (err) {
                    this.log(`导入失败: ${err.message}`, 'error');
                    this.showToast('导入失败', 'error');
                }
            }
        });
        
        document.getElementById('resetModel').addEventListener('click', () => {
            this.modelManager.reset();
            this.log('模型已重置为默认', 'success');
            this.showToast('模型已重置', 'success');
        });
    }

    setupLogControls() {
        document.getElementById('clearLog').addEventListener('click', () => {
            document.getElementById('logContainer').innerHTML = '';
            this.log('日志已清除');
        });
    }

    setupDaemonCallbacks() {
        // Status updates
        this.daemon.on('status_update', (msg) => {
            this.updateStatus(msg);
        });
        
        // Model weights
        this.daemon.on('model_weights', (msg) => {
            if (msg.weights) {
                this.modelManager.setWeights(msg.weights);
                this.log('模型权重已同步', 'info');
            }
        });
        
        // Command acknowledgment
        this.daemon.on('command_ack', (msg) => {
            if (msg.success) {
                console.log('[Cmd] Ack:', msg);
            } else {
                this.log(`命令失败: ${msg.error}`, 'error');
            }
        });
        
        // Pong response
        this.daemon.on('pong', (msg) => {
            const latency = Date.now() - msg.timestamp;
            document.getElementById('serverInfo').textContent = `延迟: ${latency}ms`;
        });
        
        // Error
        this.daemon.on('error', (msg) => {
            this.log(`错误: ${msg.message}`, 'error');
        });
    }

    updateStatus(status) {
        this.lastStatus = status;
        
        // Update FPS
        document.getElementById('fpsValue').textContent = status.fps || '--';
        document.getElementById('targetFps').textContent = `目标: ${status.target_fps || '--'}`;
        
        // Update CPU
        document.getElementById('cpuValue').textContent = `${status.cpu_util || 0}%`;
        document.getElementById('cpuBar').style.width = `${status.cpu_util || 0}%`;
        
        // Update Run Queue
        document.getElementById('rqValue').textContent = status.run_queue_len || '0';
        
        // Update Temperature
        document.getElementById('tempValue').textContent = `${status.temperature || 0}°C`;
        document.getElementById('thermalMargin').textContent = `热容: ${status.thermal_margin || 0}°C`;
        
        // Update Battery
        document.getElementById('batteryValue').textContent = `${status.battery_level || 0}%`;
        
        // Update Mode
        document.getElementById('modeValue').textContent = status.mode || 'N/A';
        
        // Update UCLAMP sliders
        if (status.uclamp_min !== undefined) {
            document.getElementById('uclampMin').value = status.uclamp_min;
            document.getElementById('uclampMinValue').textContent = `${status.uclamp_min}%`;
            this.uclampMin = status.uclamp_min;
        }
        if (status.uclamp_max !== undefined) {
            document.getElementById('uclampMax').value = status.uclamp_max;
            document.getElementById('uclampMaxValue').textContent = `${status.uclamp_max}%`;
            this.uclampMax = status.uclamp_max;
        }
        
        // Gaming mode styling
        if (status.is_gaming) {
            document.body.classList.add('gaming-mode');
        } else {
            document.body.classList.remove('gaming-mode');
        }
        
        // Predict next FPS using neural network
        if (status.fps > 0) {
            const features = [
                (status.cpu_util || 0) / 100,
                (status.run_queue_len || 0) / 10,
                (status.wakeups_100ms || 0) / 50,
                (status.frame_interval_us || 16667) / 50000,
                (status.touch_rate_100ms || 0) / 20,
                ((status.thermal_margin || 0) + 20) / 40,
                (status.battery_level || 50) / 100,
                (status.fps || 60) / 120
            ];
            
            const predictedFps = this.predictor.predict(features);
            // Use prediction for display or analytics
        }
    }

    sendCommand(cmd, params = {}) {
        this.daemon.send({
            type: 'command',
            cmd: cmd,
            params: params
        });
    }

    log(message, type = 'info') {
        const container = document.getElementById('logContainer');
        const entry = document.createElement('div');
        entry.className = `log-entry ${type}`;
        entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
        container.appendChild(entry);
        container.scrollTop = container.scrollHeight;
        
        // Keep last 100 entries
        while (container.children.length > 100) {
            container.removeChild(container.firstChild);
        }
    }

    showToast(message, type = 'info') {
        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.textContent = message;
        document.body.appendChild(toast);
        
        setTimeout(() => toast.classList.add('show'), 10);
        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 300);
        }, 2000);
    }

    getModeName(mode) {
        const names = {
            daily: '日常',
            game: '游戏',
            turbo: 'Turbo'
        };
        return names[mode] || mode;
    }

    getPresetName(preset) {
        const names = {
            performance: '性能优先',
            balanced: '均衡',
            battery: '省电'
        };
        return names[preset] || preset;
    }
}


// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new HyperPredictApp();
    window.app.init();
});
