import type { ReportPeriod } from "../api/types";
import { addDays, dateInputToSeconds, startOfDay, startOfWeek } from "./format";

export interface PeriodRange {
  from?: number;
  to?: number;
}

/**
 * Period boundaries for reports. Week/month/year/all are computed by the
 * device in Europe/Berlin; the SPA only sends explicit bounds for `custom`.
 */
export function periodRange(
  period: ReportPeriod,
  customFrom?: string,
  customTo?: string,
): PeriodRange {
  if (period === "all") return {};
  if (period === "custom") {
    const from = dateInputToSeconds(customFrom ?? "");
    const toDay = dateInputToSeconds(customTo ?? "");
    return {
      from,
      to: toDay !== undefined ? toDay + 86400 : undefined,
    };
  }

  const now = new Date();
  if (period === "week") {
    const start = startOfWeek(now);
    return {
      from: Math.floor(start.getTime() / 1000),
      to: Math.floor(addDays(start, 7).getTime() / 1000),
    };
  }
  if (period === "month") {
    const start = new Date(now.getFullYear(), now.getMonth(), 1);
    const next = new Date(now.getFullYear(), now.getMonth() + 1, 1);
    return {
      from: Math.floor(start.getTime() / 1000),
      to: Math.floor(next.getTime() / 1000),
    };
  }
  const start = new Date(now.getFullYear(), 0, 1);
  const next = new Date(now.getFullYear() + 1, 0, 1);
  return {
    from: Math.floor(start.getTime() / 1000),
    to: Math.floor(next.getTime() / 1000),
  };
}

/** Ascending list of local days ending today (used by the dashboard chart). */
export function lastDays(count: number): Date[] {
  const today = startOfDay();
  return Array.from({ length: count }, (_, index) => addDays(today, index - (count - 1)));
}
