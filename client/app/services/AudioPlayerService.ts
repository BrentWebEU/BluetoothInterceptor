import {AudioChunk} from '../types';

class AudioBuffer {
  private buffer: Uint8Array[] = [];
  private maxBufferSize: number = 100;
  private isPlaying: boolean = false;

  addChunk(chunk: AudioChunk): void {
    if (this.buffer.length >= this.maxBufferSize) {
      this.buffer.shift();
    }
    this.buffer.push(chunk.data);
  }

  getChunk(): Uint8Array | null {
    if (this.buffer.length === 0) {
      return null;
    }
    return this.buffer.shift() || null;
  }

  clear(): void {
    this.buffer = [];
  }

  getSize(): number {
    return this.buffer.length;
  }

  setPlaying(playing: boolean): void {
    this.isPlaying = playing;
  }

  isBufferPlaying(): boolean {
    return this.isPlaying;
  }
}

class AudioPlayerService {
  private audioBuffer: AudioBuffer;
  private isInitialized: boolean = false;

  constructor() {
    this.audioBuffer = new AudioBuffer();
  }

  async initialize(): Promise<void> {
    if (this.isInitialized) {
      return;
    }

    console.log('Audio player initialized');
    this.isInitialized = true;
  }

  processAudioChunk(chunk: AudioChunk): void {
    if (!this.isInitialized) {
      console.warn('Audio player not initialized');
      return;
    }

    this.audioBuffer.addChunk(chunk);

    if (!this.audioBuffer.isBufferPlaying() && this.audioBuffer.getSize() > 5) {
      this.startPlayback();
    }
  }

  private startPlayback(): void {
    this.audioBuffer.setPlaying(true);
    
    console.log('Audio playback started (PCM data ready)');
  }

  stop(): void {
    this.audioBuffer.setPlaying(false);
    this.audioBuffer.clear();
    console.log('Audio playback stopped');
  }

  getBufferSize(): number {
    return this.audioBuffer.getSize();
  }

  shutdown(): void {
    this.stop();
    this.isInitialized = false;
  }
}

export default new AudioPlayerService();
