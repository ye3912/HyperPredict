/**
 * HyperPredict ksuwebui - Main Application
 * Neural Network Predictor + Daemon Communication + 120Hz UI
 * 
 * Features:
 * - Dual predictor mode: Linear Regression + Neural Network (MLP)
 * - Daemon communication via WebSocket/HTTP
 * - Real-time model switching
 * - Weight persistence via localStorage
 */

// ============================================================
// Neural Network Predictor (Pure JS MLP)
// ============================================================
class NeuralNetworkPredictor {
    constructor(options = {}) {
        this.inputSize = options.inputSize || 7;  // 7 features
        this.hiddenSizes = options.hiddenSizes || [16, 8];  // 2 hidden layers
        this.outputSize = options.outputSize || 1;
        this.learningRate = options.learningRate || 0.01;
        
        this.weights = [];
        this.biases = [];
        this.activations = [];
        
        this.history = [];
        this.maxHistory = 100;
        
        this.initWeights();
    }
    
    initWeights() {
        // Xavier initialization
        const layerSizes = [this.inputSize, ...this.hiddenSizes, this.outputSize];
        
        for (let i = 0; i < layerSizes.length - 1; i++) {
            const rows = layerSizes[i + 1];
            const cols = layerSizes[i];
            const scale = Math.sqrt(2.0 / (cols + rows));
            
            const w = [];
            for (let r = 0; r < rows; r++) {
                const row = [];
                for (let c = 0; c < cols; c++) {
                    row.push((Math.random() - 0.5) * 2 * scale);
                }
                w.push(row);
            }
            this.weights.push(w);
            
            // Initialize biases to small values
            const b = new Array(rows).fill(0).map(() => (Math.random() - 0.5) * 0.1);
            this.biases.push(b);
        }
    }
    
    // Leaky ReLU activation
    activation(x, derivative = false) {
        const alpha = 0.01;
        if (derivative) {
            return x > 0 ? 1 : alpha;
        }
        return x > 0 ? x : alpha * x;
    }
    
    // Softmax for output layer
    softmax(arr) {
        const max = Math.max(...arr);
        const exp = arr.map(x => Math.exp(x - max));
        const sum = exp.reduce((a, b) => a + b, 0);
        return exp.map(x => x / sum);
    }
    
    // Forward propagation
    forward(input) {
        this.activations = [input];
        let current = input;
        
        for (let i = 0; i < this.weights.length; i++) {
            const w = this.weights[i];
            const b = this.biases[i];
            
            // Matrix multiplication: output = W * input + b
            const next = [];
            for (let r = 0; r < w.length; r++) {
                let sum = b[r];
                for (let c = 0; c < current.length; c++) {
                    sum += w[r][c] * current[c];
                }
                // Apply activation (linear for output layer)
                if (i === this.weights.length - 1) {
                    next.push(sum);  // Linear output for regression
                } else {
                    next.push(this.activation(sum));
                }
            }
            
            this.activations.push(next);
            current = next;
        }
        
        return current[0];
    }
    
    // Backpropagation
    backward(target, predicted) {
        const lr = this.learningRate;
        const numLayers = this.weights.length;
        
        // Output layer error
        let delta = [predicted - target];
        
        // Backpropagate through layers
        for (let i = numLayers - 1; i >= 0; i--) {
            const w = this.weights[i];
            const a = this.activations[i];
            
            // New delta for previous layer
            const newDelta = [];
            for (let c = 0; c < w[0].length; c++) {
                let sum = 0;
                for (let r = 0; r < w.length; r++) {
                    sum += w[r][c] * delta[r];
                }
                // Derivative of activation
                const input = a[c];
                newDelta.push(sum * this.activation(input, true));
            }
            
            // Update weights
            for (let r = 0; r < w.length; r++) {
                for (let c = 0; c < w[r].length; c++) {
                    w[r][c] -= lr * delta[r] * a[c];
                }
                this.biases[r] -= lr * delta[r];
            }
            
            delta = newDelta;
        }
    }
    
    // Train with one sample
    train(features, actualFps) {
        // Normalize features
        const input = this.normalizeFeatures(features);
        
        // Forward pass
        const predicted = this.forward(input);
        
        // Backward pass
        this.backward(actualFps, predicted);
        
        // Record history
        this.history.push({
            actual: actualFps,
            predicted: predicted,
            features: features,
            error: actualFps - predicted,
            timestamp: Date.now()
        });
        
        if (this.history.length > this.maxHistory) {
            this.history.shift();
        }
        
        return predicted;
    }
    
    // Predict without training
    predict(features) {
        const input = this.normalizeFeatures(features);
        return Math.max(0, Math.min(144, this.forward(input)));
    }
    
    // Normalize features to [0, 1]
    normalizeFeatures(features) {
        return [
            features.cpu_util / 1024,
            features.run_queue / 32,
            features.wakeups / 100,
            features.frame_interval / 20000,
            features.touch_rate / 20,
            (features.thermal_margin + 30) / 60,  // -30 to 30 range
            features.battery / 100,
            features.is_gaming ? 1 : 0
        ];
    }
    
    // Get accuracy (within 5 fps tolerance)
    getAccuracy() {
        if (this.history.length < 10) return 0;
        const recent = this.history.slice(-20);
        const correct = recent.filter(h => Math.abs(h.actual - h.predicted) <= 5).length;
        return (correct / recent.length) * 100;
    }
    
    // Get mean absolute error
    getMAE() {
        if (this.history.length < 5) return 0;
        const recent = this.history.slice(-20);
        const totalError = recent.reduce((sum, h) => sum + Math.abs(h.error), 0);
        return totalError / recent.length;
    }
    
