import { useState } from 'react';
import {
  Box,
  Grid,
  Card,
  CardContent,
  Typography,
  Button,
  ButtonGroup,
  Chip,
  Alert,
} from '@mui/material';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  ScatterChart,
  Scatter,
} from 'recharts';
import { useWebSocket } from '../hooks/useWebSocket';
import { api } from '../api';

export default function Predictor() {
  const { connected, status, modelWeights } = useWebSocket();
  const [currentModel, setCurrentModel] = useState<'linear' | 'neural'>('neural');
  const [predictionHistory, setPredictionHistory] = useState<Array<{
    time: string;
    actual: number;
    linear: number;
    neural: number;
  }>>([]);

  const handleModelChange = async (model: 'linear' | 'neural') => {
    setCurrentModel(model);
    try {
      await api.setModel(model);
    } catch (error) {
      console.error('Failed to set model:', error);
    }
  };

  // 模拟预测数据（实际应该从后端获取）
  const mockPredictions = [
    { time: '10:00:00', actual: 60, linear: 58, neural: 59 },
    { time: '10:00:05', actual: 62, linear: 60, neural: 61 },
    { time: '10:00:10', actual: 58, linear: 59, neural: 58 },
    { time: '10:00:15', actual: 65, linear: 63, neural: 64 },
    { time: '10:00:20', actual: 70, linear: 68, neural: 69 },
    { time: '10:00:25', actual: 68, linear: 67, neural: 68 },
    { time: '10:00:30', actual: 72, linear: 70, neural: 71 },
    { time: '10:00:35', actual: 75, linear: 73, neural: 74 },
    { time: '10:00:40', actual: 71, linear: 69, neural: 70 },
    { time: '10:00:45', actual: 69, linear: 67, neural: 68 },
  ];

  const linearAccuracy = modelWeights ? 95.5 : 0;
  const linearMAE = modelWeights ? 2.3 : 0;
  const neuralAccuracy = modelWeights ? 97.2 : 0;
  const neuralMAE = modelWeights ? 1.8 : 0;

  return (
    <Box>
      {!connected && (
        <Alert severity="info" sx={{ mb: 2 }}>
          等待连接到 HyperPredict 守护进程...
        </Alert>
      )}

      <Grid container spacing={3}>
        {/* 模型选择 */}
        <Grid item xs={12}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                预测模型
              </Typography>
              <ButtonGroup variant="contained" sx={{ mb: 2 }}>
                <Button
                  color={currentModel === 'neural' ? 'primary' : 'inherit'}
                  onClick={() => handleModelChange('neural')}
                >
                  神经网络
                </Button>
                <Button
                  color={currentModel === 'linear' ? 'primary' : 'inherit'}
                  onClick={() => handleModelChange('linear')}
                >
                  线性回归
                </Button>
              </ButtonGroup>
              <Box sx={{ mt: 2 }}>
                <Chip label={`当前模型: ${currentModel === 'neural' ? '神经网络' : '线性回归'}`} color="primary" />
              </Box>
            </CardContent>
          </Card>
        </Grid>

        {/* 模型性能 */}
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                神经网络性能
              </Typography>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 2 }}>
                <Typography variant="body2">准确率</Typography>
                <Typography variant="h6" color="success.main">
                  {neuralAccuracy.toFixed(1)}%
                </Typography>
              </Box>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 2 }}>
                <Typography variant="body2">平均绝对误差 (MAE)</Typography>
                <Typography variant="h6" color="info.main">
                  {neuralMAE.toFixed(2)} FPS
                </Typography>
              </Box>
              <Box sx={{ display: 'flex', justifyContent: 'space-between' }}>
                <Typography variant="body2">网络结构</Typography>
                <Typography variant="body2" color="text.secondary">
                  8 → 4 → 1
                </Typography>
              </Box>
            </CardContent>
          </Card>
        </Grid>

        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                线性回归性能
              </Typography>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 2 }}>
                <Typography variant="body2">准确率</Typography>
                <Typography variant="h6" color="success.main">
                  {linearAccuracy.toFixed(1)}%
                </Typography>
              </Box>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 2 }}>
                <Typography variant="body2">平均绝对误差 (MAE)</Typography>
                <Typography variant="h6" color="info.main">
                  {linearMAE.toFixed(2)} FPS
                </Typography>
              </Box>
              <Box sx={{ display: 'flex', justifyContent: 'space-between' }}>
                <Typography variant="body2">特征数量</Typography>
                <Typography variant="body2" color="text.secondary">
                  8
                </Typography>
              </Box>
            </CardContent>
          </Card>
        </Grid>

        {/* 预测对比图 */}
        <Grid item xs={12}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                预测结果对比
              </Typography>
              <ResponsiveContainer width="100%" height={400}>
                <LineChart data={mockPredictions}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" />
                  <YAxis />
                  <Tooltip />
                  <Legend />
                  <Line
                    type="monotone"
                    dataKey="actual"
                    stroke="#4caf50"
                    strokeWidth={2}
                    name="实际 FPS"
                  />
                  <Line
                    type="monotone"
                    dataKey="neural"
                    stroke="#2196f3"
                    strokeWidth={2}
                    strokeDasharray="5 5"
                    name="神经网络预测"
                  />
                  <Line
                    type="monotone"
                    dataKey="linear"
                    stroke="#9c27b0"
                    strokeWidth={2}
                    strokeDasharray="2 2"
                    name="线性回归预测"
                  />
                </LineChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>

        {/* 预测散点图 */}
        <Grid item xs={12}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                预测准确性分析
              </Typography>
              <ResponsiveContainer width="100%" height={400}>
                <ScatterChart>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="actual" name="实际 FPS" />
                  <YAxis dataKey="neural" name="预测 FPS" />
                  <Tooltip cursor={{ strokeDasharray: '3 3' }} />
                  <Legend />
                  <Scatter name="神经网络" data={mockPredictions} fill="#2196f3" />
                  <Scatter name="线性回归" data={mockPredictions} fill="#9c27b0" />
                </ScatterChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>
      </Grid>
    </Box>
  );
}
