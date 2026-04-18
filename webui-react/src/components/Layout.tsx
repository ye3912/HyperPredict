import { Box, Paper, Tab, Tabs } from '@mui/material';
import Dashboard from './Dashboard';
import Predictor from './Predictor';
import Scheduler from './Scheduler';
import Config from './Config';

interface LayoutProps {
  tabs: Array<{ label: string; icon: React.ReactNode; component: string }>;
  currentTab: number;
  onTabChange: (index: number) => void;
}

export default function Layout({ tabs, currentTab, onTabChange }: LayoutProps) {
  const renderComponent = () => {
    const componentName = tabs[currentTab].component;
    
    switch (componentName) {
      case 'Dashboard':
        return <Dashboard />;
      case 'Predictor':
        return <Predictor />;
      case 'Scheduler':
        return <Scheduler />;
      case 'Config':
        return <Config />;
      default:
        return <div>未知组件</div>;
    }
  };

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      <Paper elevation={2}>
        <Tabs
          value={currentTab}
          onChange={(_, newValue) => onTabChange(newValue)}
          variant="scrollable"
          scrollButtons="auto"
          sx={{ borderBottom: 1, borderColor: 'divider' }}
        >
          {tabs.map((tab, index) => (
            <Tab
              key={index}
              label={tab.label}
              icon={tab.icon}
              iconPosition="start"
            />
          ))}
        </Tabs>
      </Paper>

      <Paper elevation={2} sx={{ p: 3, minHeight: 600 }}>
        {renderComponent()}
      </Paper>
    </Box>
  );
}