    // Export model as JSON
    exportModel() {
        return {
            weights: this.weights,
            biases: this.biases,
            config: {
                inputSize: this.inputSize,
                hiddenSizes: this.hiddenSizes,
                outputSize: this.outputSize,
                learningRate: this.learningRate
            }
        };
    }
    
    // Import model from JSON
    importModel(model) {
        this.weights = model.weights;
        this.biases = model.biases;
        this.inputSize = model.config.inputSize;
        this.hiddenSizes = model.config.hiddenSizes;
        this.outputSize = model.config.outputSize;
        this.learningRate = model.config.learningRate;
    }
    
    // Reset to random weights
    reset() {
        this.weights = [];
        this.biases = [];
        this.activations = [];
        this.history = [];
        this.initWeights();
    }
}

// ============================================================
// Linear Regression Predictor (Original, for comparison)
// ============================================================
class LinearPredictor {
    constructor() {
        this.weights = { w_util: 0, w_rq: 0, w_wakeups: 0, w_frame: 0, w_touch: 0, w_thermal: 0, w_battery: 0, bias: 0 };
        this.last_util = 0;
        this.ema_error = 0;
        this.learning_rate = 0.05;
        this.history = [];
        this.maxHistory = 100;
    }
    
    train(features, actualFps) {
        const calc_fps = features.frame_interval > 0 ? 1000000 / features.frame_interval : 60;
        const error = actualFps - calc_fps;
        
        const util = features.cpu_util / 1024;
        const rq = features.run_queue / 32;
        const wakeups = features.wakeups / 100;
        const frame = features.frame_interval / 20000;
        const touch = features.touch_rate / 20;
        const thermal = (features.thermal_margin + 30) / 60;
        const battery = features.battery / 100;
        
        this.weights.w_util += this.learning_rate * error * util;
        this.weights.w_rq += this.learning_rate * error * rq;
        this.weights.w_wakeups += this.learning_rate * error * wakeups;
        this.weights.w_frame += this.learning_rate * error * frame;
        this.weights.w_touch += this.learning_rate * error * touch;
        this.weights.w_thermal += this.learning_rate * error * thermal;
        this.weights.w_battery += this.learning_rate * error * battery;
        this.weights.bias += this.learning_rate * error;
        
        // Clamp weights
        Object.keys(this.weights).forEach(k => {
            if (k === 'bias') {
                this.weights[k] = Math.max(-50, Math.min(50, this.weights[k]));
            } else {
                this.weights[k] = Math.max(-2, Math.min(2, this.weights[k]));
            }
        });
        
        this.ema_error = this.ema_error * 0.9 + error * 0.1;
        this.last_util = util;
        
        this.history.push({ actual: actualFps, predicted: calc_fps, error, timestamp: Date.now() });
        if (this.history.length > this.maxHistory) this.history.shift();
        
        return calc_fps;
    }
    
    predict(features) {
        const util = features.cpu_util / 1024;
        const rq = features.run_queue / 32;
        const wakeups = features.wakeups / 100;
        const frame = features.frame_interval / 20000;
        const touch = features.touch_rate / 20;
        const thermal = (features.thermal_margin + 30) / 60;
        const battery = features.battery / 100;
        
        let pred = this.weights.w_util * util +
                   this.weights.w_rq * rq +
                   this.weights.w_wakeups * wakeups +
                   this.weights.w_frame * frame +
                   this.weights.w_touch * touch +
                   this.weights.w_thermal * thermal +
                   this.weights.w_battery * battery +
                   this.weights.bias;
        
        const trend = (util - this.last_util) * 10;
        pred += trend * 0.5;
        
        return Math.max(0, Math.min(144, pred));
    }
    
    getAccuracy() {
        if (this.history.length < 10) return 0;
        const recent = this.history.slice(-20);
        const correct = recent.filter(h => Math.abs(h.actual - h.predicted) <= 5).length;
        return (correct / recent.length) * 100;
    }
    
    getMAE() {
        if (this.history.length < 5) return 0;
        const recent = this.history.slice(-20);
        const totalError = recent.reduce((sum, h) => sum + Math.abs(h.error), 0);
        return totalError / recent.length;
    }
    
    exportModel() {
        return { weights: this.weights, ema_error: this.ema_error, learning_rate: this.learning_rate };
    }
    
    importModel(model) {
        this.weights = model.weights;
        this.ema_error = model.ema_error;
        this.learning_rate = model.learning_rate;
    }
    
    reset() {
        this.weights = { w_util: 0, w_rq: 0, w_wakeups: 0, w_frame: 0, w_touch: 0, w_thermal: 0, w_battery: 0, bias: 0 };
        this.ema_error = 0;
        this.history = [];
    }
}

// ============================================================
// Daemon Communication Layer
// ============================================================
class DaemonConnector {
    constructor(options = {}) {
        this.apiBase = options.apiBase || 'http://localhost:8081/api';
        this.wsUrl = options.wsUrl || 'ws://localhost:8081/ws';
        this.pollInterval = options.pollInterval || 2000;
        
        this.connected = false;
        this.websocket = null;
        this.pollTimer = null;
        this.callbacks = {
            onData: null,
            onStatus: null,
            onError: null,
            onConnect: null,
            onDisconnect: null
        };
        
        this.lastData = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
    }
    
    // Register callbacks
    on(event, callback) {
        if (this.callbacks.hasOwnProperty(event)) {
            this.callbacks[event] = callback;
        }
        return this;
    }
    
