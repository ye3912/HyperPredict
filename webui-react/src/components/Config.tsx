import { useState, useEffect } from 'react';
import {
  Box,
  Grid,
  Card,
  CardContent,
  Typography,
  Button,
  TextField,
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  List,
  ListItem,
  ListItemText,
  ListItemSecondaryAction,
  IconButton,
  Chip,
  Alert,
  Divider,
  Tabs,
  Tab,
} from '@mui/material';
import {
  Add as AddIcon,
  Delete as DeleteIcon,
  Download as DownloadIcon,
  Upload as UploadIcon,
  Save as SaveIcon,
} from '@mui/icons-material';
import { ConfigPreset } from '../types';

export default function Config() {
  const [tabValue, setTabValue] = useState(0);
  const [presets, setPresets] = useState<ConfigPreset[]>([]);
  const [openDialog, setOpenDialog] = useState(false);
  const [editingPreset, setEditingPreset] = useState<Partial<ConfigPreset> | null>(null);
  const [alert, setAlert] = useState<{ type: 'success' | 'error'; message: string } | null>(null);

  // 加载预设
  useEffect(() => {
    loadPresets();
  }, []);

  const loadPresets = () => {
    try {
      const saved = localStorage.getItem('hp_config_presets');
      if (saved) {
        setPresets(JSON.parse(saved));
      } else {
        // 默认预设
        const defaultPresets: ConfigPreset[] = [
          {
            id: 'default-game',
            name: '默认游戏',
            description: '适合大多数游戏的默认配置',
            mode: 'game',
            uclamp_min: 50,
            uclamp_max: 100,
            thermal_preset: 'balanced',
            created_at: new Date().toISOString(),
          },
          {
            id: 'default-daily',
            name: '默认日常',
            description: '适合日常使用的默认配置',
            mode: 'daily',
            uclamp_min: 30,
            uclamp_max: 80,
            thermal_preset: 'balanced',
            created_at: new Date().toISOString(),
          },
        ];
        setPresets(defaultPresets);
        savePresets(defaultPresets);
      }
    } catch (error) {
      console.error('Failed to load presets:', error);
    }
  };

  const savePresets = (newPresets: ConfigPreset[]) => {
    try {
      localStorage.setItem('hp_config_presets', JSON.stringify(newPresets));
      setPresets(newPresets);
    } catch (error) {
      console.error('Failed to save presets:', error);
    }
  };

  const handleAddPreset = () => {
    setEditingPreset({
      name: '',
      description: '',
      mode: 'game',
      uclamp_min: 50,
      uclamp_max: 100,
      thermal_preset: 'balanced',
    });
    setOpenDialog(true);
  };

  const handleEditPreset = (preset: ConfigPreset) => {
    setEditingPreset({ ...preset });
    setOpenDialog(true);
  };

  const handleDeletePreset = (id: string) => {
    const newPresets = presets.filter(p => p.id !== id);
    savePresets(newPresets);
    showAlert('success', '预设已删除');
  };

  const handleSavePreset = () => {
    if (!editingPreset?.name) {
      showAlert('error', '请输入预设名称');
      return;
    }

    const newPreset: ConfigPreset = {
      id: editingPreset.id || `preset-${Date.now()}`,
      name: editingPreset.name,
      description: editingPreset.description || '',
      mode: editingPreset.mode || 'game',
      uclamp_min: editingPreset.uclamp_min || 50,
      uclamp_max: editingPreset.uclamp_max || 100,
      thermal_preset: editingPreset.thermal_preset || 'balanced',
      created_at: editingPreset.created_at || new Date().toISOString(),
    };

    const existingIndex = presets.findIndex(p => p.id === newPreset.id);
    let newPresets;
    if (existingIndex >= 0) {
      newPresets = [...presets];
      newPresets[existingIndex] = newPreset;
    } else {
      newPresets = [...presets, newPreset];
    }

    savePresets(newPresets);
    setOpenDialog(false);
    showAlert('success', '预设已保存');
  };

  const handleExportConfig = () => {
    const config = {
      presets,
      exportTime: new Date().toISOString(),
      version: '4.2.0',
    };

    const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `hyperpredict-config-${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
    showAlert('success', '配置已导出');
  };

  const handleImportConfig = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      try {
        const config = JSON.parse(e.target?.result as string);
        if (config.presets && Array.isArray(config.presets)) {
          savePresets(config.presets);
          showAlert('success', '配置已导入');
        } else {
          showAlert('error', '无效的配置文件');
        }
      } catch (error) {
        showAlert('error', '导入失败');
      }
    };
    reader.readAsText(file);
    event.target.value = '';
  };

  const showAlert = (type: 'success' | 'error', message: string) => {
    setAlert({ type, message });
    setTimeout(() => setAlert(null), 3000);
  };

  return (
    <Box>
      {alert && (
        <Alert severity={alert.type} sx={{ mb: 2 }} onClose={() => setAlert(null)}>
          {alert.message}
        </Alert>
      )}

      <Grid container spacing={3}>
        {/* 配置预设 */}
        <Grid item xs={12}>
          <Card>
            <CardContent>
              <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', mb: 2 }}>
                <Typography variant="h6">
                  配置预设
                </Typography>
                <Box>
                  <Button
                    startIcon={<AddIcon />}
                    onClick={handleAddPreset}
                    sx={{ mr: 1 }}
                  >
                    添加预设
                  </Button>
                  <Button
                    startIcon={<DownloadIcon />}
                    onClick={handleExportConfig}
                    sx={{ mr: 1 }}
                  >
                    导出
                  </Button>
                  <Button
                    startIcon={<UploadIcon />}
                    component="label"
                  >
                    导入
                    <input
                      type="file"
                      accept=".json"
                      hidden
                      onChange={handleImportConfig}
                    />
                  </Button>
                </Box>
              </Box>

              <List>
                {presets.map((preset) => (
                  <ListItem key={preset.id} divider>
                    <ListItemText
                      primary={preset.name}
                      secondary={
                        <Box>
                          <Typography variant="body2" color="text.secondary">
                            {preset.description}
                          </Typography>
                          <Box sx={{ mt: 1 }}>
                            <Chip label={`模式: ${preset.mode}`} size="small" sx={{ mr: 1 }} />
                            <Chip label={`uclamp: ${preset.uclamp_min}%-${preset.uclamp_max}%`} size="small" sx={{ mr: 1 }} />
                            <Chip label={`温控: ${preset.thermal_preset}`} size="small" />
                          </Box>
                        </Box>
                      }
                    />
                    <ListItemSecondaryAction>
                      <IconButton
                        edge="end"
                        onClick={() => handleEditPreset(preset)}
                        sx={{ mr: 1 }}
                      >
                        <SaveIcon />
                      </IconButton>
                      <IconButton
                        edge="end"
                        onClick={() => handleDeletePreset(preset.id)}
                      >
                        <DeleteIcon />
                      </IconButton>
                    </ListItemSecondaryAction>
                  </ListItem>
                ))}
              </List>
            </CardContent>
          </Card>
        </Grid>

        {/* 系统信息 */}
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                系统信息
              </Typography>
              <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                <Box>
                  <Typography variant="body2" color="text.secondary">
                    版本
                  </Typography>
                  <Typography variant="body1">
                    v4.2.0
                  </Typography>
                </Box>
                <Box>
                  <Typography variant="body2" color="text.secondary">
                    构建时间
                  </Typography>
                  <Typography variant="body1">
                    {new Date().toLocaleDateString('zh-CN')}
                  </Typography>
                </Box>
                <Box>
                  <Typography variant="body2" color="text.secondary">
                    配置存储
                  </Typography>
                  <Typography variant="body1">
                    LocalStorage
                  </Typography>
                </Box>
              </Box>
            </CardContent>
          </Card>
        </Grid>

        {/* 使用说明 */}
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                使用说明
              </Typography>
              <Typography variant="body2" color="text.secondary" paragraph>
                1. 点击"添加预设"创建新的配置预设
              </Typography>
              <Typography variant="body2" color="text.secondary" paragraph>
                2. 编辑预设可以调整调度模式、uclamp 和温控设置
              </Typography>
              <Typography variant="body2" color="text.secondary" paragraph>
                3. 使用"导出"功能保存配置到本地文件
              </Typography>
              <Typography variant="body2" color="text.secondary">
                4. 使用"导入"功能从本地文件恢复配置
              </Typography>
            </CardContent>
          </Card>
        </Grid>
      </Grid>

      {/* 编辑对话框 */}
      <Dialog open={openDialog} onClose={() => setOpenDialog(false)} maxWidth="sm" fullWidth>
        <DialogTitle>
          {editingPreset?.id ? '编辑预设' : '添加预设'}
        </DialogTitle>
        <DialogContent>
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2, mt: 2 }}>
            <TextField
              label="预设名称"
              fullWidth
              value={editingPreset?.name || ''}
              onChange={(e) => setEditingPreset({ ...editingPreset, name: e.target.value })}
            />
            <TextField
              label="描述"
              fullWidth
              multiline
              rows={2}
              value={editingPreset?.description || ''}
              onChange={(e) => setEditingPreset({ ...editingPreset, description: e.target.value })}
            />
            <TextField
              label="调度模式"
              select
              fullWidth
              value={editingPreset?.mode || 'game'}
              onChange={(e) => setEditingPreset({ ...editingPreset, mode: e.target.value as any })}
              SelectProps={{ native: true }}
            >
              <option value="daily">均衡</option>
              <option value="game">游戏</option>
              <option value="turbo">性能</option>
            </TextField>
            <Box>
              <Typography variant="body2" gutterBottom>
                uclamp.min: {editingPreset?.uclamp_min || 50}%
              </Typography>
              <input
                type="range"
                min="0"
                max="100"
                value={editingPreset?.uclamp_min || 50}
                onChange={(e) => setEditingPreset({ ...editingPreset, uclamp_min: parseInt(e.target.value) })}
                style={{ width: '100%' }}
              />
            </Box>
            <Box>
              <Typography variant="body2" gutterBottom>
                uclamp.max: {editingPreset?.uclamp_max || 100}%
              </Typography>
              <input
                type="range"
                min="0"
                max="100"
                value={editingPreset?.uclamp_max || 100}
                onChange={(e) => setEditingPreset({ ...editingPreset, uclamp_max: parseInt(e.target.value) })}
                style={{ width: '100%' }}
              />
            </Box>
            <TextField
              label="温控预设"
              select
              fullWidth
              value={editingPreset?.thermal_preset || 'balanced'}
              onChange={(e) => setEditingPreset({ ...editingPreset, thermal_preset: e.target.value as any })}
              SelectProps={{ native: true }}
            >
              <option value="aggressive">激进</option>
              <option value="balanced">均衡</option>
              <option value="quiet">静音</option>
            </TextField>
          </Box>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setOpenDialog(false)}>
            取消
          </Button>
          <Button onClick={handleSavePreset} variant="contained">
            保存
          </Button>
        </DialogActions>
      </Dialog>
    </Box>
  );
}
