import React from 'react';
import { GestureHandlerRootView } from 'react-native-gesture-handler';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { TransmitterScreen } from './src/screens/TransmitterScreen';

export default function App() {
  return (
    <GestureHandlerRootView style={{ flex: 1 }}>
      <SafeAreaProvider>
        <TransmitterScreen />
      </SafeAreaProvider>
    </GestureHandlerRootView>
  );
}
