export interface WifiStatus {
  state: 'connected' | 'connecting' | 'disconnected';
  ssid: string;
  rssi: number;
  quality: number;
  channel: number;
  ip: string;
  netmask: string;
  gateway: string;
  dns: string;
  mac: string;
  hostname: string;
  uptime_ms: number;
  ap_active: boolean;
}

export interface ScanResult {
  ssid: string;
  rssi: number;
  auth: string;
}

export interface SavedNetwork {
  ssid: string;
  priority: number;
}

export interface APStatus {
  active: boolean;
  ssid: string;
  ip: string;
  channel: number;
  sta_count: number;
  clients: Array<{ mac: string; ip: string }>;
}

export interface APConfig {
  ssid: string;
  password: string;
  channel: number;
  max_connections: number;
  hidden: boolean;
  ip: string;
  netmask: string;
  gateway: string;
  dhcp_start: string;
  dhcp_end: string;
}

export interface Variable {
  key: string;
  value: string;
}
