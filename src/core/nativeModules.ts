import Constants, { ExecutionEnvironment } from 'expo-constants';
import { TurboModuleRegistry } from 'react-native';

export function isExpoGo(): boolean {
  return Constants.executionEnvironment === ExecutionEnvironment.StoreClient;
}

export function hasReactNativeFs(): boolean {
  if (isExpoGo()) return false;
  try {
    return TurboModuleRegistry.get('ReactNativeFs') != null;
  } catch {
    return false;
  }
}

export function hasSherpaOnnx(): boolean {
  if (isExpoGo()) return false;
  try {
    return TurboModuleRegistry.get('SherpaOnnx') != null;
  } catch {
    return false;
  }
}

/** Safely require @dr.pogodin/react-native-fs without throwing in Expo Go or unlinked builds. */
export function tryRequireFs(): any | null {
  if (!hasReactNativeFs()) return null;
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('@dr.pogodin/react-native-fs');
  } catch {
    return null;
  }
}

/** Safely require react-native-sherpa-onnx/extraction. */
export function tryRequireExtraction(): any | null {
  if (!hasSherpaOnnx() || !hasReactNativeFs()) return null;
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('react-native-sherpa-onnx/extraction');
  } catch {
    return null;
  }
}

/** Safely require react-native-sherpa-onnx/download. */
export function tryRequireDownloadApi(): any | null {
  if (!hasSherpaOnnx()) return null;
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('react-native-sherpa-onnx/download');
  } catch {
    return null;
  }
}