    // Connect via WebSocket (preferred)
    connectWS() {
        try {
            this.websocket = new WebSocket(this.wsUrl);
            
            this.websocket.onopen = () => {
                this.connected = true;
                this.reconnectAttempts = 0;
                this.callbacks.onConnect?.();
                this.callbacks.onStatus?.('connected', 'WebSocket');
            };
            
            this.websocket.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.handleMessage(data);
                } catch (e) {
                    console.error('Failed to parse WS message:', e);
                }
            };
            
            this.websocket.onclose = () => {
                this.connected = false;
                this.callbacks.onDisconnect?.();
                this.callbacks.onStatus?.('disconnected', 'WebSocket closed');
                this.scheduleReconnect();
            };
            
            this.websocket.onerror = (error) => {
                this.callbacks.onError?.(error);
                this.callbacks.onStatus?.('error', 'WebSocket error');
            };
            
        } catch (e) {
            console.error('WebSocket connection failed:', e);
            this.callbacks.onStatus?.('error', 'WebSocket init failed');
            this.connectFallback();
        }
    }
    
    // HTTP polling fallback
    connectPoll() {
        this.startPolling();
        this.connected = true;
        this.callbacks.onConnect?.();
        this.callbacks.onStatus?.('connected', 'HTTP Poll');
    }
    
    // Fallback to HTTP polling
    connectFallback() {
        console.log('Falling back to HTTP polling');
        this.connectPoll();
    }
    
    startPolling() {
        if (this.pollTimer) clearInterval(this.pollTimer);
        
        this.pollTimer = setInterval(async () => {
            if (!this.connected) return;
            
            try {
                const data = await this.httpGet('/status');
                this.handleMessage(data);
            } catch (e) {
                this.callbacks.onError?.(e);
            }
        }, this.pollInterval);
        
        // Initial fetch
        this.fetchStatus();
    }
    
    async fetchStatus() {
        try {
            const data = await this.httpGet('/status');
            this.handleMessage(data);
        } catch (e) {
            this.callbacks.onError?.(e);
        }
    }
    
    async httpGet(endpoint) {
        const response = await fetch(this.apiBase + endpoint, {
            method: 'GET',
            headers: { 'Accept': 'application/json' },
            signal: AbortSignal.timeout(5000)
        });
        
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json();
    }
    
    async httpPost(endpoint, data) {
        const response = await fetch(this.apiBase + endpoint, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
            body: JSON.stringify(data),
            signal: AbortSignal.timeout(5000)
        });
        
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json();
    }
    
    handleMessage(data) {
        this.lastData = data;
        this.callbacks.onData?.(this.normalizeData(data));
    }
    
    // Normalize daemon data to internal format
    normalizeData(data) {
        return {
            fps: data.fps || data.current_fps || 60,
            target_fps: data.target_fps || 60,
            cpu_util: data.cpu_util || 512,
            run_queue: data.run_queue_len || data.run_queue || 1,
            wakeups: data.wakeups_100ms || 0,
            frame_interval: data.frame_interval_us || 16667,
            touch_rate: data.touch_rate_100ms || 0,
            thermal_margin: data.thermal_margin || 20,
            battery: data.battery_level || 100,
            is_gaming: data.is_gaming || false,
            mode: data.mode || 'daily',
            uclamp_min: data.uclamp_min || 50,
            uclamp_max: data.uclamp_max || 100,
            clusters: data.clusters || {
                little: { freq: 400, usage: 30 },
                mid: { freq: 800, usage: 25 },
                big: { freq: 1800, usage: 15 }
            },
            temp: data.temperature || data.temp || 42,
            timestamp: data.timestamp || Date.now()
        };
    }
    
    // Send command to daemon
    async sendCommand(cmd, params = {}) {
        try {
            return await this.httpPost('/command', { cmd, ...params });
        } catch (e) {
            console.error('Command failed:', e);
            throw e;
        }
    }
    
    // Set scheduling mode
    async setMode(mode) {
        return this.sendCommand('set_mode', { mode });
    }
    
    // Set uclamp values
    async setUclamp(min, max) {
        return this.sendCommand('set_uclamp', { min, max });
    }
    
    // Set thermal preset
    async setThermalPreset(preset) {
        return this.sendCommand('set_thermal', { preset });
    }
    
    // Get model weights from daemon
    async getModelWeights() {
        return this.httpGet('/model');
    }
    
    // Push model weights to daemon
    async pushModelWeights(weights) {
        return this.sendCommand('set_model', weights);
    }
    
    scheduleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            this.callbacks.onStatus?.('failed', 'Max reconnect attempts reached');
            return;
        }
        
        this.reconnectAttempts++;
        const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 30000);
        
        this.callbacks.onStatus?.('reconnecting', `Attempt ${this.reconnectAttempts}`);
        
        setTimeout(() => {
            if (!this.connected) {
                this.connectWS();
            }
        }, delay);
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
// Model Manager
// ============================================================
class ModelManager {
    constructor() {
        this.storageKey = 'hyperpredict_models';
        this.linearModel = new LinearPredictor();
        this.neuralModel = new NeuralNetworkPredictor({
            inputSize: 8,
            hiddenSizes: [16, 8],
            outputSize: 1,
            learningRate: 0.005
        });
        this.currentModel = 'neural';  // Default to neural
        this.autoSync = true;
        
        this.loadFromStorage();
    }
    
    get current() {
        return this.currentModel === 'neural' ? this.neuralModel : this.linearModel;
    }
    
    switchModel(modelName) {
        if (modelName === 'neural' || modelName === 'linear') {
            this.currentModel = modelName;
            this.saveToStorage();
            return true;
        }
        return false;
    }
    
