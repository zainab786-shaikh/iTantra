import * as Application from 'expo-application';
import * as Crypto from 'expo-crypto';
import { Platform } from 'react-native';

let cached: string | null = null;

/**
 * Stable per-installation sender ID.
 *
 * Prefers the OS-provided installation identifier (Android's
 * `getAndroidId()`, iOS's `identifierForVendor`) so the ID survives app
 * restarts. When neither is available — web, or a platform that withholds it —
 * a random UUID is generated for the session, which keeps packets
 * well-formed without inventing a false identity claim.
 */
export async function getSenderId(): Promise<string> {
  if (cached) return cached;

  let raw: string | null = null;
  try {
    if (Platform.OS === 'android') {
      raw = Application.getAndroidId();
    } else if (Platform.OS === 'ios') {
      raw = await Application.getIosIdForVendorAsync();
    }
  } catch {
    raw = null;
  }

  cached = raw && raw.length > 0 ? `ITX-${short(raw)}` : `ITX-${short(Crypto.randomUUID())}`;
  return cached;
}

/** Compact, human-readable 8-char tag for display in the UI. */
function short(value: string): string {
  return value.replace(/[^a-zA-Z0-9]/g, '').slice(0, 8).toUpperCase();
}
