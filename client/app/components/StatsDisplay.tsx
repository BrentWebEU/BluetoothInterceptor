import React from 'react';
import {View, Text, StyleSheet} from 'react-native';
import {ConnectionStats} from '../types';
import {formatBytes, formatDuration} from '../utils/helpers';

interface StatsDisplayProps {
  stats: ConnectionStats;
  bufferSize?: number;
}

const StatsDisplay: React.FC<StatsDisplayProps> = ({stats, bufferSize}) => {
  const getUptime = () => {
    if (stats.connectionTime === 0) return '0s';
    return formatDuration(Date.now() - stats.connectionTime);
  };

  const getLastPacketAge = () => {
    if (stats.lastPacketTime === 0) return 'N/A';
    const age = Date.now() - stats.lastPacketTime;
    return age < 1000 ? `${age}ms ago` : `${Math.floor(age / 1000)}s ago`;
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Statistics</Text>
      
      <View style={styles.grid}>
        <View style={styles.statItem}>
          <Text style={styles.statLabel}>Data Received</Text>
          <Text style={styles.statValue}>
            {formatBytes(stats.bytesReceived)}
          </Text>
        </View>

        <View style={styles.statItem}>
          <Text style={styles.statLabel}>Packets</Text>
          <Text style={styles.statValue}>
            {stats.packetsReceived.toLocaleString()}
          </Text>
        </View>

        <View style={styles.statItem}>
          <Text style={styles.statLabel}>Uptime</Text>
          <Text style={styles.statValue}>{getUptime()}</Text>
        </View>

        <View style={styles.statItem}>
          <Text style={styles.statLabel}>Last Packet</Text>
          <Text style={styles.statValue}>{getLastPacketAge()}</Text>
        </View>

        {bufferSize !== undefined && (
          <View style={styles.statItem}>
            <Text style={styles.statLabel}>Buffer Size</Text>
            <Text style={styles.statValue}>{bufferSize} chunks</Text>
          </View>
        )}
      </View>
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
  title: {
    fontSize: 18,
    fontWeight: '700',
    color: '#333',
    marginBottom: 16,
  },
  grid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    marginHorizontal: -8,
  },
  statItem: {
    width: '50%',
    paddingHorizontal: 8,
    marginBottom: 16,
  },
  statLabel: {
    fontSize: 12,
    color: '#666',
    marginBottom: 4,
  },
  statValue: {
    fontSize: 16,
    fontWeight: '600',
    color: '#333',
  },
});

export default StatsDisplay;
