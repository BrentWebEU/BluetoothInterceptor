export interface ConnectionConfig {
  host: string;
  port: number;
  timeout?: number;
}

export interface AudioChunk {
  data: Uint8Array;
  timestamp: number;
  size: number;
}

export enum ConnectionState {
  DISCONNECTED = 'DISCONNECTED',
  CONNECTING = 'CONNECTING',
  CONNECTED = 'CONNECTED',
  ERROR = 'ERROR',
}

export interface ConnectionStats {
  bytesReceived: number;
  packetsReceived: number;
  connectionTime: number;
  lastPacketTime: number;
}
