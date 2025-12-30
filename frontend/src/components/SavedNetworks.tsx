import { useState, useEffect } from 'preact/hooks';
import { Card } from './ui/Card';
import { Button } from './ui/Button';
import type { SavedNetwork } from '../types';
import { api } from '../api/client';
import './SavedNetworks.css';

export function SavedNetworks() {
  const [networks, setNetworks] = useState<SavedNetwork[]>([]);
  const [loading, setLoading] = useState(true);
  const [showAdd, setShowAdd] = useState(false);
  const [ssid, setSsid] = useState('');
  const [password, setPassword] = useState('');
  const [adding, setAdding] = useState(false);

  const load = async () => {
    try {
      const nets = await api.getNetworks();
      setNetworks(nets);
    } catch (e) {
      console.error('Failed to load networks:', e);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    load();
  }, []);

  const handleAdd = async () => {
    if (!ssid.trim()) return;
    setAdding(true);
    try {
      await api.addNetwork(ssid, password);
      setSsid('');
      setPassword('');
      setShowAdd(false);
      load();
    } catch (e) {
      console.error('Failed to add network:', e);
    } finally {
      setAdding(false);
    }
  };

  const handleDelete = async (ssid: string) => {
    try {
      await api.deleteNetwork(ssid);
      load();
    } catch (e) {
      console.error('Failed to delete network:', e);
    }
  };

  return (
    <Card
      title="Saved Networks"
      action={
        <Button size="sm" variant="secondary" onClick={() => setShowAdd(!showAdd)}>
          {showAdd ? 'Cancel' : '+ Add'}
        </Button>
      }
    >
      {showAdd && (
        <div class="add-network-form mb-4">
          <input
            type="text"
            placeholder="SSID"
            value={ssid}
            onInput={(e) => setSsid((e.target as HTMLInputElement).value)}
          />
          <input
            type="password"
            placeholder="Password (optional)"
            value={password}
            onInput={(e) => setPassword((e.target as HTMLInputElement).value)}
          />
          <Button onClick={handleAdd} loading={adding} class="w-full">
            Add Network
          </Button>
        </div>
      )}

      {loading ? (
        <div class="saved-empty">Loading...</div>
      ) : networks.length === 0 ? (
        <div class="saved-empty">No saved networks</div>
      ) : (
        <div class="saved-list">
          {networks.map((net) => (
            <div class="saved-item" key={net.ssid}>
              <div class="saved-info">
                <div class="saved-ssid">{net.ssid}</div>
                <div class="text-sm text-muted">Priority: {net.priority}</div>
              </div>
              <Button size="sm" variant="ghost" onClick={() => handleDelete(net.ssid)}>
                ×
              </Button>
            </div>
          ))}
        </div>
      )}
    </Card>
  );
}
