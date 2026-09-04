import Constants, { ExecutionEnvironment } from 'expo-constants';

import type { TtsModelDescriptor } from '../../config/ttsModels';
import { tryRequireExtraction, tryRequireFs } from '../nativeModules';
import type { TtsVoiceStatus } from './types';

/**
 * Installs TTS voices onto the device. Sibling of STT's ModelManager, kept
 * as a separate class and a separate on-disk directory
 * (`itantra-tts-models`, not STT's `itantra-models`) so the TTS workstream
 * cannot touch STT's model state even by accident — STT is frozen for this
 * task.
 *
 * Piper and MMS voices are packaged differently (verified by downloading
 * one of each, not assumed): Piper ships as a `.tar.bz2` archive from the
 * k2-fsa/sherpa-onnx `tts-models` release; the MMS conversions this app uses
 * ship as loose files (`model.onnx` + `tokens.txt`) in a Hugging Face repo,
 * with no archive at all. `install()` branches on `descriptor.source.kind`
 * to handle both.
 */
export class TtsModelManager {
  private readonly cachedPaths = new Map<string, string>();

  /** Where the voice is installed, or null if it is not. */
  async resolvePath(model: TtsModelDescriptor): Promise<string | null> {
    const cached = this.cachedPaths.get(model.id);
    if (cached) return cached;

    const dir = `${await sideloadRoot()}/${model.id}`;
    const fs = tryRequireFs();
    if (!fs) return null;

    try {
      if (!(await fs.exists(dir))) return null;
      const hasOnnx = await dirHasOnnx(fs, dir);
      if (!hasOnnx) return null;
      this.cachedPaths.set(model.id, dir);
      return dir;
    } catch {
      return null;
    }
  }

  /** Current install state, without starting anything. */
  async status(model: TtsModelDescriptor): Promise<TtsVoiceStatus> {
    const unsupported = describeUnsupported();
    if (unsupported) return { state: 'not-installed' };

    try {
      const path = await this.resolvePath(model);
      return path ? { state: 'installed', path } : { state: 'not-installed' };
    } catch (error) {
      return { state: 'error', message: messageOf(error) };
    }
  }

  /**
   * Download and install a voice. Resolves to the install path, or throws
   * with a readable message.
   */
  async install(
    model: TtsModelDescriptor,
    onProgress: (percent: number, phase: 'downloading' | 'extracting') => void
  ): Promise<string> {
    const fs = tryRequireFs();
    if (!fs) {
      throw new Error(
        describeUnsupported() ?? 'Filesystem module unavailable in this build.'
      );
    }

    const root = await sideloadRoot();
    const finalDir = `${root}/${model.id}`;
    await fs.mkdir(root);

    if (model.source.kind === 'archive') {
      await this.installArchive(model, model.source.url, finalDir, onProgress, fs);
    } else {
      await this.installLooseFiles(model, model.source, finalDir, onProgress, fs);
    }

    if (!(await dirHasOnnx(fs, finalDir))) {
      throw new Error(
        `Install finished but ${model.id} is not in the expected location`
      );
    }

    this.cachedPaths.set(model.id, finalDir);
    return finalDir;
  }

  private async installArchive(
    model: TtsModelDescriptor,
    url: string,
    finalDir: string,
    onProgress: (percent: number, phase: 'downloading' | 'extracting') => void,
    fs: any
  ): Promise<void> {
    const extraction = tryRequireExtraction();
    if (!extraction) {
      throw new Error('Archive extraction module unavailable in this build.');
    }
    const root = await sideloadRoot();
    const archivePath = `${root}/${model.id}.tar.bz2`;

    try {
      const { promise } = fs.downloadFile({
        fromUrl: url,
        toFile: archivePath,
        background: true,
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
      const extractResult = await extraction.extractArchive(
        { modelId: model.id, archivePath, format: 'tar.bz2' },
        root,
        { force: true }
      );
      if (!extractResult.success) {
        throw new Error(extractResult.reason ?? 'Extraction failed');
      }
    } finally {
      try {
        if (await fs.exists(archivePath)) await fs.unlink(archivePath);
      } catch {
        // Non-fatal: a leftover archive costs space, not correctness.
      }
    }
  }

  private async installLooseFiles(
    model: TtsModelDescriptor,
    source: Extract<TtsModelDescriptor['source'], { kind: 'files' }>,
    finalDir: string,
    onProgress: (percent: number, phase: 'downloading' | 'extracting') => void,
    fs: any
  ): Promise<void> {
    await fs.mkdir(finalDir);

    // Sizes vary a lot between files (model.onnx is ~114 MB, tokens.txt is a
    // few hundred bytes) — weight overall progress by the biggest file's
    // share rather than by file count, so the bar doesn't jump to ~90% the
    // instant tokens.txt finishes.
    let completedBytes = 0;
    let totalBytes = 0;
    const fileTotals = new Map<string, number>();

    for (const file of source.files) {
      const toFile = `${finalDir}/${file}`;
      const { promise } = fs.downloadFile({
        fromUrl: `${source.baseUrl}${file}`,
        toFile,
        background: true,
        progressInterval: 400,
        progress: (res: { bytesWritten: number; contentLength: number }) => {
          if (!res.contentLength) return;
          fileTotals.set(file, res.contentLength);
          totalBytes = sum(fileTotals.values());
          const prevForFile = completedBytes;
          const percent = totalBytes
            ? Math.round(((prevForFile + res.bytesWritten) / totalBytes) * 100)
            : 0;
          onProgress(Math.min(percent, 99), 'downloading');
        },
      });

      const result = await promise;
      if (result.statusCode !== 200) {
        throw new Error(
          `Download of ${file} failed with HTTP ${result.statusCode}`
        );
      }
      completedBytes += fileTotals.get(file) ?? 0;
    }

    onProgress(100, 'downloading');
  }

  /** Forget cached paths, e.g. after a delete. */
  invalidate(): void {
    this.cachedPaths.clear();
  }
}

function sum(values: IterableIterator<number>): number {
  let total = 0;
  for (const v of values) total += v;
  return total;
}

async function dirHasOnnx(fs: any, dir: string): Promise<boolean> {
  try {
    const entries = await fs.readDir(dir);
    return entries.some((e: any) => String(e.name).endsWith('.onnx'));
  } catch {
    return false;
  }
}

/** Directory under the app's documents dir where TTS voices are installed. */
export const TTS_SIDELOAD_DIR = 'itantra-tts-models';

async function sideloadRoot(): Promise<string> {
  const fs = tryRequireFs();
  if (!fs) throw new Error('Filesystem module unavailable in this build.');
  return `${fs.DocumentDirectoryPath}/${TTS_SIDELOAD_DIR}`;
}

/** Why the native TTS engine cannot run here, or null if it can. */
function describeUnsupported(): string | null {
  if (Constants.executionEnvironment === ExecutionEnvironment.StoreClient) {
    return 'Expo Go cannot download or run speech models. Build the app with `npx expo run:android`.';
  }
  if (!tryRequireFs()) {
    return 'The sherpa-onnx native module is not present in this build.';
  }
  return null;
}


function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
