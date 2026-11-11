import type { ReportPeriod } from "../api/types";
import { addDays, startOfDay } from "./format";

export const BERLIN_TZ = "Europe/Berlin";

export interface PeriodRange {
  from?: number;
  to?: number;
}

const timeZoneFormatters = new Map<string, Intl.DateTimeFormat>();

function zonedParts(utcSeconds: number, timeZone: string) {
  let formatter = timeZoneFormatters.get(timeZone);
  if (!formatter) {
    formatter = new Intl.DateTimeFormat("en-GB", {
      timeZone,
      hourCycle: "h23",
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
    timeZoneFormatters.set(timeZone, formatter);
  }
  const parts = formatter.formatToParts(new Date(utcSeconds * 1000));
  const value = (type: string) => Number(parts.find((part) => part.type === type)?.value ?? 0);
  return {
    year: value("year"),
    month: value("month"),
    day: value("day"),
    hour: value("hour"),
    minute: value("minute"),
    second: value("second"),
  };
}

function offsetSeconds(utcSeconds: number, timeZone: string): number {
  const parts = zonedParts(utcSeconds, timeZone);
  const asUtc = Date.UTC(
    parts.year,
    parts.month - 1,
    parts.day,
    parts.hour,
    parts.minute,
    parts.second,
  );
  return asUtc / 1000 - utcSeconds;
}

function parseDate(value: string) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  if (!match) return undefined;
  return { year: Number(match[1]), month: Number(match[2]), day: Number(match[3]) };
}

function formatDateParts(year: number, month: number, day: number): string {
  return `${year}-${String(month).padStart(2, "0")}-${String(day).padStart(2, "0")}`;
}

/** Epoch seconds for local midnight of `yyyy-mm-dd` in an IANA time zone. */
export function zonedMidnightSeconds(
  value: string,
  timeZone = BERLIN_TZ,
): number | undefined {
  const parts = parseDate(value);
  if (!parts) return undefined;
  const guess = Date.UTC(parts.year, parts.month - 1, parts.day) / 1000;
  let result = guess - offsetSeconds(guess, timeZone);
  // Re-evaluate once for DST boundaries.
  result = guess - offsetSeconds(result, timeZone);
  return result;
}

/** Add calendar days to a `yyyy-mm-dd` string without DST arithmetic. */
export function addCalendarDays(value: string, days: number): string | undefined {
  const parts = parseDate(value);
  if (!parts) return undefined;
  const utc = new Date(Date.UTC(parts.year, parts.month - 1, parts.day + days));
  return formatDateParts(utc.getUTCFullYear(), utc.getUTCMonth() + 1, utc.getUTCDate());
}

function todayInZone(timeZone: string): string {
  const parts = zonedParts(Math.floor(Date.now() / 1000), timeZone);
  return formatDateParts(parts.year, parts.month, parts.day);
}

function weekday(value: string): number {
  const parts = parseDate(value);
  if (!parts) return 0;
  return new Date(Date.UTC(parts.year, parts.month - 1, parts.day)).getUTCDay();
}

/**
 * Period boundaries for reports in Europe/Berlin (Mo–So for weeks).
 * Custom ranges are inclusive calendar days; `to` is the exclusive midnight of
 * the day after the selected end date and is DST-safe.
 */
export function periodRange(
  period: ReportPeriod,
  customFrom?: string,
  customTo?: string,
  timeZone = BERLIN_TZ,
): PeriodRange {
  if (period === "all") return {};
  if (period === "custom") {
    const from = customFrom ? zonedMidnightSeconds(customFrom, timeZone) : undefined;
    const endDay = customTo ? addCalendarDays(customTo, 1) : undefined;
    const to = endDay ? zonedMidnightSeconds(endDay, timeZone) : undefined;
    return { from, to };
  }

  const today = todayInZone(timeZone);
  const parts = parseDate(today);
  if (!parts) return {};

  if (period === "week") {
    const startDay = addCalendarDays(today, -((weekday(today) + 6) % 7));
    const endDay = startDay ? addCalendarDays(startDay, 7) : undefined;
    return {
      from: startDay ? zonedMidnightSeconds(startDay, timeZone) : undefined,
      to: endDay ? zonedMidnightSeconds(endDay, timeZone) : undefined,
    };
  }
  if (period === "month") {
    const start = formatDateParts(parts.year, parts.month, 1);
    const nextMonth = parts.month === 12 ? { year: parts.year + 1, month: 1 } : { year: parts.year, month: parts.month + 1 };
    const end = formatDateParts(nextMonth.year, nextMonth.month, 1);
    return {
      from: zonedMidnightSeconds(start, timeZone),
      to: zonedMidnightSeconds(end, timeZone),
    };
  }
  return {
    from: zonedMidnightSeconds(formatDateParts(parts.year, 1, 1), timeZone),
    to: zonedMidnightSeconds(formatDateParts(parts.year + 1, 1, 1), timeZone),
  };
}

/** YYYY-MM of an instant in a time zone (used for report filenames). */
export function monthKeyInZone(seconds: number, timeZone = BERLIN_TZ): string {
  const parts = zonedParts(seconds, timeZone);
  return `${parts.year}-${String(parts.month).padStart(2, "0")}`;
}

/** Ascending local days ending today (used by the dashboard chart). */
export function lastDays(count: number): Date[] {
  const today = startOfDay();
  return Array.from({ length: count }, (_, index) => addDays(today, index - (count - 1)));
}
