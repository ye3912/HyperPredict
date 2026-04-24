import { useEffect, useRef, useState, useCallback } from 'react';
import { SystemStatus, ModelWeights, WSMessage } from '../types';

const WS_URL = 'ws://localhost:8081/ws';

export function useWebSocket() {
  const [connected, setConnected] = useState(false);
  const [status, setStatus] = useState<SystemStatus | null>(null);
  const [modelWeights, setModelWeights] = useState<ModelWeights | null>(null);
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimerRef = useRef<NodeJS.Timeout | null>(null);
  const reconnectAttemptsRef = useRef(0);

  const connect = useCallback(() => {
    try {
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => {
        setConnected(true);
        reconnectAttemptsRef.current = 0;
      };

      ws.onmessage = (event) => {
        try {
          const message: WSMessage = JSON.parse(event.data);

          // 处理状态更新
          if (message.type === 1 || message.data?.fps !== undefined) {
            setStatus(message.data || message);
          }

          // 处理模型权重
          if (message.type === 2 || message.data?.linear !== undefined) {
            setModelWeights(message.data || message);
          }
        } catch (e) {
          console.error('Failed to parse WebSocket message:', e);
        }
      };

      ws.onclose = () => {
        setConnected(false);
        scheduleReconnect();
      };

      ws.onerror = (error) => {
        console.error('WebSocket error:', error);
      };
    } catch (e) {
      console.error('Failed to connect WebSocket:', e);
      scheduleReconnect();
    }
  }, []);

  const scheduleReconnect = useCallback(() => {
    if (reconnectAttemptsRef.current >= 5) return;

    reconnectAttemptsRef.current++;
    const delay = Math.min(1000 * Math.pow(2, reconnectAttemptsRef.current), 30000);

    reconnectTimerRef.current = setTimeout(() => {
      connect();
    }, delay);
  }, [connect]);

  const disconnect = useCallback(() => {
    if (reconnectTimerRef.current) {
      clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = null;
    }
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }
    setConnected(false);
  }, []);

  useEffect(() => {
    connect();
    return () => disconnect();
  }, [connect, disconnect]);

  return { connected, status, modelWeights };
}
