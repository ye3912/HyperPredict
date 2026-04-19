import { useState } from 'react';
import { ThemeProvider, createTheme, CssBaseline, Box, AppBar, Toolbar, Typography, Container } from '@mui/material';
import { Speed, Psychology, Settings, Description } from '@mui/icons-material';
import Layout from './components/Layout';

const darkTheme = createTheme({
  palette: {
    mode: 'dark',
    primary: {
      main: '#1976d2',
    },
    secondary: {
      main: '#dc004e',
    },
    background: {
      default: '#121212',
      paper: '#1e1e1e',
    },
  },
  typography: {
    fontFamily: '"Roboto", "Helvetica", "Arial", sans-serif',
  },
});

function App() {
  const [currentTab, setCurrentTab] = useState(0);

  const tabs = [
    { label: '监控', icon: <Speed />, component: 'Dashboard' },
    { label: '预测', icon: <Psychology />, component: 'Predictor' },
    { label: '调度', icon: <Settings />, component: 'Scheduler' },
    { label: '配置', icon: <Description />, component: 'Config' },
  ];

  return (
    <ThemeProvider theme={darkTheme}>
      <CssBaseline />
      <Box sx={{ display: 'flex', flexDirection: 'column', minHeight: '100vh' }}>
        <AppBar position="static" elevation={0}>
          <Toolbar>
            <Typography variant="h6" component="div" sx={{ flexGrow: 1 }}>
              HyperPredict WebUI
            </Typography>
            <Typography variant="body2" sx={{ opacity: 0.7 }}>
              v4.2.0
            </Typography>
          </Toolbar>
        </AppBar>

        <Container maxWidth="xl" sx={{ mt: 3, mb: 3, flex: 1 }}>
          <Layout
            tabs={tabs}
            currentTab={currentTab}
            onTabChange={setCurrentTab}
          />
        </Container>
      </Box>
    </ThemeProvider>
  );
}

export default App;
