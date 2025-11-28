import React, {useState, useEffect} from 'react';
import {
  SafeAreaView,
  ScrollView,
  StyleSheet,
  Text,
  View,
  Alert,
} from 'react-native';
import ConnectionForm from '../components/ConnectionForm';
import ConnectionStatus from '../components/ConnectionStatus';
import StatsDisplay from '../components/StatsDisplay';
import TCPConnectionService from '../services/TCPConnectionService';
import AudioPlayerService from '../services/AudioPlayerService';
import {ConnectionState, ConnectionStats, AudioChunk} from '../types';

const HomeScreen: React.FC = () => {
  const [connectionState, setConnectionState] = useState<ConnectionState>(
    ConnectionState.DISCONNECTED,
  );
  const [stats, setStats] = useState<ConnectionStats>({
    bytesReceived: 0,
    packetsReceived: 0,
    connectionTime: 0,
    lastPacketTime: 0,
  });
  const [bufferSize, setBufferSize] = useState(0);
  const [currentHost, setCurrentHost] = useState<string>('');
  const [currentPort, setCurrentPort] = useState<number>(0);

  useEffect(() => {
    AudioPlayerService.initialize();

    TCPConnectionService.onStateChange((state: ConnectionState) => {
      setConnectionState(state);
      if (state === ConnectionState.ERROR) {
        Alert.alert(
          'Connection Error',
          'Failed to connect to Raspberry Pi. Please check the IP address and port.',
        );
      }
    });

    TCPConnectionService.onData((chunk: AudioChunk) => {
      AudioPlayerService.processAudioChunk(chunk);
      const newBufferSize = AudioPlayerService.getBufferSize();
      setBufferSize(newBufferSize);
    });

    TCPConnectionService.onStats((newStats: ConnectionStats) => {
      setStats(newStats);
    });

    const statsInterval = setInterval(() => {
      if (connectionState === ConnectionState.CONNECTED) {
        setStats(TCPConnectionService.getStats());
        setBufferSize(AudioPlayerService.getBufferSize());
      }
    }, 1000);

    return () => {
      clearInterval(statsInterval);
      TCPConnectionService.disconnect();
      AudioPlayerService.shutdown();
    };
  }, [connectionState]);

  const handleConnect = async (host: string, port: number) => {
    try {
      setCurrentHost(host);
      setCurrentPort(port);
      await TCPConnectionService.connect({host, port});
      Alert.alert('Connected', 'Successfully connected to Raspberry Pi');
    } catch (error) {
      console.error('Connection error:', error);
    }
  };

  const handleDisconnect = () => {
    TCPConnectionService.disconnect();
    AudioPlayerService.stop();
    setCurrentHost('');
    setCurrentPort(0);
  };

  return (
    <SafeAreaView style={styles.container}>
      <ScrollView style={styles.scrollView}>
        <View style={styles.header}>
          <Text style={styles.headerTitle}>Bluetooth Interceptor</Text>
          <Text style={styles.headerSubtitle}>
            Audio Stream Receiver
          </Text>
        </View>

        <View style={styles.content}>
          {connectionState === ConnectionState.DISCONNECTED ? (
            <ConnectionForm
              onConnect={handleConnect}
              disabled={connectionState === ConnectionState.CONNECTING}
            />
          ) : (
            <>
              <ConnectionStatus
                state={connectionState}
                host={currentHost}
                port={currentPort}
                onDisconnect={handleDisconnect}
              />

              {connectionState === ConnectionState.CONNECTED && (
                <>
                  <StatsDisplay stats={stats} bufferSize={bufferSize} />

                  <View style={styles.infoCard}>
                    <Text style={styles.infoTitle}>Status</Text>
                    <Text style={styles.infoText}>
                      {bufferSize > 0
                        ? '🎵 Receiving audio data'
                        : '⏳ Waiting for data...'}
                    </Text>
                    <Text style={styles.infoSubtext}>
                      The intercepted audio stream is being received from the
                      Raspberry Pi. Raw PCM data is buffered for playback.
                    </Text>
                  </View>
                </>
              )}
            </>
          )}

          <View style={styles.infoCard}>
            <Text style={styles.infoTitle}>How It Works</Text>
            <Text style={styles.infoText}>
              1. Start the C interceptor on Raspberry Pi
            </Text>
            <Text style={styles.infoText}>
              2. Enter Pi's IP address and port (default: 8888)
            </Text>
            <Text style={styles.infoText}>
              3. Connect to receive intercepted audio
            </Text>
            <Text style={styles.infoSubtext}>
              Note: Audio playback requires native SBC decoder module (not
              implemented in this version).
            </Text>
          </View>
        </View>
      </ScrollView>
    </SafeAreaView>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#fff',
  },
  scrollView: {
    flex: 1,
  },
  header: {
    backgroundColor: '#2196F3',
    paddingVertical: 24,
    paddingHorizontal: 16,
  },
  headerTitle: {
    fontSize: 28,
    fontWeight: '700',
    color: '#fff',
    marginBottom: 4,
  },
  headerSubtitle: {
    fontSize: 16,
    color: '#E3F2FD',
  },
  content: {
    padding: 16,
  },
  infoCard: {
    padding: 16,
    backgroundColor: '#E3F2FD',
    borderRadius: 8,
    marginBottom: 16,
  },
  infoTitle: {
    fontSize: 16,
    fontWeight: '700',
    color: '#1976D2',
    marginBottom: 12,
  },
  infoText: {
    fontSize: 14,
    color: '#333',
    marginBottom: 8,
    lineHeight: 20,
  },
  infoSubtext: {
    fontSize: 12,
    color: '#666',
    marginTop: 8,
    fontStyle: 'italic',
  },
});

export default HomeScreen;
