import type { ReportPayload } from "../api/types";

export type TimeQualityState = "trusted" | "untrusted" | "unknown";

const META_KEYS = new Set([
  "kind",
  "period",
  "from",
  "to",
  "time_quality",
  "columns",
  "rows",
  "sections",
  "totals",
]);

/** Flat scalar overviews and `totals` both render as a key/value summary. */
export function reportSummary(payload: ReportPayload): Record<string, unknown> {
  const flat = Object.fromEntries(
    Object.entries(payload).filter(
      ([key, value]) =>
        !META_KEYS.has(key) && (value === null || typeof value !== "object"),
    ),
  );
  return { ...flat, ...(payload.totals ?? {}) };
}

export function timeQualityState(trusted: boolean | undefined): TimeQualityState {
  if (trusted === true) return "trusted";
  if (trusted === false) return "untrusted";
  return "unknown";
}
