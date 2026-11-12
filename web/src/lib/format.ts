const euro = new Intl.NumberFormat("de-DE", {
  style: "currency",
  currency: "EUR",
});

const decimal = new Intl.NumberFormat("de-DE", {
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

export function formatEuro(cents: number): string {
  return euro.format(cents / 100);
}

/** Signed amount for ledger rows: +1,50 € / −0,50 €. */
export function formatSignedEuro(cents: number): string {
  const formatted = euro.format(Math.abs(cents) / 100);
  if (cents === 0) return formatted;
  return `${cents > 0 ? "+" : "−"}${formatted}`;
}

/** Decimal euros without the currency symbol (form labels, CSV hints). */
export function formatEuroPlain(cents: number): string {
  return decimal.format(cents / 100);
}

export function formatInteger(value: number): string {
  return new Intl.NumberFormat("de-DE").format(value);
}

export function formatPercent(value: number, digits = 0): string {
  return new Intl.NumberFormat("de-DE", {
    style: "percent",
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  }).format(value);
}

export function formatDate(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
  }).format(new Date(seconds * 1000));
}

export function formatDateInZone(seconds: number, timeZone = "Europe/Berlin"): string {
  return new Intl.DateTimeFormat("de-DE", {
    timeZone,
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
  }).format(new Date(seconds * 1000));
}

export function formatDateTimeInZone(seconds: number, timeZone = "Europe/Berlin"): string {
  if (!seconds) return "–";
  return new Intl.DateTimeFormat("de-DE", {
    timeZone,
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  }).format(new Date(seconds * 1000));
}

export function formatDateShort(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", {
    day: "2-digit",
    month: "2-digit",
  }).format(new Date(seconds * 1000));
}

export function formatDateTime(seconds: number): string {
  if (!seconds) return "–";
  return new Intl.DateTimeFormat("de-DE", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  }).format(new Date(seconds * 1000));
}

export function formatTime(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  }).format(new Date(seconds * 1000));
}

export function formatMonth(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", { month: "long", year: "numeric" }).format(
    new Date(seconds * 1000),
  );
}

export function formatWeekday(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", { weekday: "short" }).format(new Date(seconds * 1000));
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

export function formatDuration(seconds: number): string {
  if (seconds < 60) return `${Math.max(0, Math.floor(seconds))} s`;
  const minutes = Math.floor(seconds / 60) % 60;
  const hours = Math.floor(seconds / 3600) % 24;
  const days = Math.floor(seconds / 86400);
  const parts: string[] = [];
  if (days) parts.push(`${days} T`);
  if (hours) parts.push(`${hours} h`);
  if (minutes || parts.length === 0) parts.push(`${minutes} min`);
  return parts.join(" ");
}

/** yyyy-mm-dd for <input type="date"> in local time. */
export function dateInputValue(date = new Date()): string {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  return `${year}-${month}-${day}`;
}

export interface MoneyParse {
  cents: number;
  valid: boolean;
}

/**
 * Parse a German money input into integer cents.
 * Accepts `1,50`, `1.234,56`, `0.50`, `12`, an optional currency symbol and a
 * leading +/−. Returns `valid: false` for empty or junk input so forms can
 * reject `abc` instead of silently booking zero cents.
 */
export function parseMoneyInput(value: string): MoneyParse {
  const raw = value.trim().replace(/[\s€]/g, "");
  if (!raw) return { cents: 0, valid: false };
  const negative = raw.startsWith("-") || raw.startsWith("−");
  const unsigned = raw.replace(/^[+−-]/, "");
  // German thousands separators: 1.234,56 -> 1234.56
  const normalized = unsigned.replace(/\.(?=\d{3}(\D|$))/g, "").replace(",", ".");
  if (!/^\d+(\.\d{1,2})?$/.test(normalized)) return { cents: 0, valid: false };
  const parsed = Number.parseFloat(normalized);
  if (!Number.isFinite(parsed)) return { cents: 0, valid: false };
  return { cents: Math.round(parsed * 100) * (negative ? -1 : 1), valid: true };
}

export function centsFromInput(value: string): number {
  return parseMoneyInput(value).cents;
}


export function inputFromCents(cents: number): string {
  const sign = cents < 0 ? "-" : "";
  return `${sign}${(Math.abs(cents) / 100).toFixed(2).replace(".", ",")}`;
}

export function startOfMonth(date = new Date()): number {
  return Math.floor(new Date(date.getFullYear(), date.getMonth(), 1).getTime() / 1000);
}

export function addMonths(seconds: number, months: number): number {
  const d = new Date(seconds * 1000);
  d.setMonth(d.getMonth() + months);
  return Math.floor(d.getTime() / 1000);
}

export function monthKey(seconds: number): string {
  const d = new Date(seconds * 1000);
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}`;
}

export function dayKey(seconds: number): string {
  return dateInputValue(new Date(seconds * 1000));
}

/** Midnight (local) of the day containing `date`. */
export function startOfDay(date = new Date()): Date {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate());
}

/** Monday 00:00 (local, ISO week) of the week containing `date`. */
export function startOfWeek(date = new Date()): Date {
  const start = startOfDay(date);
  const offset = (start.getDay() + 6) % 7;
  start.setDate(start.getDate() - offset);
  return start;
}

export function addDays(date: Date, days: number): Date {
  const next = new Date(date);
  next.setDate(next.getDate() + days);
  return next;
}
