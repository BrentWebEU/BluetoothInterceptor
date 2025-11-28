# Bluetooth Interceptor - React Native App

A React Native mobile application for receiving and playing intercepted Bluetooth audio from a Raspberry Pi MITM device.

## Features

- Real-time TCP connection to Raspberry Pi interceptor
- Audio stream reception and buffering
- Live connection statistics and monitoring
- Clean, modern UI with Material Design principles
- TypeScript for type safety
- Modular architecture with separated concerns

## Architecture

### Core Services

1. **TCPConnectionService** - Manages TCP socket connection to Raspberry Pi
   - Handles connection lifecycle
   - Emits data events for received audio chunks
   - Provides connection state and statistics

2. **AudioPlayerService** - Handles audio buffering and playback
   - Buffers incoming audio chunks
   - Manages playback state
   - Ready for native SBC decoder integration

### Components

- **HomeScreen** - Main application screen with connection management
- **ConnectionForm** - IP/port input form for Pi connection
- **ConnectionStatus** - Visual connection state indicator
- **StatsDisplay** - Real-time statistics dashboard

## Prerequisites

- Node.js 16+
- React Native development environment
- Android Studio (for Android) or Xcode (for iOS)
- Raspberry Pi running the C interceptor application

## Installation

```bash
cd app
npm install
```

### Android

```bash
npx react-native run-android
```

### iOS

```bash
cd ios
pod install
cd ..
npx react-native run-ios
```

## Usage

1. Start the Bluetooth interceptor on your Raspberry Pi
2. Note the Pi's IP address (e.g., `192.168.1.100`)
3. Launch the app on your mobile device
4. Enter the Pi's IP and port (default: 8888)
5. Tap "Connect" to start receiving the audio stream

## Configuration

Default settings can be modified in:
- TCP port: Update `ConnectionForm` default or use UI
- Buffer size: Modify `AudioBuffer.maxBufferSize` in `AudioPlayerService`

## Native SBC Decoder (Optional Enhancement)

The current implementation receives raw audio data but doesn't decode SBC-encoded audio. To add full audio playback:

### Option 1: Native Module (Recommended)

1. Implement `SBCDecoderModule.java` with actual JNI bridge
2. Add libsbc or AOSP SBC decoder as native dependency
3. Create C/C++ JNI wrapper for decoder functions
4. Update `AudioPlayerService` to use native decoder

### Option 2: Server-Side Decoding (Simpler)

Modify the C interceptor to decode SBC before streaming:
1. Link libsbc in Raspberry Pi C application
2. Decode SBC → PCM in `crypto.c`
3. Stream raw PCM to React Native app
4. Use `react-native-sound` for PCM playback

## Project Structure

```
app/
├── src/
│   ├── components/          # Reusable UI components
│   │   ├── ConnectionForm.tsx
│   │   ├── ConnectionStatus.tsx
│   │   └── StatsDisplay.tsx
│   ├── screens/             # Screen components
│   │   └── HomeScreen.tsx
│   ├── services/            # Business logic services
│   │   ├── TCPConnectionService.ts
│   │   └── AudioPlayerService.ts
│   ├── types/               # TypeScript type definitions
│   │   └── index.ts
│   └── utils/               # Helper functions
│       └── helpers.ts
├── android/                 # Android native code
│   └── app/src/main/java/com/bluetoothinterceptor/
│       ├── MainActivity.java
│       ├── MainApplication.java
│       ├── SBCDecoderModule.java      # Native decoder stub
│       └── SBCDecoderPackage.java
├── App.tsx                  # Root component
├── index.js                 # App entry point
└── package.json
```

## Dependencies

- **react-native-tcp-socket** - TCP client for Pi connection
- **react-native-sound** - Audio playback (ready for integration)
- **@react-native-async-storage/async-storage** - Local storage

## Troubleshooting

### Connection Issues

- Ensure Raspberry Pi and phone are on same network
- Check firewall settings on Pi
- Verify interceptor is running: `sudo netstat -tlnp | grep 8888`

### No Audio Playback

- Current version buffers but doesn't play audio
- SBC decoder implementation required for full playback
- Check buffer size in stats display to verify data reception

## Security Notes

⚠️ **Educational purposes only**. This app is designed for authorized security research and testing. Unauthorized interception of Bluetooth communications may be illegal.

## Future Enhancements

- [ ] Implement native SBC decoder
- [ ] Add audio visualization
- [ ] Support multiple Pi connections
- [ ] Recording and playback controls
- [ ] Connection history and favorites
- [ ] Dark mode support
- [ ] Bluetooth LE support

## License

Educational purposes only. Use responsibly and with authorization.