    train(features, actualFps) {
        // Train both models
        const linearPred = this.linearModel.train(features, actualFps);
        const neuralPred = this.neuralModel.train(features, actualFps);
        
        // Auto-sync to daemon if enabled
        if (this.autoSync) {
            this.syncToDaemon();
        }
        
        return {
            linear: linearPred,
            neural: neuralPred,
            actual: actualFps,
            linearError: Math.abs(actualFps - linearPred),
            neuralError: Math.abs(actualFps - neuralPred)
        };
    }
    
    predict(features) {
        return {
            value: this.current.predict(features),
            model: this.currentModel,
            confidence: this.current.getAccuracy()
        };
    }
    
    getStats() {
        return {
            linear: {
                accuracy: this.linearModel.getAccuracy(),
                mae: this.linearModel.getMAE()
            },
            neural: {
                accuracy: this.neuralModel.getAccuracy(),
                mae: this.neuralModel.getMAE()
            },
            current: this.currentModel
        };
    }
    
    saveToStorage() {
        try {
            const data = {
                currentModel: this.currentModel,
                linearWeights: this.linearModel.exportModel(),
                neuralWeights: this.neuralModel.exportModel()
            };
            localStorage.setItem(this.storageKey, JSON.stringify(data));
        } catch (e) {
            console.error('Failed to save models:', e);
        }
    }
    
    loadFromStorage() {
        try {
            const stored = localStorage.getItem(this.storageKey);
            if (stored) {
                const data = JSON.parse(stored);
                this.currentModel = data.currentModel || 'neural';
                if (data.linearWeights) this.linearModel.importModel(data.linearWeights);
                if (data.neuralWeights) this.neuralModel.importModel(data.neuralWeights);
            }
        } catch (e) {
            console.error('Failed to load models:', e);
        }
    }
    
    resetModels() {
        this.linearModel.reset();
        this.neuralModel.reset();
        this.saveToStorage();
    }
    
    async syncToDaemon() {
        // This would sync weights to the daemon
        // Implementation depends on daemon API
    }
}

// ============================================================
// Main Application
// ============================================================
class HyperPredictApp {
    constructor() {
        // Initialize components
        this.modelManager = new ModelManager();
        this.daemon = new DaemonConnector({
            apiBase: 'http://localhost:8081/api',
            wsUrl: 'ws://localhost:8081/ws',
            pollInterval: 2000
        });
        
        // Application state
        this.state = {
            connected: false,
            connectionType: 'disconnected',
            mode: 'daily',
            targetFps: 60,
            dualCell: false,
            thermalPreset: 'balanced',
            currentModel: 'neural',
            
            // Real-time data
            fps: 60,
            temp: 42,
            battery: 85,
            cpu_util: 512,
            run_queue: 1,
            wakeups: 10,
            frame_interval: 16667,
            touch_rate: 0,
            thermal_margin: 20,
            is_gaming: false,
            
            // Smoothed display values
            display: {
                fps: 60,
                temp: 42,
                battery: 85
            },
            
            // Cluster data
            clusters: {
                little: { freq: 400, usage: 30 },
                mid: { freq: 800, usage: 25 },
                big: { freq: 1800, usage: 15 }
            }
        };
        
        // Target values for smooth animation
        this.target = { ...this.state };
        
        // UI data
        this.logs = [];
        this.maxLogs = 20;
        this.thermalData = [];
        this.maxThermalData = 30;
        this.predictorData = [];
        this.maxPredictorData = 50;
        
        // Training history
        this.trainingLog = [];
        
        // Canvas references
        this.thermalCanvas = null;
        this.thermalCtx = null;
        this.predCanvas = null;
        this.predCtx = null;
        
        this.init();
    }
    
    init() {
        this.setupDaemonCallbacks();
        this.setupEventListeners();
        this.initCanvases();
        this.initDataHistory();
        this.startUIRender();
        this.connectDaemon();
        this.addLog('HyperPredict 初始化完成', 'info');
    }
    
    setupDaemonCallbacks() {
        this.daemon
            .on('onConnect', () => {
                this.state.connected = true;
                this.addLog('守护进程已连接', 'success');
            })
            .on('onDisconnect', () => {
                this.state.connected = false;
                this.addLog('守护进程已断开', 'warning');
            })
            .on('onStatus', (status, detail) => {
                this.state.connectionType = status;
                this.updateConnectionUI(status, detail);
            })
            .on('onData', (data) => {
                this.processDaemonData(data);
            })
            .on('onError', (error) => {
                this.addLog(`错误: ${error.message || error}`, 'error');
            });
    }
    
    connectDaemon() {
        // Try WebSocket first, fallback to HTTP polling
        this.daemon.connectWS();
        
        // Also start simulation mode for demo
        this.startSimulationMode();
    }
    
    startSimulationMode() {
        // Simulation interval (2s matching daemon poll)
        this.simTimer = setInterval(() => {
            this.simulateData();
        }, 2000);
        
        // Initial simulation
        this.simulateData();
    }
    
