import React, {useState} from 'react';
import {
  View,
  Text,
  TextInput,
  TouchableOpacity,
  StyleSheet,
  Alert,
} from 'react-native';
import {validateIPAddress, validatePort} from '../utils/helpers';

interface ConnectionFormProps {
  onConnect: (host: string, port: number) => void;
  disabled?: boolean;
}

const ConnectionForm: React.FC<ConnectionFormProps> = ({
  onConnect,
  disabled = false,
}) => {
  const [host, setHost] = useState('192.168.1.100');
  const [port, setPort] = useState('8888');

  const handleConnect = () => {
    if (!validateIPAddress(host)) {
      Alert.alert('Invalid IP', 'Please enter a valid IP address');
      return;
    }

    const portNum = parseInt(port, 10);
    if (!validatePort(portNum)) {
      Alert.alert('Invalid Port', 'Port must be between 1 and 65535');
      return;
    }

    onConnect(host, portNum);
  };

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Connect to Raspberry Pi</Text>

      <View style={styles.inputGroup}>
        <Text style={styles.label}>IP Address</Text>
        <TextInput
          style={styles.input}
          value={host}
          onChangeText={setHost}
          placeholder="192.168.1.100"
          keyboardType="numeric"
          autoCapitalize="none"
          editable={!disabled}
        />
      </View>

      <View style={styles.inputGroup}>
        <Text style={styles.label}>Port</Text>
        <TextInput
          style={styles.input}
          value={port}
          onChangeText={setPort}
          placeholder="8888"
          keyboardType="numeric"
          editable={!disabled}
        />
      </View>

      <TouchableOpacity
        style={[styles.button, disabled && styles.buttonDisabled]}
        onPress={handleConnect}
        disabled={disabled}>
        <Text style={styles.buttonText}>Connect</Text>
      </TouchableOpacity>
    </View>
  );
};

const styles = StyleSheet.create({
  container: {
    padding: 16,
    backgroundColor: '#fff',
    borderRadius: 8,
    marginBottom: 16,
    elevation: 2,
    shadowColor: '#000',
    shadowOffset: {width: 0, height: 2},
    shadowOpacity: 0.1,
    shadowRadius: 4,
  },
  title: {
    fontSize: 20,
    fontWeight: '700',
    color: '#333',
    marginBottom: 16,
  },
  inputGroup: {
    marginBottom: 16,
  },
  label: {
    fontSize: 14,
    fontWeight: '600',
    color: '#666',
    marginBottom: 8,
  },
  input: {
    borderWidth: 1,
    borderColor: '#ddd',
    borderRadius: 4,
    paddingHorizontal: 12,
    paddingVertical: 10,
    fontSize: 16,
    backgroundColor: '#fff',
  },
  button: {
    backgroundColor: '#2196F3',
    paddingVertical: 14,
    borderRadius: 4,
    alignItems: 'center',
    marginTop: 8,
  },
  buttonDisabled: {
    backgroundColor: '#ccc',
  },
  buttonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
});

export default ConnectionForm;
