import Constants, { ExecutionEnvironment } from 'expo-constants';

import type { SttModelDescriptor } from '../../config/models';

/** Install state of a decoder on this device. */
export type ModelStatus =
  | { state: 'unsupported'; reason: string }
  | { state: 'not-installed' }
  | { state: 'downloading'; percent: number; phase: 'downloading' | 'extracting' }
  | { state: 'installed'; path: string }
  | { state: 'error'; message: string };

/**
 * Installs sherpa-onnx decoders onto the device.
 *
 * Models are fetched from the `asr-models` release of k2-fsa/sherpa-onnx via the
 * library's own download manager, which handles the archive extraction and
 * writes a "ready" marker only after validation. That means one internet
 * connection at setup time, and fully offline operation from then on.
 *
 * Every call into the native library is behind a lazy require, so this module
 * stays importable in Expo Go and on web, where it simply reports `unsupported`.
 */
export class ModelManager {
  /**
   * Resolved install path per model id.
   *
   * Keyed by id, not a single field: languages route to different decoders, so
   * a shared cache would hand back whichever model was resolved first. That bug
   * returned the English NeMo directory when Hindi asked for Dolphin, and the
   * resulting load failure silently demoted Hindi to the placeholder decoder.
   */
  private readonly cachedPaths = new Map<string, string>();

  /**
   * Where the decoder is installed, or null if it is not.
   *
   * A side-loaded directory wins over a downloaded one. That ordering exists so
   * a model can be placed on the device by hand — over adb, or from an SD card —
   * for demos with no internet, and so a known-good copy can override a broken
   * download without clearing the library's cache.
   */
  async resolvePath(model: SttModelDescriptor): Promise<string | null> {
    const cached = this.cachedPaths.get(model.id);
    if (cached) return cached;

    const sideloaded = await this.findSideloaded(model);
    if (sideloaded) {
      this.cachedPaths.set(model.id, sideloaded);
      return sideloaded;
    }

    const api = tryRequireDownloadApi();
    if (!api) return null;

    const path = await api.getLocalModelPathByCategory(
      api.ModelCategory.Stt,
      model.id
    );
    if (path) this.cachedPaths.set(model.id, path);
    return path;
  }

  /**
   * Look for a manually-placed model at `<documents>/itantra-models/<id>`.
   * Presence of the directory is not enough — it must contain at least one
   * .onnx file, so a half-finished copy is not mistaken for a usable model.
   */
  private async findSideloaded(
    model: SttModelDescriptor
  ): Promise<string | null> {
    const fs = tryRequireFs();
    if (!fs) return null;

    const dir = `${fs.DocumentDirectoryPath}/${SIDELOAD_DIR}/${model.id}`;
    try {
      if (!(await fs.exists(dir))) return null;
      const entries = await fs.readDir(dir);
      const hasOnnx = entries.some((e: any) =>
        String(e.name).endsWith('.onnx')
      );
      if (!hasOnnx) return null;
      console.log(`[ModelManager] using side-loaded model at ${dir}`);
      return dir;
    } catch {
      return null;
    }
  }

  /** Current install state, without starting anything. */
  async status(model: SttModelDescriptor): Promise<ModelStatus> {
    // Check the side-load path before the downloader: a hand-installed model is
    // usable even on a build where the download manager is not.
    const sideloaded = await this.findSideloaded(model);
    if (sideloaded) {
      this.cachedPaths.set(model.id, sideloaded);
      return { state: 'installed', path: sideloaded };
    }

    const unsupported = describeUnsupported();
    if (unsupported) return { state: 'unsupported', reason: unsupported };

    try {
      const path = await this.resolvePath(model);
      return path ? { state: 'installed', path } : { state: 'not-installed' };
    } catch (error) {
      return { state: 'error', message: messageOf(error) };
    }
  }