    simulateData() {
        // Generate realistic simulation data
        const baseFps = this.state.mode === 'turbo' ? 120 : 
                        this.state.mode === 'game' ? 90 : 60;
        
        // Add realistic variance
        const variance = baseFps * 0.15;
        const actualFps = baseFps + (Math.random() - 0.5) * variance;
        this.target.fps = Math.round(Math.max(24, Math.min(144, actualFps)));
        
        // CPU load correlates with FPS
        const loadFactor = this.target.fps / 120;
        this.target.cpu_util = Math.round(150 + loadFactor * 700 + Math.random() * 150);
        this.target.cpu_util = Math.min(1024, this.target.cpu_util);
        
        // Run queue inversely correlates with performance
        this.target.run_queue = Math.round(1 + (1 - loadFactor) * 4 + Math.random() * 2);
        this.target.run_queue = Math.min(32, this.target.run_queue);
        
        // Wakeups per 100ms
        this.target.wakeups = Math.round(5 + loadFactor * 50 + Math.random() * 20);
        
        // Frame interval
        this.target.frame_interval = 1000000 / this.target.fps;
        
        // Touch rate (random spikes)
        this.target.touch_rate = Math.random() > 0.9 ? Math.round(Math.random() * 15) : 0;
        
        // Thermal margin (higher = cooler)
        this.target.thermal_margin = Math.round(10 + (1 - this.target.cpu_util / 1024) * 30 + Math.random() * 10);
        
        // Temperature
        let temp = 35 + (this.target.cpu_util / 1024) * 25 + Math.random() * 5;
        if (this.state.dualCell) temp = Math.max(30, temp - 5);
        this.target.temp = Math.round(temp);
        
        // Battery drain
        const drain = this.state.dualCell ? 0.015 : 0.03;
        this.target.battery = Math.max(0, this.state.battery - drain);
        
        // Gaming mode detection
        this.target.is_gaming = this.state.mode === 'game' || this.state.mode === 'turbo' || this.target.touch_rate > 5;
        
        // Cluster frequencies
        ['little', 'mid', 'big'].forEach(cluster => {
            const baseFreq = cluster === 'big' ? 1800 : cluster === 'mid' ? 1000 : 500;
            this.target.clusters[cluster].freq = Math.round(
                baseFreq + loadFactor * 500 + Math.random() * 300
            );
            this.target.clusters[cluster].usage = Math.round(
                15 + loadFactor * 70 + Math.random() * 15
            );
        });
        
        // Process with ML models
        this.processWithModels();
    }
    
    processWithModels() {
        const features = {
            cpu_util: this.target.cpu_util,
            run_queue: this.target.run_queue,
            wakeups: this.target.wakeups,
            frame_interval: this.target.frame_interval,
            touch_rate: this.target.touch_rate,
            thermal_margin: this.target.thermal_margin,
            battery: this.target.battery,
            is_gaming: this.target.is_gaming
        };
        
        // Train models with actual FPS
        const result = this.modelManager.train(features, this.target.fps);
        
        // Get prediction
        const prediction = this.modelManager.predict(features);
        
        // Record for visualization
        this.recordTrainingData(result, prediction);
        
        // Update stats display
        this.updateModelStats();
        
        // Log if significant error
        const error = Math.abs(this.target.fps - prediction.value);
        if (error > 10) {
            this.addLog(`FPS ${this.target.fps} | ${prediction.model}预测 ${Math.round(prediction.value)} | 误差 ${error.toFixed(1)}`, 
                error > 20 ? 'warning' : 'info');
        }
    }
    
    recordTrainingData(result, prediction) {
        this.predictorData.push({
            actual: result.actual,
            linearPred: result.linear,
            neuralPred: result.neural,
            currentPred: prediction.value,
            model: prediction.model,
            timestamp: Date.now()
        });
        
        if (this.predictorData.length > this.maxPredictorData) {
            this.predictorData.shift();
        }
        
        // Thermal data
        this.thermalData.push(this.target.temp);
        if (this.thermalData.length > this.maxThermalData) {
            this.thermalData.shift();
        }
    }
    
    processDaemonData(data) {
        // Update targets from daemon
        Object.assign(this.target, {
            fps: data.fps,
            cpu_util: data.cpu_util,
            run_queue: data.run_queue,
            wakeups: data.wakeups || 10,
            frame_interval: data.frame_interval,
            touch_rate: data.touch_rate || 0,
            thermal_margin: data.thermal_margin,
            battery: data.battery,
            is_gaming: data.is_gaming,
            temp: data.temp
        });
        
        if (data.clusters) {
            Object.assign(this.target.clusters, data.clusters);
        }
        
        // Process with ML models
        this.processWithModels();
    }
    
