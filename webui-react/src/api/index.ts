import { SystemStatus, ModelWeights } from '../types';

const API_BASE = '/api';

export const api = {
  // 获取系统状态
  async getStatus(): Promise<SystemStatus> {
    const response = await fetch(`${API_BASE}/status`);
    if (!response.ok) throw new Error('Failed to fetch status');
    return response.json();
  },

  // 获取模型权重
  async getModelWeights(): Promise<ModelWeights> {
    const response = await fetch(`${API_BASE}/model`);
    if (!response.ok) throw new Error('Failed to fetch model weights');
    return response.json();
  },

  // 设置模型
  async setModel(model: 'linear' | 'neural'): Promise<void> {
    const response = await fetch(`${API_BASE}/command`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd: 'set_model', model })
    });
    if (!response.ok) throw new Error('Failed to set model');
  },

  // 设置调度模式
  async setMode(mode: 'daily' | 'game' | 'turbo'): Promise<void> {
    const response = await fetch(`${API_BASE}/command`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd: 'set_mode', mode })
    });
    if (!response.ok) throw new Error('Failed to set mode');
  },

  // 设置 uclamp
  async setUclamp(min: number, max: number): Promise<void> {
    const response = await fetch(`${API_BASE}/command`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd: 'set_uclamp', min, max })
    });
    if (!response.ok) throw new Error('Failed to set uclamp');
  },

  // 设置温控预设
  async setThermalPreset(preset: 'aggressive' | 'balanced' | 'quiet'): Promise<void> {
    const response = await fetch(`${API_BASE}/command`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd: 'set_thermal', preset })
    });
    if (!response.ok) throw new Error('Failed to set thermal preset');
  }
};
