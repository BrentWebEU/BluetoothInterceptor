import TcpSocket from 'react-native-tcp-socket';
import {
  ConnectionConfig,
  ConnectionState,
  AudioChunk,
  ConnectionStats,
} from '../types';

type DataCallback = (chunk: AudioChunk) => void;
type StateCallback = (state: ConnectionState) => void;
type StatsCallback = (stats: ConnectionStats) => void;

class TCPConnectionService {
  private socket: TcpSocket.Socket | null = null;
  private connectionState: ConnectionState = ConnectionState.DISCONNECTED;
  private dataCallback: DataCallback | null = null;
  private stateCallback: StateCallback | null = null;
  private statsCallback: StatsCallback | null = null;
  
  private stats: ConnectionStats = {
    bytesReceived: 0,
    packetsReceived: 0,
    connectionTime: 0,
    lastPacketTime: 0,
  };

  constructor() {
    this.socket = null;
  }

  connect(config: ConnectionConfig): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.socket) {
        this.disconnect();
      }

      this.updateState(ConnectionState.CONNECTING);

      this.socket = TcpSocket.createConnection(
        {
          port: config.port,
          host: config.host,
          timeout: config.timeout || 5000,
        },
        () => {
          console.log('Connected to Raspberry Pi');
          this.updateState(ConnectionState.CONNECTED);
          this.stats.connectionTime = Date.now();
          resolve();
        },
      );

      this.socket.on('data', (data: Uint8Array) => {
        this.handleData(data);
      });

      this.socket.on('error', (error: Error) => {
        console.error('TCP Socket Error:', error);
        this.updateState(ConnectionState.ERROR);
        reject(error);
      });

      this.socket.on('close', () => {
        console.log('Connection closed');
        this.updateState(ConnectionState.DISCONNECTED);
        this.socket = null;
      });
    });
  }

  disconnect(): void {
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
    this.updateState(ConnectionState.DISCONNECTED);
    this.resetStats();
  }

  onData(callback: DataCallback): void {
    this.dataCallback = callback;
  }

  onStateChange(callback: StateCallback): void {
    this.stateCallback = callback;
  }

  onStats(callback: StatsCallback): void {
    this.statsCallback = callback;
  }

  getState(): ConnectionState {
    return this.connectionState;
  }

  getStats(): ConnectionStats {
    return {...this.stats};
  }

  private handleData(data: Uint8Array): void {
    const chunk: AudioChunk = {
      data: data,
      timestamp: Date.now(),
      size: data.length,
    };

    this.stats.bytesReceived += data.length;
    this.stats.packetsReceived += 1;
    this.stats.lastPacketTime = Date.now();

    if (this.dataCallback) {
      this.dataCallback(chunk);
    }

    if (this.statsCallback && this.stats.packetsReceived % 100 === 0) {
      this.statsCallback({...this.stats});
    }
  }

  private updateState(state: ConnectionState): void {
    this.connectionState = state;
    if (this.stateCallback) {
      this.stateCallback(state);
    }
  }

  private resetStats(): void {
    this.stats = {
      bytesReceived: 0,
      packetsReceived: 0,
      connectionTime: 0,
      lastPacketTime: 0,
    };
  }
}

export default new TCPConnectionService();
