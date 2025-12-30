import { Card } from './ui/Card';
import type { WifiStatus } from '../types';
import './StatusCard.css';

interface Props {
  status: WifiStatus | null;
  loading: boolean;
}

function SignalBars({ quality }: { quality: number }) {
  const bars = 5;
  const filled = Math.round((quality / 100) * bars);
  return (
    <div class="signal-bars">
      {Array.from({ length: bars }).map((_, i) => (
        <div class={`bar ${i < filled ? 'filled' : ''}`} />
      ))}
    </div>
  );
}

export function StatusCard({ status, loading }: Props) {
  if (loading) {
    return (
      <Card>
        <div class="status-loading">Loading...</div>
      </Card>
    );
  }

  if (!status) {
    return (
      <Card>
        <div class="status-error">Unable to load status</div>
      </Card>
    );
  }

  const isConnected = status.state === 'connected';

  return (
    <Card>
      <div class="status-content">
        <div class={`status-icon ${isConnected ? 'connected' : ''}`}>
          {isConnected ? (
            <svg viewBox="0 0 24 24" fill="currentColor">
              <path d="M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3c-1.65-1.66-4.34-1.66-6 0zm-4-4l2 2c2.76-2.76 7.24-2.76 10 0l2-2C15.14 9.14 8.87 9.14 5 13z"/>
            </svg>
          ) : (
            <svg viewBox="0 0 24 24" fill="currentColor">
              <path d="M23.64 7c-.45-.34-4.93-4-11.64-4-1.5 0-2.89.19-4.15.48L18.18 13.8 23.64 7zm-6.6 8.22L3.27 1.44 2 2.72l2.05 2.06C1.91 5.76.59 6.82.36 7l11.63 14.49.01.01.01-.01 3.9-4.86 3.32 3.32 1.27-1.27-3.46-3.46z"/>
            </svg>
          )}
        </div>
        <div class="status-info">
          <div class="status-state">
            {isConnected ? `Connected to "${status.ssid}"` : 'Disconnected'}
          </div>
          {isConnected && (
            <>
              <div class="status-ip">{status.ip}</div>
              <div class="status-signal">
                <SignalBars quality={status.quality} />
                <span class="text-sm text-muted">{status.quality}%</span>
              </div>
            </>
          )}
        </div>
      </div>
    </Card>
  );
}