    updateModelStats() {
        const stats = this.modelManager.getStats();
        
        const linearAcc = document.getElementById('linear-accuracy');
        const neuralAcc = document.getElementById('neural-accuracy');
        const linearMae = document.getElementById('linear-mae');
        const neuralMae = document.getElementById('neural-mae');
        const currentModel = document.getElementById('current-model');
        
        if (linearAcc) linearAcc.textContent = stats.linear.accuracy.toFixed(1) + '%';
        if (neuralAcc) neuralAcc.textContent = stats.neural.accuracy.toFixed(1) + '%';
        if (linearMae) linearMae.textContent = stats.linear.mae.toFixed(2);
        if (neuralMae) neuralMae.textContent = stats.neural.mae.toFixed(2);
        if (currentModel) currentModel.textContent = stats.current === 'neural' ? '神经网络' : '线性回归';
        
        // Update model selector
        document.querySelectorAll('.model-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.model === stats.current);
        });
    }
    
    updateConnectionUI(status, detail) {
        const statusDot = document.getElementById('conn-status');
        const statusText = document.getElementById('conn-text');
        const connType = document.getElementById('conn-type');
        
        if (statusDot) {
            statusDot.className = 'status-dot';
            if (status === 'connected') statusDot.classList.add('connected');
            else if (status === 'error' || status === 'failed') statusDot.classList.add('error');
            else if (status === 'reconnecting') statusDot.classList.add('reconnecting');
        }
        
        if (statusText) {
            const labels = {
                'connected': '已连接',
                'disconnected': '未连接',
                'error': '连接错误',
                'reconnecting': '重新连接中',
                'failed': '连接失败'
            };
            statusText.textContent = labels[status] || status;
        }
        
        if (connType) {
            connType.textContent = detail || '';
        }
    }
    
    setupEventListeners() {
        // Navigation
        document.querySelectorAll('.nav-item').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.nav-item').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            });
        });
        
        // Mode buttons
        document.querySelectorAll('.segment-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.segment-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                this.state.mode = btn.dataset.mode;
                this.applyMode();
            });
        });
        
        // Model switch buttons
        document.querySelectorAll('.model-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                const model = btn.dataset.model;
                if (this.modelManager.switchModel(model)) {
                    this.state.currentModel = model;
                    document.querySelectorAll('.model-btn').forEach(b => b.classList.remove('active'));
                    btn.classList.add('active');
                    this.addLog(`切换到${model === 'neural' ? '神经网络' : '线性回归'}模型`, 'info');
                }
            });
        });
        
        // Thermal presets
        document.querySelectorAll('.preset-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                this.state.thermalPreset = btn.dataset.preset;
                this.applyThermalPreset();
            });
        });
        
        // Dual cell toggle
        const dualCellToggle = document.getElementById('dual-cell-toggle');
        if (dualCellToggle) {
            dualCellToggle.addEventListener('change', (e) => {
                this.state.dualCell = e.target.checked;
                this.addLog(`双电芯: ${this.state.dualCell ? '启用' : '禁用'}`, 'info');
            });
        }
        
        // Sliders
        this.setupSlider('uclamp-min-slider', 'uclamp-min-value', (val) => `${val}%`);
        this.setupSlider('uclamp-max-slider', 'uclamp-max-value', (val) => `${val}%`);
        
        // Clear log
        const clearLogBtn = document.getElementById('clear-log');
        if (clearLogBtn) {
            clearLogBtn.addEventListener('click', () => {
                this.logs = [];
                this.updateLogUI();
                this.addLog('日志已清空', 'info');
            });
        }
        
        // Reset models
        const resetBtn = document.getElementById('reset-models');
        if (resetBtn) {
            resetBtn.addEventListener('click', () => {
                this.modelManager.resetModels();
                this.addLog('模型已重置', 'info');
                this.updateModelStats();
            });
        }
        
        // Export/Import models
        const exportBtn = document.getElementById('export-model');
        const importBtn = document.getElementById('import-model');
        
        if (exportBtn) {
            exportBtn.addEventListener('click', () => this.exportModels());
        }
        
        if (importBtn) {
            const fileInput = document.getElementById('import-file');
            if (fileInput) {
                fileInput.addEventListener('change', (e) => this.importModels(e));
            }
        }
    }
    
    setupSlider(sliderId, valueId, formatter) {
        const slider = document.getElementById(sliderId);
        const valueEl = document.getElementById(valueId);
        if (!slider || !valueEl) return;
        
        slider.addEventListener('input', (e) => {
            valueEl.textContent = formatter(e.target.value);
        });
        
        slider.addEventListener('change', async (e) => {
            const min = document.getElementById('uclamp-min-slider')?.value || 50;
            const max = document.getElementById('uclamp-max-slider')?.value || 100;
            try {
                await this.daemon.setUclamp(min, max);
                this.addLog(`uclamp: ${min}% - ${max}%`, 'success');
            } catch (e) {
                this.addLog('uclamp 设置失败', 'error');
            }
        });
    }
    
    initCanvases() {
        // Thermal canvas
        const thermalCanvas = document.getElementById('thermal-canvas');
        if (thermalCanvas) {
            this.thermalCanvas = thermalCanvas;
            this.thermalCtx = canvas.getContext('2d');
            this.setupCanvas(thermalCanvas, (ctx) => { this.thermalCtx = ctx; });
        }
        
        // Predictor canvas
        const predCanvas = document.getElementById('predictor-canvas');
        if (predCanvas) {
            this.predCanvas = predCanvas;
            this.setupCanvas(predCanvas, (ctx) => { this.predCtx = ctx; });
        }
    }
    
    setupCanvas(canvas, onContext) {
        const resize = () => {
            const rect = canvas.getBoundingClientRect();
            canvas.width = rect.width * 2;
            canvas.height = rect.height * 2;
            const ctx = canvas.getContext('2d');
            ctx.scale(2, 2);
            this[`${canvas.id.replace('-canvas', '')}Width`] = rect.width;
            this[`${canvas.id.replace('-canvas', '')}Height`] = rect.height;
            onContext(ctx);
        };
        
        resize();
        window.addEventListener('resize', resize);
    }
    
    initDataHistory() {
        for (let i = 0; i < this.maxThermalData; i++) {
            this.thermalData.push(42);
        }
        for (let i = 0; i < this.maxPredictorData; i++) {
            this.predictorData.push({ actual: 60, linearPred: 60, neuralPred: 60, currentPred: 60 });
        }
    }
    
    applyMode() {
        const modes = { daily: 60, game: 90, turbo: 120 };
        this.state.targetFps = modes[this.state.mode] || 60;
        this.addLog(`调度模式: ${this.state.mode}`, 'success');
        
        // Try to sync with daemon
        if (this.state.connected) {
            this.daemon.setMode(this.state.mode).catch(() => {});
        }
    }
    
    applyThermalPreset() {
        this.addLog(`温控预设: ${this.state.thermalPreset}`, 'info');
        
        if (this.state.connected) {
            this.daemon.setThermalPreset(this.state.thermalPreset).catch(() => {});
        }
    }
    
    startUIRender() {
        const loop = () => {
            this.renderUI();
            this.drawCharts();
            requestAnimationFrame(loop);
        };
        requestAnimationFrame(loop);
    }
    
    renderUI() {
        const lerp = (current, target, factor) => current + (target - current) * factor;
        const smoothFactor = 0.12;
        
        // Smooth display values
        this.state.display.fps = lerp(this.state.display.fps, this.target.fps, smoothFactor);
        this.state.display.temp = lerp(this.state.display.temp, this.target.temp, smoothFactor);
        this.state.display.battery = lerp(this.state.display.battery, this.target.battery, smoothFactor * 0.5);
        
        // Smooth cluster values
        ['little', 'mid', 'big'].forEach(cluster => {
            this.state.clusters[cluster].freq = lerp(
                this.state.clusters[cluster].freq,
                this.target.clusters[cluster].freq,
                smoothFactor
            );
            this.state.clusters[cluster].usage = lerp(
                this.state.clusters[cluster].usage,
                this.target.clusters[cluster].usage,
                smoothFactor
            );
        });
        
        // Update DOM
        this.updateDOMValues();
    }
    
    updateDOMValues() {
        // FPS
        const fpsEl = document.getElementById('fps-value');
        if (fpsEl) fpsEl.textContent = Math.round(this.state.display.fps);
        
        // FPS status color
        const fpsStatus = document.getElementById('fps-status');
        if (fpsStatus) {
            const fps = this.state.display.fps;
            if (fps >= 90) {
                fpsStatus.textContent = '流畅';
                fpsStatus.style.color = '#34A853';
            } else if (fps >= 60) {
                fpsStatus.textContent = '良好';
                fpsStatus.style.color = '#4285F4';
            } else if (fps >= 30) {
                fpsStatus.textContent = '一般';
                fpsStatus.style.color = '#FBBC04';
            } else {
                fpsStatus.textContent = '卡顿';
                fpsStatus.style.color = '#EA4335';
            }
        }
        
        // Temperature
        const tempEl = document.getElementById('temp-value');
        if (tempEl) {
            tempEl.textContent = Math.round(this.state.display.temp);
            const temp = this.state.display.temp;
            if (temp < 38) tempEl.style.color = '#34A853';
            else if (temp < 45) tempEl.style.color = '#4285F4';
            else if (temp < 50) tempEl.style.color = '#FBBC04';
            else tempEl.style.color = '#EA4335';
        }
        
        // Battery
        const batteryEl = document.getElementById('battery-value');
        if (batteryEl) batteryEl.textContent = Math.round(this.state.display.battery);
        
        // Predicted FPS
        const prediction = this.modelManager.predict({
            cpu_util: this.target.cpu_util,
            run_queue: this.target.run_queue,
            wakeups: this.target.wakeups,
            frame_interval: this.target.frame_interval,
            touch_rate: this.target.touch_rate,
            thermal_margin: this.target.thermal_margin,
            battery: this.target.battery,
            is_gaming: this.target.is_gaming
        });
        
        const predictedEl = document.getElementById('predicted-fps');
        const actualEl = document.getElementById('actual-fps');
        const confidenceEl = document.getElementById('confidence');
        
        if (predictedEl) predictedEl.textContent = Math.round(prediction.value);
        if (actualEl) actualEl.textContent = Math.round(this.state.display.fps);
        if (confidenceEl) confidenceEl.textContent = prediction.confidence.toFixed(1) + '%';
        
        // CPU info
        const cpuUtilEl = document.getElementById('cpu-util');
        const rqLenEl = document.getElementById('rq-length');
        if (cpuUtilEl) cpuUtilEl.textContent = Math.round(this.target.cpu_util);
        if (rqLenEl) rqLenEl.textContent = this.target.run_queue;
        
        // Cluster info
        ['little', 'mid', 'big'].forEach(cluster => {
            const freqEl = document.getElementById(`freq-${cluster}`);
            const usageEl = document.getElementById(`usage-${cluster}`);
            const freqBar = document.getElementById(`freq-bar-${cluster}`);
            
            if (freqEl) freqEl.textContent = Math.round(this.state.clusters[cluster].freq);
            if (usageEl) usageEl.style.width = `${this.state.clusters[cluster].usage}%`;
            if (freqBar) freqBar.style.width = `${(this.state.clusters[cluster].freq / 2800) * 100}%`;
        });
    }
    
    drawCharts() {
        this.drawThermalChart();
        this.drawPredictorChart();
    }
    
    drawThermalChart() {
        if (!this.thermalCtx) return;
        
        const ctx = this.thermalCtx;
        const width = this.thermalWidth || 300;
        const height = this.thermalHeight || 120;
        
        ctx.clearRect(0, 0, width, height);
        
        if (this.thermalData.length < 2) return;
        
        const step = width / (this.thermalData.length - 1);
        const maxTemp = 70;
        
        // Background gradient
        const bgGrad = ctx.createLinearGradient(0, 0, 0, height);
        bgGrad.addColorStop(0, 'rgba(66, 133, 244, 0.05)');
        bgGrad.addColorStop(1, 'rgba(66, 133, 244, 0)');
        ctx.fillStyle = bgGrad;
        ctx.fillRect(0, 0, width, height);
        
        // Draw filled area
        ctx.beginPath();
        ctx.moveTo(0, height);
        
        this.thermalData.forEach((temp, i) => {
            const x = i * step;
            const y = height - (temp / maxTemp) * height;
            ctx.lineTo(x, y);
        });
        
        ctx.lineTo(width, height);
        ctx.closePath();
        
        // Gradient fill
        const gradient = ctx.createLinearGradient(0, 0, 0, height);
        gradient.addColorStop(0, 'rgba(234, 67, 53, 0.4)');
        gradient.addColorStop(0.5, 'rgba(251, 188, 4, 0.3)');
        gradient.addColorStop(1, 'rgba(52, 168, 83, 0.2)');
        ctx.fillStyle = gradient;
        ctx.fill();
        
        // Draw line
        ctx.beginPath();
        ctx.strokeStyle = '#EA4335';
        ctx.lineWidth = 2;
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        
        this.thermalData.forEach((temp, i) => {
            const x = i * step;
            const y = height - (temp / maxTemp) * height;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
        
        ctx.stroke();
        
        // Draw current point
        const lastX = (this.thermalData.length - 1) * step;
        const lastY = height - (this.thermalData[this.thermalData.length - 1] / maxTemp) * height;
        
        ctx.beginPath();
        ctx.arc(lastX, lastY, 4, 0, Math.PI * 2);
        ctx.fillStyle = '#EA4335';
        ctx.fill();
        
        // Glow effect
        ctx.beginPath();
        ctx.arc(lastX, lastY, 6, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(234, 67, 53, 0.3)';
        ctx.fill();
        
        // Temperature labels
        ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
        ctx.font = '10px sans-serif';
        ctx.fillText('70°', 2, 12);
        ctx.fillText('35°', 2, height - 4);
    }
    
    drawPredictorChart() {
        if (!this.predCtx) return;
        
        const ctx = this.predCtx;
        const width = this.predWidth || 300;
        const height = this.predHeight || 150;
        
        ctx.clearRect(0, 0, width, height);
        
        const data = this.predictorData;
        if (data.length < 2) return;
        
        const maxFps = 144;
        const step = width / (data.length - 1);
        
        // Background
        ctx.fillStyle = 'rgba(66, 133, 244, 0.03)';
        ctx.fillRect(0, 0, width, height);
        
        // Grid lines
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
        ctx.lineWidth = 0.5;
        for (let i = 0; i <= 4; i++) {
            const y = (height / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }
        
        // Target FPS line
        const targetY = height - (this.state.targetFps / maxFps) * height;
        ctx.beginPath();
        ctx.strokeStyle = 'rgba(251, 188, 4, 0.5)';
        ctx.lineWidth = 1;
        ctx.setLineDash([4, 4]);
        ctx.moveTo(0, targetY);
        ctx.lineTo(width, targetY);
        ctx.stroke();
        ctx.setLineDash([]);
        
        // Actual FPS line (solid green)
        ctx.beginPath();
        ctx.strokeStyle = '#34A853';
        ctx.lineWidth = 2;
        
        data.forEach((item, i) => {
            const x = i * step;
            const y = height - (item.actual / maxFps) * height;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
        ctx.stroke();
        
        // Neural prediction line (dashed blue)
        ctx.beginPath();
        ctx.strokeStyle = '#4285F4';
        ctx.lineWidth = 2;
        ctx.setLineDash([6, 3]);
        
        data.forEach((item, i) => {
            const x = i * step;
            const y = height - (item.neuralPred / maxFps) * height;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
        ctx.stroke();
        ctx.setLineDash([]);
        
        // Linear prediction line (dotted purple)
        ctx.beginPath();
        ctx.strokeStyle = '#9C27B0';
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);
        
        data.forEach((item, i) => {
            const x = i * step;
            const y = height - (item.linearPred / maxFps) * height;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
        ctx.stroke();
        ctx.setLineDash([]);
        
        // Legend
        ctx.font = '10px sans-serif';
        ctx.fillStyle = '#34A853';
        ctx.fillText('● 实际', 4, 12);
        ctx.fillStyle = '#4285F4';
        ctx.fillText('● 神经网络', 50, 12);
        ctx.fillStyle = '#9C27B0';
        ctx.fillText('● 线性', 115, 12);
        
        // FPS labels
        ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
        ctx.fillText('144', 2, 10);
        ctx.fillText('72', 2, height / 2 + 4);
        ctx.fillText('0', 2, height - 2);
    }
    
    addLog(message, type = 'info') {
        const now = new Date();
        const time = now.toTimeString().slice(0, 8);
        
        this.logs.unshift({ time, message, type });
        if (this.logs.length > this.maxLogs) this.logs.pop();
        this.updateLogUI();
    }
    
    updateLogUI() {
        const container = document.getElementById('log-container');
        if (!container) return;
        
        container.innerHTML = this.logs.map(log => `
            <div class="log-item log-${log.type}">
                <span class="log-time">[${log.time}]</span>
                <span class="log-msg">${log.message}</span>
            </div>
        `).join('');
    }
    
    exportModels() {
        const data = {
            linear: this.modelManager.linearModel.exportModel(),
            neural: this.modelManager.neuralModel.exportModel(),
            current: this.modelManager.currentModel,
            exportTime: new Date().toISOString()
        };
        
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `hyperpredict-model-${Date.now()}.json`;
        a.click();
        URL.revokeObjectURL(url);
        
        this.addLog('模型已导出', 'success');
    }
    
    importModels(event) {
        const file = event.target.files?.[0];
        if (!file) return;
        
        const reader = new FileReader();
        reader.onload = (e) => {
            try {
                const data = JSON.parse(e.target.result);
                
                if (data.linear) this.modelManager.linearModel.importModel(data.linear);
                if (data.neural) this.modelManager.neuralModel.importModel(data.neural);
                if (data.current) this.modelManager.switchModel(data.current);
                
                this.addLog('模型已导入', 'success');
                this.updateModelStats();
            } catch (err) {
                this.addLog('模型导入失败', 'error');
            }
        };
        reader.readAsText(file);
    }
}

// Initialize app
document.addEventListener('DOMContentLoaded', () => {
    window.app = new HyperPredictApp();
});
