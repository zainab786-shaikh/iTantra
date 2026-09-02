/** Conversions and measurements over raw PCM. All hot-path, no allocations beyond the result. */

/**
 * Reinterpret an int16 little-endian buffer as normalized floats in [-1, 1].
 *
 * expo-audio hands us an ArrayBuffer whose byteOffset is not guaranteed to be
 * zero, so the view is constructed defensively rather than with `new Int16Array(buf)`.
 */
export function int16ToFloat32(buffer: ArrayBuffer): Float32Array {
  const int16 = new Int16Array(buffer, 0, Math.floor(buffer.byteLength / 2));
  const out = new Float32Array(int16.length);
  for (let i = 0; i < int16.length; i++) {
    const s = int16[i]!;
    // Asymmetric scaling: int16 range is [-32768, 32767].
    out[i] = s < 0 ? s / 32768 : s / 32767;
  }
  return out;
}

/** Root-mean-square amplitude of a frame, 0..1. */
export function rms(samples: Float32Array): number {
  if (samples.length === 0) return 0;
  let sum = 0;
  for (let i = 0; i < samples.length; i++) {
    const v = samples[i]!;
    sum += v * v;
  }
  return Math.sqrt(sum / samples.length);
}

/** Zero-crossing rate, 0..1. Separates voiced speech from broadband hiss. */
export function zeroCrossingRate(samples: Float32Array): number {
  if (samples.length < 2) return 0;
  let crossings = 0;
  for (let i = 1; i < samples.length; i++) {
    if (samples[i - 1]! < 0 !== samples[i]! < 0) crossings++;
  }
  return crossings / (samples.length - 1);
}

/**
 * Map an RMS value to a perceptually even 0..1 meter reading.
 *
 * Speech RMS lives around 0.02-0.3, so a linear meter barely moves. This maps
 * a -60..0 dBFS window onto 0..1.
 */
export function rmsToLevel(value: number): number {
  if (value <= 0) return 0;
  const db = 20 * Math.log10(value);
  const MIN_DB = -60;
  return Math.min(1, Math.max(0, (db - MIN_DB) / -MIN_DB));
}

/**
 * Linear-interpolating resampler. Only used when the hardware refuses 16 kHz
 * and expo-audio falls back to another rate; at equal rates the input is
 * returned untouched.
 */
export function resampleLinear(
  samples: Float32Array,
  fromRate: number,
  toRate: number
): Float32Array {
  if (fromRate === toRate || samples.length === 0) return samples;
  const ratio = fromRate / toRate;
  const outLength = Math.floor(samples.length / ratio);
  const out = new Float32Array(outLength);
  for (let i = 0; i < outLength; i++) {
    const pos = i * ratio;
    const idx = Math.floor(pos);
    const frac = pos - idx;
    const a = samples[idx] ?? 0;
    const b = samples[idx + 1] ?? a;
    out[i] = a + (b - a) * frac;
  }
  return out;
}

/** Concatenate frames into one contiguous buffer. */
export function concatFloat32(chunks: readonly Float32Array[]): Float32Array {
  let total = 0;
  for (const c of chunks) total += c.length;
  const out = new Float32Array(total);
  let offset = 0;
  for (const c of chunks) {
    out.set(c, offset);
    offset += c.length;
  }
  return out;
}

/**
 * sherpa-onnx's bridge takes a plain number[]. Converting here keeps the
 * conversion cost visible and confined to one call per utterance, rather than
 * per frame.
 */
export function toSampleArray(samples: Float32Array): number[] {
  return Array.from(samples);
}
