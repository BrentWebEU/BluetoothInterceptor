import React from 'react';
import {View, Text, StyleSheet, TouchableOpacity} from 'react-native';
import {ConnectionState} from '../types';

interface ConnectionStatusProps {
  state: ConnectionState;
  host?: string;
  port?: number;
  onDisconnect?: () => void;
}

const ConnectionStatus: React.FC<ConnectionStatusProps> = ({
  state,
  host,
  port,
  onDisconnect,
}) => {
  const getStatusColor = () => {
    switch (state) {
      case ConnectionState.CONNECTED:
        return '#4CAF50';
      case ConnectionState.CONNECTING:
        return '#FFC107';
      case ConnectionState.ERROR:
        return '#F44336';
      default:
        return '#9E9E9E';
    }
  };

  const getStatusText = () => {
    switch (state) {
      case ConnectionState.CONNECTED:
        return 'Connected';
      case ConnectionState.CONNECTING:
        return 'Connecting...';
      case ConnectionState.ERROR:
        return 'Connection Error';
      default:
        return 'Disconnected';
    }
  };

  return (
    <View style={styles.container}>
      <View style={styles.statusRow}>
        <View style={[styles.indicator, {backgroundColor: getStatusColor()}]} />
        <Text style={styles.statusText}>{getStatusText()}</Text>
      </View>
      
      {host && port && (
        <Text style={styles.addressText}>
          {host}:{port}
        </Text>
      )}
      
      {state === ConnectionState.CONNECTED && onDisconnect && (
        <TouchableOpacity style={styles.disconnectButton} onPress={onDisconnect}>
          <Text style={styles.disconnectText}>Disconnect</Text>
        </TouchableOpacity>
      )}
    </View>
  );
};

const styles = StyleSheet.create({
  container: {
    padding: 16,
    backgroundColor: '#f5f5f5',
    borderRadius: 8,
    marginBottom: 16,
  },
  statusRow: {
    flexDirection: 'row',
    alignItems: 'center',
    marginBottom: 8,
  },
  indicator: {
    width: 12,
    height: 12,
    borderRadius: 6,
    marginRight: 8,
  },
  statusText: {
    fontSize: 16,
    fontWeight: '600',
    color: '#333',
  },
  addressText: {
    fontSize: 14,
    color: '#666',
    marginLeft: 20,
  },
  disconnectButton: {
    marginTop: 12,
    backgroundColor: '#F44336',
    paddingVertical: 8,
    paddingHorizontal: 16,
    borderRadius: 4,
    alignSelf: 'flex-start',
  },
  disconnectText: {
    color: '#fff',
    fontSize: 14,
    fontWeight: '600',
  },
});

export default ConnectionStatus;
