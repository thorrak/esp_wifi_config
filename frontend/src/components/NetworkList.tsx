import { useState } from 'preact/hooks';
import { Card } from './ui/Card';
import { Button } from './ui/Button';
import type { ScanResult } from '../types';
import { api } from '../api/client';
import './NetworkList.css';

interface Props {
  onConnect: (ssid: string) => void;
}

export function NetworkList({ onConnect }: Props) {
  const [networks, setNetworks] = useState<ScanResult[]>([]);
  const [scanning, setScanning] = useState(false);
  const [selectedSsid, setSelectedSsid] = useState<string | null>(null);
  const [password, setPassword] = useState('');
  const [connecting, setConnecting] = useState(false);

  const scan = async () => {
    setScanning(true);
    try {
      const results = await api.scan();
      setNetworks(results);
    } catch (e) {
      console.error('Scan failed:', e);
    } finally {
      setScanning(false);
    }
  };

  const connect = async (ssid: string) => {
    setConnecting(true);
    try {
      // Add network first if password provided
      if (password) {
        await api.addNetwork(ssid, password);
      }
      await api.connect(ssid);
      onConnect(ssid);
      setSelectedSsid(null);
      setPassword('');
    } catch (e) {
      console.error('Connect failed:', e);
    } finally {
      setConnecting(false);
    }
  };

  return (
    <Card
      title="Available Networks"
      action={
        <Button size="sm" variant="secondary" onClick={scan} loading={scanning}>
          Scan
        </Button>
      }
    >
      {networks.length === 0 ? (
        <div class="network-empty">
          {scanning ? 'Scanning...' : 'Click Scan to find networks'}
        </div>
      ) : (
        <div class="network-list">
          {networks.map((net) => (
            <div class="network-item" key={net.ssid}>
              <div class="network-info">
                <div class="network-ssid">
                  {net.auth !== 'OPEN' && <span class="lock">🔒</span>}
                  {net.ssid || '(hidden)'}
                </div>
                <div class="network-meta text-sm text-muted">
                  {net.rssi} dBm • {net.auth}
                </div>
              </div>
              <Button
                size="sm"
                onClick={() => setSelectedSsid(selectedSsid === net.ssid ? null : net.ssid)}
              >
                {selectedSsid === net.ssid ? 'Cancel' : 'Connect'}
              </Button>

              {selectedSsid === net.ssid && (
                <div class="network-connect-form">
                  {net.auth !== 'OPEN' && (
                    <input
                      type="password"
                      placeholder="Password"
                      value={password}
                      onInput={(e) => setPassword((e.target as HTMLInputElement).value)}
                    />
                  )}
                  <Button
                    onClick={() => connect(net.ssid)}
                    loading={connecting}
                    class="w-full"
                  >
                    Connect
                  </Button>
                </div>
              )}
            </div>
          ))}
        </div>
      )}
    </Card>
  );
}