  /**
   * Download and extract the decoder into the side-load directory.
   *
   * This deliberately bypasses the library's own model registry. That registry
   * resolves ids by listing a GitHub release's assets, and it returned
   * "Unknown model id: sherpa-onnx-whisper-base" for an asset that demonstrably
   * exists — the release carries 498 assets and the one we need sits at index
   * 100, right on a pagination boundary. Rather than depend on that, the
   * archive is fetched from its stable release URL and extracted here.
   *
   * Resolves to the install path, or throws with a readable message.
   */
  async install(
    model: SttModelDescriptor,
    onProgress: (percent: number, phase: 'downloading' | 'extracting') => void
  ): Promise<string> {
    const fs = tryRequireFs();
    const extraction = tryRequireExtraction();
    if (!fs || !extraction) {
      throw new Error(
        describeUnsupported() ??
          'Filesystem or extraction module unavailable in this build.'
      );
    }

    const targetDir = `${fs.DocumentDirectoryPath}/${SIDELOAD_DIR}`;
    const finalDir = `${targetDir}/${model.id}`;
    const archivePath = `${fs.DocumentDirectoryPath}/${model.id}.tar.bz2`;

    await fs.mkdir(targetDir);

    try {
      const { promise } = fs.downloadFile({
        fromUrl: releaseUrlFor(model),
        toFile: archivePath,
        background: true,
        // Throttle native->JS progress callbacks; without this the bridge is
        // flooded for a 200 MB transfer.
        progressInterval: 400,
        progress: (res: { bytesWritten: number; contentLength: number }) => {
          if (!res.contentLength) return;
          onProgress(
            Math.round((res.bytesWritten / res.contentLength) * 100),
            'downloading'
          );
        },
      });

      const result = await promise;
      if (result.statusCode !== 200) {
        throw new Error(`Download failed with HTTP ${result.statusCode}`);
      }

      onProgress(100, 'extracting');
      // `extraction` is the `react-native-sherpa-onnx/extraction` subpath, which
      // exports `extractArchive` — not `extractTarBz2`. That name is an internal
      // helper the subpath imports for its own use and never re-exports; calling
      // it here threw "undefined is not a function" after every download.
      const extractResult = await extraction.extractArchive(
        { modelId: model.id, archivePath, format: 'tar.bz2' },
        targetDir,
        { force: true }
      );
      if (!extractResult.success) {
        throw new Error(extractResult.reason ?? 'Extraction failed');
      }

      // The archive contains a top-level directory named after the model, so
      // extracting into targetDir yields exactly finalDir.
      if (!(await fs.exists(finalDir))) {
        throw new Error(
          `Extraction finished but ${model.id} is not in the expected location`
        );
      }

      this.cachedPaths.set(model.id, finalDir);
      return finalDir;
    } finally {
      // The archive is ~200 MB and useless once extracted.
      try {
        if (await fs.exists(archivePath)) await fs.unlink(archivePath);
      } catch {
        // Non-fatal: a leftover archive costs space, not correctness.
      }
    }
  }

  /** Forget cached paths, e.g. after a delete. */
  invalidate(): void {
    this.cachedPaths.clear();
  }
}

/** Why the native downloader cannot run here, or null if it can. */
function describeUnsupported(): string | null {
  if (Constants.executionEnvironment === ExecutionEnvironment.StoreClient) {
    return 'Expo Go cannot download or run speech models. Build the app with `npx expo run:android`.';
  }
  if (!tryRequireDownloadApi()) {
    return 'The sherpa-onnx native module is not present in this build.';
  }
  return null;
}

/** Directory under the app's documents dir scanned for hand-installed models. */
export const SIDELOAD_DIR = 'itantra-models';

/** Lazy, failure-tolerant access to the filesystem module. */
function tryRequireFs(): any | null {
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('@dr.pogodin/react-native-fs');
  } catch {
    return null;
  }
}

/** Stable download URL for a model's release archive. */
function releaseUrlFor(model: SttModelDescriptor): string {
  return (
    'https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/' +
    `${model.id}.tar.bz2`
  );
}

/** Lazy, failure-tolerant access to the tar.bz2 extractor. */
function tryRequireExtraction(): any | null {
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('react-native-sherpa-onnx/extraction');
  } catch {
    return null;
  }
}

/** Lazy, failure-tolerant access to the library's download subpath. */
function tryRequireDownloadApi(): any | null {
  if (Constants.executionEnvironment === ExecutionEnvironment.StoreClient) {
    return null;
  }
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('react-native-sherpa-onnx/download');
  } catch {
    return null;
  }
}

function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
