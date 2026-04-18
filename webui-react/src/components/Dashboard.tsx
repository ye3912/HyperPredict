import { useEffect, useState } from 'react';
import {
  Box,
  Grid,
  Card,
  CardContent,
  Typography,
  LinearProgress,
  Chip,
  Alert,
} from '@mui/material';
import {
  Speed,
  Thermostat,
  BatteryChargingFull,
  Memory,
  TrendingUp,
  TrendingDown,
} from '@mui/icons-material';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  AreaChart,
  Area,
} from 'recharts';
import { useWebSocket } from '../hooks/useWebSocket';
import { SystemStatus } from '../types';

export default function Dashboard() {
  const { connected, status } = useWebSocket();
  const [history, setHistory] = useState<Array<{ time: string; fps: number; temp: number; cpu: number }>>([]);

  useEffect(() => {
    if (status) {
      const now = new Date();
      const timeStr = now.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' });

      setHistory(prev => {
        const newHistory = [
          ...prev,
          {
            time: timeStr,
            fps: status.fps,
            temp: status.temperature,
            cpu: status.cpu_util,
          },
        ];
        // 保留最近 60 个数据点
        return newHistory.slice(-60);
      });
    }
  }, [status]);

  if (!status) {
    return (
      <Alert severity="info" sx={{ mb: 2 }}>
        等待连接到 HyperPredict 守护进程...
      </Alert>
    );
  }

  const fpsStatus = status.fps >= 90 ? '流畅' : status.fps >= 60 ? '良好' : status.fps >= 30 ? '一般' : '卡顿';
  const fpsColor = status.fps >= 90 ? 'success' : status.fps >= 60 ? 'info' : status.fps >= 30 ? 'warning' : 'error';

  return (
    <Box>
      <Grid container spacing={3}>
        {/* 连接状态 */}
        <Grid item xs={12}>
          <Chip
            label={connected ? '已连接' : '未连接'}
            color={connected ? 'success' : 'error'}
            sx={{ mb: 2 }}
          />
        </Grid>

        {/* 核心指标 */}
        <Grid item xs={12} sm={6} md={3}>
          <Card>
            <CardContent>
              <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
                <Speed sx={{ mr: 1, color: 'primary.main' }} />
                <Typography variant="body2" color="text.secondary">
                  FPS
                </Typography>
              </Box>
              <Typography variant="h4" component="div">
                {status.fps}
              </Typography>
              <Chip label={fpsStatus} color={fpsColor as any} size="small" sx={{ mt: 1 }} />
            </CardContent>
          </Card>
        </Grid>

        <Grid item xs={12} sm={6} md={3}>
          <Card>
            <CardContent>
              <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
                <Thermostat sx={{ mr: 1, color: 'error.main' }} />
                <Typography variant="body2" color="text.secondary">
                  温度
                </Typography>
              </Box>
              <Typography variant="h4" component="div">
                {status.temperature}°C
              </Typography>
              <Typography variant="body2" color="text.secondary" sx={{ mt: 1 }}>
                余量: {status.thermal_margin}°C
              </Typography>
            </CardContent>
          </Card>
        </Grid>

        <Grid item xs={12} sm={6} md={3}>
          <Card>
            <CardContent>
              <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
                <BatteryChargingFull sx={{ mr: 1, color: 'success.main' }} />
                <Typography variant="body2" color="text.secondary">
                  电池
                </Typography>
              </Box>
              <Typography variant="h4" component="div">
                {status.battery_level}%
              </Typography>
              <LinearProgress
                variant="determinate"
                value={status.battery_level}
                sx={{ mt: 1 }}
              />
            </CardContent>
          </Card>
        </Grid>

        <Grid item xs={12} sm={6} md={3}>
          <Card>
            <CardContent>
              <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
                <Memory sx={{ mr: 1, color: 'warning.main' }} />
                <Typography variant="body2" color="text.secondary">
                  CPU 负载
                </Typography>
              </Box>
              <Typography variant="h4" component="div">
                {Math.round(status.cpu_util)}
              </Typography>
              <Typography variant="body2" color="text.secondary" sx={{ mt: 1 }}>
                运行队列: {status.run_queue_len}
              </Typography>
            </CardContent>
          </Card>
        </Grid>

        {/* FPS 趋势图 */}
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                FPS 趋势
              </Typography>
              <ResponsiveContainer width="100%" height={300}>
                <AreaChart data={history}>
                  <defs>
                    <linearGradient id="colorFps" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="#1976d2" stopOpacity={0.8} />
                      <stop offset="95%" stopColor="#1976d2" stopOpacity={0} />
                    </linearGradient>
                  </defs>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" />
                  <YAxis />
                  <Tooltip />
                  <Area
                    type="monotone"
                    dataKey="fps"
                    stroke="#1976d2"
                    fillOpacity={1}
                    fill="url(#colorFps)"
                  />
                </AreaChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>

        {/* 温度趋势图 */}
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                温度趋势
              </Typography>
              <ResponsiveContainer width="100%" height={300}>
                <LineChart data={history}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" />
                  <YAxis />
                  <Tooltip />
                  <Legend />
                  <Line
                    type="monotone"
                    dataKey="temp"
                    stroke="#f44336"
                    strokeWidth={2}
                    name="温度 (°C)"
                  />
                </LineChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>

        {/* CPU 负载趋势图 */}
        <Grid item xs={12}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                CPU 负载趋势
              </Typography>
              <ResponsiveContainer width="100%" height={300}>
                <LineChart data={history}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" />
                  <YAxis />
                  <Tooltip />
                  <Legend />
                  <Line
                    type="monotone"
                    dataKey="cpu"
                    stroke="#ff9800"
                    strokeWidth={2}
                    name="CPU 负载"
                  />
                </LineChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>

        {/* 集群信息 */}
        <Grid item xs={12} md={4}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                LITTLE 集群
              </Typography>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 1 }}>
                <Typography variant="body2">频率</Typography>
                <Typography variant="body2" color="primary">
                  {status.clusters.little.freq} MHz
                </Typography>
              </Box>
              <LinearProgress
                variant="determinate"
                value={(status.clusters.little.freq / 2000) * 100}
                sx={{ mb: 2 }}
              />
              <Box sx={{ display: 'flex', justifyContent: 'space-between' }}>
                <Typography variant="body2">使用率</Typography>
                <Typography variant="body2" color="primary">
                  {status.clusters.little.usage}%
                </Typography>
              </Box>
              <LinearProgress
                variant="determinate"
                value={status.clusters.little.usage}
              />
            </CardContent>
          </Card>
        </Grid>

        <Grid item xs={12} md={4}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                MID 集群
              </Typography>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 1 }}>
                <Typography variant="body2">频率</Typography>
                <Typography variant="body2" color="primary">
                  {status.clusters.mid.freq} MHz
                </Typography>
              </Box>
              <LinearProgress
                variant="determinate"
                value={(status.clusters.mid.freq / 2500) * 100}
                sx={{ mb: 2 }}
              />
              <Box sx={{ display: 'flex', justifyContent: 'space-between' }}>
                <Typography variant="body2">使用率</Typography>
                <Typography variant="body2" color="primary">
                  {status.clusters.mid.usage}%
                </Typography>
              </Box>
              <LinearProgress
                variant="determinate"
                value={status.clusters.mid.usage}
              />
            </CardContent>
          </Card>
        </Grid>

        <Grid item xs={12} md={4}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                BIG 集群
              </Typography>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 1 }}>
                <Typography variant="body2">频率</Typography>
                <Typography variant="body2" color="primary">
                  {status.clusters.big.freq} MHz
                </Typography>
              </Box>
              <LinearProgress
                variant="determinate"
                value={(status.clusters.big.freq / 3200) * 100}
                sx={{ mb: 2 }}
              />
              <Box sx={{ display: 'flex', justifyContent: 'space-between' }}>
                <Typography variant="body2">使用率</Typography>
                <Typography variant="body2" color="primary">
                  {status.clusters.big.usage}%
                </Typography>
              </Box>
              <LinearProgress
                variant="determinate"
                value={status.clusters.big.usage}
              />
            </CardContent>
          </Card>
        </Grid>
      </Grid>
    </Box>
  );
}
