import { SAMPLE_RATE } from '../../config/vadConfig';

/**
 * Writes a captured segment to a WAV file for offline inspection.
 *
 * Development aid only. When two different recognisers both garble live speech
 * but decode a reference file correctly, the audio itself is the suspect — and
 * the only way to settle that is to listen to exactly what the decoder was
 * handed, rather than to what the microphone was supposed to have produced.
 */
export async function dumpSegmentWav(
  samples: Float32Array,
  path: string
): Promise<void> {
  let fs: any;
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    fs = require('@dr.pogodin/react-native-fs');
  } catch {
    return;
  }

  const bytes = encodeWav(samples, SAMPLE_RATE);
  await fs.writeFile(path, toBase64(bytes), 'base64');
  console.log(
    `[debugWav] wrote ${samples.length} samples ` +
      `(${(samples.length / SAMPLE_RATE).toFixed(2)}s) to ${path}`
  );
}

/** Minimal 16-bit PCM mono WAV encoder. */
function encodeWav(samples: Float32Array, sampleRate: number): Uint8Array {
  const dataBytes = samples.length * 2;
  const buffer = new ArrayBuffer(44 + dataBytes);
  const view = new DataView(buffer);

  const ascii = (offset: number, text: string) => {
    for (let i = 0; i < text.length; i++) {
      view.setUint8(offset + i, text.charCodeAt(i));
    }
  };

  ascii(0, 'RIFF');
  view.setUint32(4, 36 + dataBytes, true);
  ascii(8, 'WAVE');
  ascii(12, 'fmt ');
  view.setUint32(16, 16, true); // PCM chunk size
  view.setUint16(20, 1, true); // format = PCM
  view.setUint16(22, 1, true); // mono
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * 2, true); // byte rate
  view.setUint16(32, 2, true); // block align
  view.setUint16(34, 16, true); // bits per sample
  ascii(36, 'data');
  view.setUint32(40, dataBytes, true);

  for (let i = 0; i < samples.length; i++) {
    const clamped = Math.max(-1, Math.min(1, samples[i]!));
    view.setInt16(44 + i * 2, clamped * 32767, true);
  }
  return new Uint8Array(buffer);
}

const B64 =
  'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

/** Hermes has no Buffer and an unreliable btoa, so encode by hand. */
function toBase64(bytes: Uint8Array): string {
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const b0 = bytes[i]!;
    const b1 = bytes[i + 1];
    const b2 = bytes[i + 2];
    out += B64[b0 >> 2];
    out += B64[((b0 & 3) << 4) | ((b1 ?? 0) >> 4)];
    out += b1 === undefined ? '=' : B64[((b1 & 15) << 2) | ((b2 ?? 0) >> 6)];
    out += b2 === undefined ? '=' : B64[b2 & 63];
  }
  return out;
}
