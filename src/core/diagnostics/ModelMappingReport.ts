import { LANGUAGES } from '../../config/languages';
import { resolveModelForLanguage, STT_MODELS, type SttModelDescriptor } from '../../config/models';
import type { SttEngineProvider } from '../stt/SttEngineProvider';
import { ModelManager } from '../stt/ModelManager';

/**
 * Model-mapping diagnostic. TEMPORARY, dev-only.
 *
 * Answers, per language, without assuming the UI label is honest:
 *   - which decoder id config actually routes this language to
 *   - what directory/files are actually installed on disk for that id
 *   - whether the real backend actually loaded it (not just "installed")
 *   - whether any OTHER language maps to the exact same decoder id — this is
 *     the ground truth for "is Dolphin genuinely one multilingual model, or
 *     does the registry expect a separate model per Indic language". It is
 *     read directly from STT_MODELS/resolveModelForLanguage, not inferred.
 *
 * Does not install, delete, or replace any model.
 */
export interface ModelMappingEntry {
  languageCode: string;
  languageLabel: string;
  /** Decoder id resolveModelForLanguage() actually returns for this language. */
  modelId: string | null;
  modelType: string | null;
  /** Every other language code that resolves to this same decoder id. */
  sharedWithLanguages: string[];
  /** On-disk directory sherpa-onnx would load, per ModelManager, or null. */
  modelDir: string | null;
  /** .onnx / config files found in modelDir, with byte sizes. */
  modelFiles: Array<{ name: string; sizeBytes: number | null }>;
  totalSizeBytes: number | null;
  installState: 'unsupported' | 'not-installed' | 'installed' | 'error' | 'downloading';
  installDetail: string | null;
  /** Result of actually calling sttProvider.prepare(languageCode) — the real load, not just a status check. */
  loadedSuccessfully: boolean;
  loadFailureReason: string | null;
}

export async function runModelMappingReport(
  sttProvider: SttEngineProvider
): Promise<ModelMappingEntry[]> {
  const models = new ModelManager();
  const results: ModelMappingEntry[] = [];

  for (const lang of LANGUAGES) {
    const descriptor = resolveModelForLanguage(lang.code);
    const sharedWith = descriptor
      ? STT_MODELS.filter((m) => m.id === descriptor.id)
          .flatMap((m) => m.languages)
          .filter((code) => code !== lang.code)
      : [];

    const entry: ModelMappingEntry = {
      languageCode: lang.code,
      languageLabel: lang.label,
      modelId: descriptor?.id ?? null,
      modelType: descriptor?.modelType ?? null,
      sharedWithLanguages: sharedWith,
      modelDir: null,
      modelFiles: [],
      totalSizeBytes: null,
      installState: 'unsupported',
      installDetail: null,
      loadedSuccessfully: false,
      loadFailureReason: null,
    };

    if (!descriptor) {
      entry.installDetail = 'No SttModelDescriptor registered for this language.';
      results.push(entry);
      // eslint-disable-next-line no-continue
      continue;
    }

    // eslint-disable-next-line no-await-in-loop
    const status = await models.status(descriptor);
    entry.installState = status.state === 'downloading' ? 'downloading' : status.state;
    if (status.state === 'unsupported') entry.installDetail = status.reason;
    if (status.state === 'error') entry.installDetail = status.message;

    if (status.state === 'installed') {
      entry.modelDir = status.path;
      // eslint-disable-next-line no-await-in-loop
      const files = await listDirWithSizes(status.path);
      entry.modelFiles = files;
      entry.totalSizeBytes = files.reduce(
        (sum, f) => sum + (f.sizeBytes ?? 0),
        0
      );
    }

    // eslint-disable-next-line no-await-in-loop
    const prepared = await sttProvider.prepare(lang.code);
    entry.loadedSuccessfully = prepared.kind === 'sherpa-onnx';
    entry.loadFailureReason = prepared.reason;

    results.push(entry);
  }

  return results;
}

/**
 * Whether every language sharing a decoder id also has an identical on-disk
 * directory path and file set — proof the "multilingual" claim isn't secretly
 * routing to different bytes per language despite the shared id.
 */
export function verifySharedModelIsIdentical(
  entries: ModelMappingEntry[]
): { modelId: string; consistent: boolean; dirs: Record<string, string | null> }[] {
  const byModel = new Map<string, ModelMappingEntry[]>();
  for (const e of entries) {
    if (!e.modelId) continue;
    const list = byModel.get(e.modelId) ?? [];
    list.push(e);
    byModel.set(e.modelId, list);
  }

  const out: { modelId: string; consistent: boolean; dirs: Record<string, string | null> }[] = [];
  for (const [modelId, list] of byModel) {
    if (list.length < 2) continue;
    const dirs: Record<string, string | null> = {};
    for (const e of list) dirs[e.languageCode] = e.modelDir;
    const uniqueDirs = new Set(Object.values(dirs));
    out.push({ modelId, consistent: uniqueDirs.size === 1, dirs });
  }
  return out;
}

async function listDirWithSizes(
  dir: string
): Promise<Array<{ name: string; sizeBytes: number | null }>> {
  const fs = tryRequireFs();
  if (!fs) return [];
  try {
    const entries = await fs.readDir(dir);
    return entries.map((e: any) => ({
      name: String(e.name),
      sizeBytes: typeof e.size === 'number' ? e.size : null,
    }));
  } catch {
    return [];
  }
}

function tryRequireFs(): any | null {
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require('@dr.pogodin/react-native-fs');
  } catch {
    return null;
  }
}

export type { SttModelDescriptor };
