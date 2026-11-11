export function formatEuro(cents: number): string {
  return new Intl.NumberFormat("de-DE", {
    style: "currency",
    currency: "EUR",
  }).format(cents / 100);
}

export function formatDate(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
  }).format(new Date(seconds * 1000));
}

export function formatDateTime(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  }).format(new Date(seconds * 1000));
}

export function formatMonth(seconds: number): string {
  return new Intl.DateTimeFormat("de-DE", { month: "long", year: "numeric" }).format(
    new Date(seconds * 1000),
  );
}

export function centsFromInput(value: string): number {
  let normalized = value.trim().replace(/\s/g, "");
  // German thousands separator: 1.234,56 -> 1234.56
  normalized = normalized.replace(/\.(?=\d{3}(\D|$))/g, "").replace(",", ".");
  if (!normalized) return 0;
  const parsed = Number.parseFloat(normalized);
  if (Number.isNaN(parsed)) return 0;
  return Math.round(parsed * 100);
}

export function inputFromCents(cents: number): string {
  return (cents / 100).toFixed(2).replace(".", ",");
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
