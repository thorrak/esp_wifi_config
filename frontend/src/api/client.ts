import type { WifiStatus, ScanResult, SavedNetwork, APStatus, APConfig, Variable } from '../types';

const API_BASE = '/api/wifi';

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options?.headers,
    },
  });

  if (!res.ok) {
    const error = await res.json().catch(() => ({ error: res.statusText }));
    throw new Error(error.error || 'Request failed');
  }

  return res.json();
}

export const api = {
  // Status
  getStatus: () => request<WifiStatus>('/status'),

  // Scan
  scan: () => request<{ networks: ScanResult[] }>('/scan').then(r => r.networks),

  // Networks
  getNetworks: () => request<{ networks: SavedNetwork[] }>('/networks').then(r => r.networks),

  addNetwork: (ssid: string, password: string, priority = 10) =>
    request('/networks', {
      method: 'POST',
      body: JSON.stringify({ ssid, password, priority }),
    }),

  deleteNetwork: (ssid: string) =>
    request(`/networks/${encodeURIComponent(ssid)}`, { method: 'DELETE' }),

  // Connection
  connect: (ssid?: string) =>
    request('/connect', {
      method: 'POST',
      body: ssid ? JSON.stringify({ ssid }) : '{}',
    }),

  disconnect: () => request('/disconnect', { method: 'POST' }),

  // AP
  getAPStatus: () => request<APStatus>('/ap/status'),
  getAPConfig: () => request<APConfig>('/ap/config'),

  updateAPConfig: (config: Partial<APConfig>) =>
    request('/ap/config', {
      method: 'PUT',
      body: JSON.stringify(config),
    }),

  startAP: () => request('/ap/start', { method: 'POST' }),
  stopAP: () => request('/ap/stop', { method: 'POST' }),

  // Variables
  getVars: () => request<{ vars: Variable[] }>('/vars').then(r => r.vars),

  setVar: (key: string, value: string) =>
    request(`/vars/${encodeURIComponent(key)}`, {
      method: 'PUT',
      body: JSON.stringify({ value }),
    }),

  deleteVar: (key: string) =>
    request(`/vars/${encodeURIComponent(key)}`, { method: 'DELETE' }),

  // System
  factoryReset: () => request('/factory_reset', { method: 'POST' }),
};
