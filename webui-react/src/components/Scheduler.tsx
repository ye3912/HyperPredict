import { useState } from 'react';
import {
  Box,
  Grid,
  Card,
  CardContent,
  Typography,
  Button,
  ButtonGroup,
  Slider,
  Chip,
  Alert,
  Divider,
} from '@mui/material';
import { useWebSocket } from '../hooks/useWebSocket';
import { api } from '../api';

export default function Scheduler() {
  const { connected, status } = useWebSocket();
  const [mode, setMode] = useState<'daily' | 'game' | 'turbo'>('game');
  const [uclampMin, setUclampMin] = useState(50);
  const [uclampMax, setUclampMax] = useState(100);
  const [thermalPreset, setThermalPreset] = useState<'aggressive' | 'balanced' | 'quiet'>('balanced');

  const handleModeChange = async (newMode: 'daily' | 'game' | 'turbo') => {
    setMode(newMode);
    try {
      await api.setMode(newMode);
    } catch (error) {
      console.error('Failed to set mode:', error);
    }
  };

  const handleUclampChange = async () => {
    try {
      await api.setUclamp(uclampMin, uclampMax);
    } catch (error) {
      console.error('Failed to set uclamp:', error);
    }
  };

  const handleThermalPresetChange = async (preset: 'aggressive' | 'balanced' | 'quiet') => {
    setThermalPreset(preset);
    try {
      await api.setThermalPreset(preset);
    } catch (error) {
      console.error('Failed to set thermal preset:', error);
    }
  };

  const modeDescriptions = {
    daily: '均衡模式 - 适合日常使用，平衡性能和功耗',
    game: '游戏模式 - 优化游戏性能，提升帧率稳定性',
    turbo: '性能模式 - 最大性能输出，适合高负载场景',
  };

  const thermalDescriptions = {
    aggressive: '激进 - 优先性能，允许更高温度',
    balanced: '均衡 - 平衡性能和温度',
    quiet: '静音 - 优先温度控制，降低风扇噪音',
  };

  return (
    <Box>
      {!connected && (
        <Alert severity="info" sx={{ mb: 2 }}>
          等待连接到 HyperPredict 守护进程...
        </Alert>
      )}

      <Grid container spacing={3}>
        {/* 调度模式 */}
        <Grid item xs={12}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                调度模式
              </Typography>
              <ButtonGroup variant="contained" sx={{ mb: 2, display: 'flex', flexWrap: 'wrap' }}>
                <Button
                  color={mode === 'daily' ? 'primary' : 'inherit'}
                  onClick={() => handleModeChange('daily')}
                >
                  ⚖️ 均衡
                </Button>
                <Button
                  color={mode === 'game' ? 'primary' : 'inherit'}
                  onClick={() => handleModeChange('game')}
                >
                  🎮 游戏
                </Button>
                <Button
                  color={mode === 'turbo' ? 'primary' : 'inherit'}
                  onClick={() => handleModeChange('turbo')}
                >
                  🚀 性能
                </Button>
              </ButtonGroup>
              <Box sx={{ mt: 2 }}>
                <Typography variant="body2" color="text.secondary">
                  {modeDescriptions[mode]}
                </Typography>
              </Box>
              {status && (
                <Box sx={{ mt: 2 }}>
                  <Chip label={`目标 FPS: ${status.target_fps}`} color="primary" />
                  <Chip label={`当前模式: ${status.mode}`} sx={{ ml: 1 }} />
                </Box>
              )}
            </CardContent>
          </Card>
        </Grid>

        {/* uclamp 设置 */}
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                uclamp 设置
              </Typography>
              
              <Box sx={{ mt: 3 }}>
                <Typography variant="body2" gutterBottom>
                  uclamp.min: {uclampMin}%
                </Typography>
                <Slider
                  value={uclampMin}
                  onChange={(_, value) => setUclampMin(value as number)}
                  onChangeCommitted={handleUclampChange}
                  min={0}
                  max={100}
                  valueLabelDisplay="auto"
                />
                <Typography variant="caption" color="text.secondary">
                  最小性能限制，防止核心降频过低
                </Typography>
              </Box>

              <Divider sx={{ my: 3 }} />

              <Box>
                <Typography variant="body2" gutterBottom>
                  uclamp.max: {uclampMax}%
                </Typography>
                <Slider
                  value={uclampMax}
                  onChange={(_, value) => setUclampMax(value as number)}
                  onChangeCommitted={handleUclampChange}
                  min={0}
                  max={100}
                  valueLabelDisplay="auto"
                />
                <Typography variant="caption" color="text.secondary">
                  最大性能限制，防止核心超频过高
                </Typography>
              </Box>

              {status && (
                <Box sx={{ mt: 2 }}>
                  <Chip label={`当前 uclamp.min: ${status.uclamp_min}%`} size="small" />
                  <Chip label={`当前 uclamp.max: ${status.uclamp_max}%`} size="small" sx={{ ml: 1 }} />
                </Box>
              )}
            </CardContent>
          </Card>
        </Grid>

        {/* 温控预设 */}
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                温控预设
              </Typography>
              <ButtonGroup variant="contained" sx={{ mb: 2, display: 'flex', flexWrap: 'wrap' }}>
                <Button
                  color={thermalPreset === 'aggressive' ? 'error' : 'inherit'}
                  onClick={() => handleThermalPresetChange('aggressive')}
                >
                  🥶 激进
                </Button>
                <Button
                  color={thermalPreset === 'balanced' ? 'primary' : 'inherit'}
                  onClick={() => handleThermalPresetChange('balanced')}
                >
                  ⚖️ 均衡
                </Button>
                <Button
                  color={thermalPreset === 'quiet' ? 'success' : 'inherit'}
                  onClick={() => handleThermalPresetChange('quiet')}
                >
                  🔇 静音
                </Button>
              </ButtonGroup>
              <Box sx={{ mt: 2 }}>
                <Typography variant="body2" color="text.secondary">
                  {thermalDescriptions[thermalPreset]}
                </Typography>
              </Box>
              {status && (
                <Box sx={{ mt: 2 }}>
                  <Chip label={`当前温度: ${status.temperature}°C`} size="small" />
                  <Chip label={`温控余量: ${status.thermal_margin}°C`} size="small" sx={{ ml: 1 }} />
                </Box>
              )}
            </CardContent>
          </Card>
        </Grid>

        {/* 当前状态 */}
        <Grid item xs={12}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                当前调度状态
              </Typography>
              <Grid container spacing={2}>
                <Grid item xs={6} md={3}>
                  <Typography variant="body2" color="text.secondary">
                    调度模式
                  </Typography>
                  <Typography variant="h6">
                    {status?.mode || 'N/A'}
                  </Typography>
                </Grid>
                <Grid item xs={6} md={3}>
                  <Typography variant="body2" color="text.secondary">
                    目标 FPS
                  </Typography>
                  <Typography variant="h6">
                    {status?.target_fps || 'N/A'}
                  </Typography>
                </Grid>
                <Grid item xs={6} md={3}>
                  <Typography variant="body2" color="text.secondary">
                    uclamp.min
                  </Typography>
                  <Typography variant="h6">
                    {status?.uclamp_min || 'N/A'}%
                  </Typography>
                </Grid>
                <Grid item xs={6} md={3}>
                  <Typography variant="body2" color="text.secondary">
                    uclamp.max
                  </Typography>
                  <Typography variant="h6">
                    {status?.uclamp_max || 'N/A'}%
                  </Typography>
                </Grid>
              </Grid>
            </CardContent>
          </Card>
        </Grid>
      </Grid>
    </Box>
  );
}
